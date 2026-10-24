/******************************************************************************
    Amal Studio - module « Caméra téléphone »
    Copyright (C) 2026 Amal Ben Aly

    Problème résolu : avec les applications « webcam pour téléphone », seul
    le micro interne du téléphone est transmis. Ici, le téléphone envoie
    image ET son en SRT (application gratuite Larix Broadcaster), y compris
    le son d'un micro branché au téléphone. Amal Studio écoute le flux sur
    le réseau local : aucun câble, aucun capteur de son supplémentaire.

    Un QR code configure Larix automatiquement (format « Larix Grove »).

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/config-file.h>

#include <qrcodegen.hpp>

#include <QComboBox>
#include <QDockWidget>
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QMainWindow>
#include <QNetworkInterface>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cstring>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-telephone", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : caméra de téléphone avec son (SRT)";
}

namespace {

constexpr const char *DOCK_ID = "amal-telephone";
constexpr const char *CONFIG_SECTION = "AmalTelephone";
constexpr int FIRST_PORT = 9001;
constexpr int MAX_PHONES = 3;
constexpr int LATENCY_MS = 200;

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* Réseau local : 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16 */
bool IsPrivateIPv4(const QHostAddress &ip)
{
	const quint32 a = ip.toIPv4Address();
	return (a >> 24) == 10 || (a >> 20) == ((172u << 4) | 1u) || (a >> 16) == ((192u << 8) | 168u);
}

/* Adresses IPv4 privées de l'ordinateur (Wi-Fi, Ethernet, partage de connexion) */
QStringList LocalAddresses()
{
	QStringList wifi;
	QStringList others;
	for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
		const auto flags = iface.flags();
		if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
		    (flags & QNetworkInterface::IsLoopBack)) {
			continue;
		}
		const QString name = iface.humanReadableName().toLower();
		if (name.contains("virtual") || name.contains("vmware") || name.contains("vbox") ||
		    name.contains("hyper-v") || name.contains("vethernet") || name.contains("bluetooth")) {
			continue;
		}
		for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
			const QHostAddress ip = entry.ip();
			if (ip.protocol() != QAbstractSocket::IPv4Protocol || !IsPrivateIPv4(ip)) {
				continue;
			}
			const bool wireless = iface.type() == QNetworkInterface::Wifi || name.contains("wi-fi") ||
					      name.contains("wlan");
			(wireless ? wifi : others).append(ip.toString());
		}
	}
	return wifi + others;
}

QPixmap DrawQr(const QString &text, int size)
{
	QPixmap map(size, size);
	map.fill(Qt::white);
	QPainter painter(&map);
	const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
	const int modules = qr.getSize() > 0 ? qr.getSize() : 1;
	const double scale = (double)size / (double)(modules + 4);
	painter.setPen(Qt::NoPen);
	painter.setBrush(Qt::black);
	for (int y = 0; y < modules; y++) {
		for (int x = 0; x < modules; x++) {
			if (qr.getModule(x, y)) {
				painter.drawRect(QRectF((x + 2) * scale, (y + 2) * scale, scale + 0.5, scale + 0.5));
			}
		}
	}
	return map;
}

/* ------------------------------------------------------------------------ */
/* Panneau « Caméra téléphone »                                             */

class PhoneDock;
QPointer<PhoneDock> dockWidget;

class PhoneDock : public QWidget {
public:
	explicit PhoneDock(QWidget *parent) : QWidget(parent)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(8, 8, 8, 8);

		QLabel *help = new QLabel(T("Dock.Help"), this);
		help->setWordWrap(true);
		layout->addWidget(help);

		QHBoxLayout *row = new QHBoxLayout;
		row->addWidget(new QLabel(T("Phone"), this));
		phoneCombo = new QComboBox(this);
		for (int i = 1; i <= MAX_PHONES; i++) {
			phoneCombo->addItem(T("Phone.N").arg(i), i);
		}
		row->addWidget(phoneCombo, 1);
		layout->addLayout(row);

		QHBoxLayout *ipRow = new QHBoxLayout;
		ipRow->addWidget(new QLabel(T("Address"), this));
		ipCombo = new QComboBox(this);
		ipRow->addWidget(ipCombo, 1);
		QPushButton *refresh = new QPushButton(QString::fromUtf8("\xE2\x86\xBB"), this);
		refresh->setToolTip(T("Address.Refresh"));
		refresh->setFixedWidth(36);
		ipRow->addWidget(refresh);
		layout->addLayout(ipRow);

		qrLabel = new QLabel(this);
		qrLabel->setAlignment(Qt::AlignCenter);
		qrLabel->setMinimumSize(220, 220);
		layout->addWidget(qrLabel);

		urlLabel = new QLabel(this);
		urlLabel->setAlignment(Qt::AlignCenter);
		urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
		QFont mono = urlLabel->font();
		mono.setBold(true);
		urlLabel->setFont(mono);
		layout->addWidget(urlLabel);

		QPushButton *copy = new QPushButton(T("Copy"), this);
		layout->addWidget(copy);

		QLabel *steps = new QLabel(T("Steps"), this);
		steps->setWordWrap(true);
		steps->setTextFormat(Qt::RichText);
		steps->setOpenExternalLinks(true);
		layout->addWidget(steps);

		QPushButton *add = new QPushButton(T("AddToScene"), this);
		add->setMinimumHeight(40);
		QFont bold = add->font();
		bold.setBold(true);
		add->setFont(bold);
		layout->addWidget(add);

		state = new QLabel(this);
		state->setWordWrap(true);
		QFont stateFont = state->font();
		stateFont.setBold(true);
		state->setFont(stateFont);
		layout->addWidget(state);
		status = new QLabel(this);
		status->setWordWrap(true);
		layout->addWidget(status);
		layout->addStretch(1);

		connect(phoneCombo, &QComboBox::currentIndexChanged, this, [this](int) { updateInfo(); });
		connect(ipCombo, &QComboBox::currentIndexChanged, this, [this](int) { updateInfo(); });
		connect(refresh, &QPushButton::clicked, this, &PhoneDock::refreshAddresses);
		connect(copy, &QPushButton::clicked, this, [this] {
			QGuiApplication::clipboard()->setText(phoneUrl());
			status->setText(T("Copied"));
		});
		connect(add, &QPushButton::clicked, this, &PhoneDock::addToScene);

		timer = new QTimer(this);
		timer->setInterval(2000);
		connect(timer, &QTimer::timeout, this, &PhoneDock::updateStatus);
		timer->start();

		refreshAddresses();
	}

private:
	int phone() const { return phoneCombo->currentData().toInt(); }
	int port() const { return FIRST_PORT + phone() - 1; }
	QString sourceName() const { return T("Source.Name").arg(phone()); }

	/* Adresse que le téléphone appelle */
	QString phoneUrl() const
	{
		const QString ip = ipCombo->currentText();
		return QString("srt://%1:%2").arg(ip.isEmpty() ? QStringLiteral("?") : ip).arg(port());
	}

	/* Lien « Larix Grove » : le téléphone importe la connexion en scannant le QR code */
	QString larixLink() const
	{
		const QString url = QString::fromLatin1(QUrl::toPercentEncoding(phoneUrl()));
		const QString name =
			QString::fromLatin1(QUrl::toPercentEncoding(QString("Amal Studio %1").arg(phone())));
		return QString("larix://set/v1?conn[][url]=%1&conn[][name]=%2&conn[][mode]=va&conn[][srtmode]=c"
			       "&conn[][srtlatency]=%3&conn[][overwrite]=on&conn[][active]=on")
			.arg(url, name)
			.arg(LATENCY_MS);
	}

	void refreshAddresses()
	{
		const QString current = ipCombo->currentText();
		ipCombo->blockSignals(true);
		ipCombo->clear();
		ipCombo->addItems(LocalAddresses());
		const int index = ipCombo->findText(current);
		ipCombo->setCurrentIndex(index >= 0 ? index : 0);
		ipCombo->blockSignals(false);
		updateInfo();
	}

	void updateInfo()
	{
		if (ipCombo->count() == 0) {
			qrLabel->setPixmap(QPixmap());
			qrLabel->setText(T("Error.NoNetwork"));
			urlLabel->clear();
			return;
		}
		qrLabel->setPixmap(DrawQr(larixLink(), 220));
		urlLabel->setText(phoneUrl());
		updateStatus();
	}

	void addToScene()
	{
		const QString name = sourceName();
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_bool(settings, "is_local_file", false);
		obs_data_set_string(settings, "input",
				    QString("srt://0.0.0.0:%1?mode=listener&latency=%2")
					    .arg(port())
					    .arg(LATENCY_MS * 1000)
					    .toUtf8()
					    .constData());
		obs_data_set_string(settings, "input_format", "mpegts");
		obs_data_set_int(settings, "reconnect_delay_sec", 1);
		obs_data_set_int(settings, "buffering_mb", 1);
		obs_data_set_bool(settings, "hw_decode", true);
		obs_data_set_bool(settings, "restart_on_activate", false);
		obs_data_set_bool(settings, "close_when_inactive", false);
		obs_data_set_bool(settings, "clear_on_media_end", false);

		OBSSourceAutoRelease source = obs_get_source_by_name(name.toUtf8().constData());
		if (source) {
			const char *id = obs_source_get_unversioned_id(source);
			if (!id || strcmp(id, "ffmpeg_source") != 0) {
				status->setText(T("Error.NameTaken").arg(name));
				return;
			}
			obs_source_update(source, settings);
		} else {
			source = obs_source_create("ffmpeg_source", name.toUtf8().constData(), settings, nullptr);
		}
		if (!source) {
			status->setText(T("Error.Create"));
			return;
		}

		OBSSourceAutoRelease sceneSource = obs_frontend_preview_program_mode_active()
							   ? obs_frontend_get_current_preview_scene()
							   : obs_frontend_get_current_scene();
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (scene && !obs_scene_find_source(scene, name.toUtf8().constData())) {
			obs_video_info ovi = {};
			obs_sceneitem_t *item = obs_scene_add(scene, source);
			if (item && obs_get_video_info(&ovi)) {
				vec2 bounds;
				vec2_set(&bounds, (float)ovi.base_width, (float)ovi.base_height);
				obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
				obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
				obs_sceneitem_set_bounds(item, &bounds);
			}
		}
		status->setText(T("Status.Added").arg(name));
		updateStatus();
	}

	/* État de réception : la source a-t-elle une image ? */
	void updateStatus()
	{
		OBSSourceAutoRelease source = obs_get_source_by_name(sourceName().toUtf8().constData());
		if (!source) {
			state->setText(T("State.NotCreated"));
			return;
		}
		const uint32_t w = obs_source_get_width(source);
		const uint32_t h = obs_source_get_height(source);
		const obs_media_state mediaState = obs_source_media_get_state(source);
		if (w && h && mediaState == OBS_MEDIA_STATE_PLAYING) {
			state->setText(T("State.Receiving").arg(w).arg(h));
		} else {
			state->setText(T("State.Waiting").arg(port()));
		}
	}

	QComboBox *phoneCombo = nullptr;
	QComboBox *ipCombo = nullptr;
	QLabel *qrLabel = nullptr;
	QLabel *urlLabel = nullptr;
	QLabel *state = nullptr;
	QLabel *status = nullptr;
	QTimer *timer = nullptr;
};

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
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		ShowDockOnFirstRun();
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
	dockWidget = new PhoneDock(scroll);
	scroll->setWidget(dockWidget);
	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), scroll)) {
		delete scroll;
		return false;
	}
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	blog(LOG_INFO, "[amal-telephone] Module Caméra téléphone chargé");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
}
