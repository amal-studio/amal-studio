/******************************************************************************
    Amal Studio - module « Multidiffusion »
    Copyright (C) 2026 Amal Ben Aly

    Diffuse le direct en même temps vers plusieurs plateformes (YouTube,
    Facebook, Twitch, TikTok, Instagram, serveur personnalisé), en plus de la
    destination principale (Paramètres > Stream).

    Les destinations supplémentaires réutilisent les encodeurs du direct
    principal : l'image n'est encodée qu'une seule fois, l'ordinateur n'est pas
    plus sollicité ; seule la connexion Internet envoie une copie par plateforme.

    Les clés de stream sont enregistrées sur cet ordinateur seulement
    (dossier de configuration d'Amal Studio), jamais dans les projets.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/config-file.h>
#include <util/platform.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <memory>
#include <cstddef>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-multidiffusion", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : multidiffusion (plusieurs plateformes en même temps)";
}

namespace {

constexpr const char *DOCK_ID = "amal-multidiffusion";
constexpr const char *CONFIG_SECTION = "AmalMultidiffusion";

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* Plateformes : adresse du serveur connue, ou à copier depuis la plateforme */
struct Platform {
	const char *key;
	const char *nameKey;
	const char *server;
};

const Platform PLATFORMS[] = {
	{"youtube", "Platform.YouTube", "rtmp://a.rtmp.youtube.com/live2"},
	{"facebook", "Platform.Facebook", "rtmps://live-api-s.facebook.com:443/rtmp/"},
	{"twitch", "Platform.Twitch", "rtmp://live.twitch.tv/app"},
	{"tiktok", "Platform.TikTok", ""},
	{"instagram", "Platform.Instagram", ""},
	{"autre", "Platform.Other", ""},
};

const Platform *FindPlatform(const QString &key)
{
	for (const Platform &p : PLATFORMS) {
		if (key == p.key) {
			return &p;
		}
	}
	return &PLATFORMS[sizeof(PLATFORMS) / sizeof(PLATFORMS[0]) - 1];
}

struct Destination {
	QString name;
	QString platform;
	QString server;
	QString key;
	bool enabled = true;

	QJsonObject toJson() const
	{
		return QJsonObject{{"nom", name}, {"plateforme", platform}, {"serveur", server}, {"cle", key},
				   {"active", enabled}};
	}

	static Destination fromJson(const QJsonObject &o)
	{
		Destination d;
		d.name = o.value("nom").toString();
		d.platform = o.value("plateforme").toString("autre");
		d.server = o.value("serveur").toString();
		d.key = o.value("cle").toString();
		d.enabled = o.value("active").toBool(true);
		return d;
	}
};

/* État d'une sortie en cours, mis à jour par les signaux du moteur (autres fils) */
enum class State { Off, Connecting, Live, Reconnecting, Error };

void OnStart(void *data, calldata_t *);
void OnStop(void *data, calldata_t *cd);
void OnReconnect(void *data, calldata_t *);
void OnReconnectSuccess(void *data, calldata_t *);

struct Runtime {
	~Runtime()
	{
		/* Ne plus recevoir de signaux une fois cet état détruit */
		if (output) {
			signal_handler_t *sh = obs_output_get_signal_handler(output);
			signal_handler_disconnect(sh, "start", OnStart, this);
			signal_handler_disconnect(sh, "stop", OnStop, this);
			signal_handler_disconnect(sh, "reconnect", OnReconnect, this);
			signal_handler_disconnect(sh, "reconnect_success", OnReconnectSuccess, this);
			if (obs_output_active(output)) {
				obs_output_force_stop(output);
			}
		}
	}

	OBSOutput output;
	OBSService service;
	std::atomic<int> state{(int)State::Off};
	std::atomic<int> stopCode{0};
	uint64_t lastBytes = 0;
	uint64_t lastTime = 0;
	int kbps = 0;
	QString error;
};

void OnStart(void *data, calldata_t *)
{
	static_cast<Runtime *>(data)->state = (int)State::Live;
}

void OnStop(void *data, calldata_t *cd)
{
	Runtime *rt = static_cast<Runtime *>(data);
	const int code = (int)calldata_int(cd, "code");
	rt->stopCode = code;
	rt->state = code == OBS_OUTPUT_SUCCESS ? (int)State::Off : (int)State::Error;
}

void OnReconnect(void *data, calldata_t *)
{
	static_cast<Runtime *>(data)->state = (int)State::Reconnecting;
}

void OnReconnectSuccess(void *data, calldata_t *)
{
	static_cast<Runtime *>(data)->state = (int)State::Live;
}

/* ------------------------------------------------------------------------ */
/* Fenêtre d'ajout / modification d'une destination                         */

class DestinationDialog : public QDialog {
public:
	DestinationDialog(QWidget *parent, const Destination &d) : QDialog(parent)
	{
		setWindowTitle(T("Dialog.Title"));
		setMinimumWidth(460);
		QVBoxLayout *layout = new QVBoxLayout(this);
		QFormLayout *form = new QFormLayout;

		platform = new QComboBox(this);
		for (const Platform &p : PLATFORMS) {
			platform->addItem(T(p.nameKey), p.key);
		}
		name = new QLineEdit(d.name, this);
		server = new QLineEdit(d.server, this);
		key = new QLineEdit(d.key, this);
		key->setEchoMode(QLineEdit::Password);
		QCheckBox *showKey = new QCheckBox(T("Dialog.ShowKey"), this);
		help = new QLabel(this);
		help->setWordWrap(true);
		help->setTextFormat(Qt::PlainText);

		form->addRow(T("Dialog.Platform"), platform);
		form->addRow(T("Dialog.Name"), name);
		form->addRow(T("Dialog.Server"), server);
		form->addRow(T("Dialog.Key"), key);
		form->addRow(QString(), showKey);
		layout->addLayout(form);
		layout->addWidget(help);

		QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
		layout->addWidget(buttons);

		const int index = platform->findData(d.platform.isEmpty() ? QString("youtube") : d.platform);
		platform->setCurrentIndex(index < 0 ? 0 : index);
		platformChanged(d.server.isEmpty());

		connect(platform, &QComboBox::currentIndexChanged, this, [this](int) { platformChanged(true); });
		connect(showKey, &QCheckBox::toggled, this, [this](bool on) {
			key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
		});
		connect(buttons, &QDialogButtonBox::accepted, this, [this] {
			if (server->text().trimmed().isEmpty() || key->text().trimmed().isEmpty()) {
				QMessageBox::warning(this, T("Dialog.Title"), T("Dialog.Missing"));
				return;
			}
			accept();
		});
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	}

	Destination destination() const
	{
		Destination d;
		d.platform = platform->currentData().toString();
		d.name = name->text().trimmed().isEmpty() ? platform->currentText() : name->text().trimmed();
		d.server = server->text().trimmed();
		d.key = key->text().trimmed();
		return d;
	}

private:
	void platformChanged(bool fillServer)
	{
		const Platform *p = FindPlatform(platform->currentData().toString());
		if (fillServer) {
			server->setText(QString::fromUtf8(p->server));
		}
		const std::string helpKey = std::string("Help.") + p->key;
		help->setText(T(helpKey.c_str()));
	}

	QComboBox *platform = nullptr;
	QLineEdit *name = nullptr;
	QLineEdit *server = nullptr;
	QLineEdit *key = nullptr;
	QLabel *help = nullptr;
};

/* ------------------------------------------------------------------------ */
/* Panneau « Multidiffusion »                                               */

class MultiDock;
QPointer<MultiDock> dockWidget;

class MultiDock : public QWidget {
public:
	explicit MultiDock(QWidget *parent) : QWidget(parent)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(6, 6, 6, 6);

		QLabel *intro = new QLabel(T("Intro"), this);
		intro->setWordWrap(true);
		layout->addWidget(intro);

		listLayout = new QVBoxLayout;
		layout->addLayout(listLayout);

		QPushButton *add = new QPushButton(T("Add"), this);
		add->setMinimumHeight(32);
		layout->addWidget(add);

		status = new QLabel(this);
		status->setWordWrap(true);
		layout->addWidget(status);

		QLabel *tips = new QLabel(T("Tips"), this);
		tips->setWordWrap(true);
		QFont small = tips->font();
		small.setPointSizeF(small.pointSizeF() * 0.9);
		tips->setFont(small);
		layout->addWidget(tips);
		layout->addStretch(1);

		connect(add, &QPushButton::clicked, this, [this] {
			DestinationDialog dialog(this, Destination{});
			if (dialog.exec() == QDialog::Accepted) {
				destinations.push_back(dialog.destination());
				runtimes.push_back(std::make_unique<Runtime>());
				saveDestinations();
				rebuildRows();
			}
		});

		timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, &MultiDock::refreshStatus);
		timer->start(1000);

		loadDestinations();
		rebuildRows();
	}

	/* Le direct principal démarre : les destinations cochées partent avec lui */
	void mainStarted()
	{
		int started = 0;
		for (size_t i = 0; i < destinations.size(); i++) {
			if (destinations[i].enabled && start(i)) {
				started++;
			}
		}
		if (started) {
			status->setText(T("Status.Started").arg(started));
		}
	}

	void stopAll()
	{
		for (size_t i = 0; i < runtimes.size(); i++) {
			stop(i);
		}
	}

private:
	/* Démarre une destination en réutilisant les encodeurs du direct principal */
	bool start(size_t i)
	{
		Runtime *rt = runtimes[i].get();
		if (rt->output && obs_output_active(rt->output)) {
			return true;
		}
		OBSOutputAutoRelease main = obs_frontend_get_streaming_output();
		if (!main || !obs_output_active(main)) {
			status->setText(T("Error.MainNotLive"));
			return false;
		}
		obs_encoder_t *video = obs_output_get_video_encoder(main);
		obs_encoder_t *audio = obs_output_get_audio_encoder(main, 0);
		if (!video || !audio) {
			status->setText(T("Error.NoEncoder"));
			return false;
		}

		const Destination &d = destinations[i];
		OBSDataAutoRelease serviceSettings = obs_data_create();
		obs_data_set_string(serviceSettings, "server", d.server.toUtf8().constData());
		obs_data_set_string(serviceSettings, "key", d.key.toUtf8().constData());
		const QString id = QString("amal-multi-%1").arg(i);
		rt->service = obs_service_create_private("rtmp_custom", id.toUtf8().constData(), serviceSettings);
		obs_service_release(rt->service);

		disconnectSignals(rt);
		rt->output = obs_output_create("rtmp_output", id.toUtf8().constData(), nullptr, nullptr);
		obs_output_release(rt->output);
		if (!rt->output || !rt->service) {
			status->setText(T("Error.Create"));
			return false;
		}
		obs_output_set_service(rt->output, rt->service);
		obs_output_set_video_encoder(rt->output, video);
		obs_output_set_audio_encoder(rt->output, audio, 0);
		obs_output_set_reconnect_settings(rt->output, 20, 10);

		signal_handler_t *sh = obs_output_get_signal_handler(rt->output);
		signal_handler_connect(sh, "start", OnStart, rt);
		signal_handler_connect(sh, "stop", OnStop, rt);
		signal_handler_connect(sh, "reconnect", OnReconnect, rt);
		signal_handler_connect(sh, "reconnect_success", OnReconnectSuccess, rt);

		rt->state = (int)State::Connecting;
		rt->lastBytes = 0;
		rt->lastTime = os_gettime_ns();
		rt->kbps = 0;
		rt->error.clear();
		if (!obs_output_start(rt->output)) {
			const char *err = obs_output_get_last_error(rt->output);
			rt->error = QString::fromUtf8(err ? err : "");
			rt->state = (int)State::Error;
			blog(LOG_WARNING, "[amal-multidiffusion] Échec du démarrage vers %s : %s",
			     d.name.toUtf8().constData(), err ? err : "?");
			return false;
		}
		blog(LOG_INFO, "[amal-multidiffusion] Diffusion vers %s démarrée", d.name.toUtf8().constData());
		return true;
	}

	void stop(size_t i)
	{
		Runtime *rt = runtimes[i].get();
		if (rt->output && obs_output_active(rt->output)) {
			obs_output_stop(rt->output);
		}
	}

	static void disconnectSignals(Runtime *rt)
	{
		if (!rt->output) {
			return;
		}
		signal_handler_t *sh = obs_output_get_signal_handler(rt->output);
		signal_handler_disconnect(sh, "start", OnStart, rt);
		signal_handler_disconnect(sh, "stop", OnStop, rt);
		signal_handler_disconnect(sh, "reconnect", OnReconnect, rt);
		signal_handler_disconnect(sh, "reconnect_success", OnReconnectSuccess, rt);
	}

	/* ---------- Liste des destinations ---------- */

	struct Row {
		QCheckBox *enabled;
		QLabel *state;
		QPushButton *toggle;
	};

	void rebuildRows()
	{
		while (QLayoutItem *item = listLayout->takeAt(0)) {
			delete item->widget();
			delete item;
		}
		rows.clear();

		if (destinations.empty()) {
			QLabel *empty = new QLabel(T("Empty"), this);
			empty->setWordWrap(true);
			listLayout->addWidget(empty);
			return;
		}

		for (size_t i = 0; i < destinations.size(); i++) {
			QWidget *row = new QWidget(this);
			QVBoxLayout *v = new QVBoxLayout(row);
			v->setContentsMargins(4, 4, 4, 4);
			row->setObjectName("amalDestination");
			row->setStyleSheet("#amalDestination { border: 1px solid rgba(255,255,255,0.15); border-radius: 6px; }");

			QHBoxLayout *top = new QHBoxLayout;
			QCheckBox *enabled = new QCheckBox(destinations[i].name, row);
			enabled->setChecked(destinations[i].enabled);
			enabled->setToolTip(T("Enabled.Help"));
			QLabel *state = new QLabel(row);
			top->addWidget(enabled, 1);
			top->addWidget(state);
			v->addLayout(top);

			QHBoxLayout *buttons = new QHBoxLayout;
			QPushButton *toggle = new QPushButton(T("Start"), row);
			QPushButton *edit = new QPushButton(T("Edit"), row);
			QPushButton *remove = new QPushButton(T("Remove"), row);
			buttons->addWidget(toggle, 1);
			buttons->addWidget(edit);
			buttons->addWidget(remove);
			v->addLayout(buttons);
			listLayout->addWidget(row);

			connect(enabled, &QCheckBox::toggled, this, [this, i](bool on) {
				destinations[i].enabled = on;
				saveDestinations();
			});
			connect(toggle, &QPushButton::clicked, this, [this, i] {
				Runtime *rt = runtimes[i].get();
				if (rt->output && obs_output_active(rt->output)) {
					stop(i);
				} else {
					start(i);
				}
				refreshStatus();
			});
			connect(edit, &QPushButton::clicked, this, [this, i] {
				if (runtimes[i]->output && obs_output_active(runtimes[i]->output)) {
					status->setText(T("Error.StopFirst"));
					return;
				}
				DestinationDialog dialog(this, destinations[i]);
				if (dialog.exec() == QDialog::Accepted) {
					const bool enabledBefore = destinations[i].enabled;
					destinations[i] = dialog.destination();
					destinations[i].enabled = enabledBefore;
					saveDestinations();
					QTimer::singleShot(0, this, [this] { rebuildRows(); });
				}
			});
			connect(remove, &QPushButton::clicked, this, [this, i] {
				if (runtimes[i]->output && obs_output_active(runtimes[i]->output)) {
					status->setText(T("Error.StopFirst"));
					return;
				}
				if (QMessageBox::question(this, T("Remove"), T("Remove.Confirm").arg(destinations[i].name)) !=
				    QMessageBox::Yes) {
					return;
				}
				disconnectSignals(runtimes[i].get());
				destinations.erase(destinations.begin() + (std::ptrdiff_t)i);
				runtimes.erase(runtimes.begin() + (std::ptrdiff_t)i);
				saveDestinations();
				QTimer::singleShot(0, this, [this] { rebuildRows(); });
			});
			rows.push_back(Row{enabled, state, toggle});
		}
		refreshStatus();
	}

	void refreshStatus()
	{
		const uint64_t now = os_gettime_ns();
		for (size_t i = 0; i < rows.size() && i < runtimes.size(); i++) {
			Runtime *rt = runtimes[i].get();
			const bool active = rt->output && obs_output_active(rt->output);
			State state = (State)rt->state.load();
			if (!active && (state == State::Live || state == State::Connecting)) {
				state = State::Off;
			}

			QString text;
			QString color;
			switch (state) {
			case State::Live: {
				const uint64_t bytes = obs_output_get_total_bytes(rt->output);
				if (rt->lastTime && now > rt->lastTime && bytes >= rt->lastBytes) {
					const double seconds = (double)(now - rt->lastTime) / 1e9;
					rt->kbps = (int)((double)(bytes - rt->lastBytes) * 8.0 / 1000.0 / seconds);
				}
				rt->lastBytes = bytes;
				rt->lastTime = now;
				text = T("State.Live").arg(rt->kbps);
				color = "#2ECC71";
				break;
			}
			case State::Connecting:
				text = T("State.Connecting");
				color = "#F1C40F";
				break;
			case State::Reconnecting:
				text = T("State.Reconnecting");
				color = "#F39C12";
				break;
			case State::Error:
				text = ErrorText(rt);
				color = "#E74C3C";
				break;
			case State::Off:
				text = T("State.Off");
				color = "#95A5A6";
				break;
			}
			rows[i].state->setText(QString("<span style='color:%1'>\xE2\x97\x8F</span> %2").arg(color, text));
			rows[i].state->setToolTip(rt->error);
			rows[i].toggle->setText(active ? T("Stop") : T("Start"));
		}
	}

	static QString ErrorText(Runtime *rt)
	{
		switch (rt->stopCode.load()) {
		case OBS_OUTPUT_BAD_PATH:
			return T("State.BadServer");
		case OBS_OUTPUT_INVALID_STREAM:
			return T("State.BadKey");
		case OBS_OUTPUT_CONNECT_FAILED:
			return T("State.ConnectFailed");
		case OBS_OUTPUT_DISCONNECTED:
			return T("State.Disconnected");
		default:
			return T("State.Error");
		}
	}

	/* ---------- Enregistrement des destinations (cet ordinateur seulement) ---------- */

	static QString ConfigPath()
	{
		char *path = obs_module_config_path("destinations.json");
		QString result = QString::fromUtf8(path ? path : "");
		bfree(path);
		return result;
	}

	void loadDestinations()
	{
		destinations.clear();
		runtimes.clear();
		QFile file(ConfigPath());
		if (file.open(QIODevice::ReadOnly)) {
			for (const QJsonValue &v : QJsonDocument::fromJson(file.readAll()).array()) {
				destinations.push_back(Destination::fromJson(v.toObject()));
				runtimes.push_back(std::make_unique<Runtime>());
			}
		}
	}

	void saveDestinations()
	{
		char *dir = obs_module_config_path("");
		if (dir) {
			os_mkdirs(dir);
			bfree(dir);
		}
		QJsonArray list;
		for (const Destination &d : destinations) {
			list.append(d.toJson());
		}
		QSaveFile file(ConfigPath());
		if (file.open(QIODevice::WriteOnly)) {
			file.write(QJsonDocument(list).toJson());
			file.commit();
		}
	}

	QVBoxLayout *listLayout = nullptr;
	QLabel *status = nullptr;
	QTimer *timer = nullptr;
	std::vector<Destination> destinations;
	std::vector<std::unique_ptr<Runtime>> runtimes;
	std::vector<Row> rows;
};

/* ------------------------------------------------------------------------ */

void ShowDockOnFirstRun()
{
	config_t *config = obs_frontend_get_app_config();
	if (!config || config_get_bool(config, CONFIG_SECTION, "DockPlaced")) {
		return;
	}
	QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	QDockWidget *dock = nullptr;
	for (QWidget *w = dockWidget; w && !dock; w = w->parentWidget()) {
		dock = qobject_cast<QDockWidget *>(w);
	}
	if (!main || !dock) {
		return;
	}
	dock->setFloating(false);
	main->addDockWidget(Qt::RightDockWidgetArea, dock);
	dock->show();
	config_set_bool(config, CONFIG_SECTION, "DockPlaced", true);
	config_save_safe(config, "tmp", nullptr);
}

void FrontendEvent(enum obs_frontend_event event, void *)
{
	if (!dockWidget) {
		return;
	}
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		dockWidget->mainStarted();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
	case OBS_FRONTEND_EVENT_EXIT:
		dockWidget->stopAll();
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		ShowDockOnFirstRun();
		break;
	default:
		break;
	}
}

} // namespace

bool obs_module_load(void)
{
	QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	QScrollArea *scroll = new QScrollArea(main);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	dockWidget = new MultiDock(scroll);
	scroll->setWidget(dockWidget);
	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), scroll)) {
		delete scroll;
		return false;
	}
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	blog(LOG_INFO, "[amal-multidiffusion] Module Multidiffusion chargé");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
}
