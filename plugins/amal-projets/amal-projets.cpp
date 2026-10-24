/******************************************************************************
    Amal Studio - module « Projets »
    Copyright (C) 2026 Amal Ben Aly

    Chaque projet Amal Studio correspond à une collection de scènes du moteur
    (OBS Studio) et porte une fiche d'événement : titre, date, lieu,
    intervenant et notes. La fiche est enregistrée dans le fichier de la
    collection elle-même : elle se sauvegarde, se copie et se restaure avec
    les scènes.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include <obs-module.h>
#include <obs.hpp>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/util.hpp>

#include <QCheckBox>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-projets", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : projets et événements";
}

namespace {

constexpr const char *DOCK_ID = "amal-projets";
constexpr const char *SAVE_KEY = "amal_projet";
constexpr const char *CONFIG_SECTION = "AmalProjets";
constexpr int AUTOSAVE_INTERVAL_MS = 60 * 1000;

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* Fiche d'événement d'un projet. */
struct ProjectInfo {
	QString title;
	QString date; /* format ISO AAAA-MM-JJ */
	QString place;
	QString speaker;
	QString notes;

	bool isEmpty() const { return title.isEmpty() && date.isEmpty() && place.isEmpty() && speaker.isEmpty(); }
};

ProjectInfo currentInfo;
bool infoLoaded = false;
/* Vrai si le moteur a transmis les données du projet depuis le dernier changement de projet. */
bool loadedSinceChanging = false;

class ProjectsDock;
QPointer<ProjectsDock> dockWidget;

/* ------------------------------------------------------------------------ */
/* Fenêtre de saisie de la fiche                                           */

class ProjectDialog : public QDialog {
public:
	ProjectDialog(QWidget *parent, const ProjectInfo &info, bool creating) : QDialog(parent)
	{
		setWindowTitle(creating ? T("Dialog.NewTitle") : T("Dialog.EditTitle"));
		setMinimumWidth(460);

		titleEdit = new QLineEdit(info.title, this);
		titleEdit->setPlaceholderText(T("Field.TitlePlaceholder"));

		dateEdit = new QDateEdit(this);
		dateEdit->setCalendarPopup(true);
		dateEdit->setDisplayFormat("dd/MM/yyyy");
		QDate date = QDate::fromString(info.date, Qt::ISODate);
		dateEdit->setDate(date.isValid() ? date : QDate::currentDate());

		placeEdit = new QLineEdit(info.place, this);
		speakerEdit = new QLineEdit(info.speaker, this);
		speakerEdit->setPlaceholderText(T("Field.SpeakerPlaceholder"));

		notesEdit = new QPlainTextEdit(info.notes, this);
		notesEdit->setFixedHeight(80);

		QFormLayout *form = new QFormLayout;
		form->addRow(T("Field.Title"), titleEdit);
		form->addRow(T("Field.Date"), dateEdit);
		form->addRow(T("Field.Place"), placeEdit);
		form->addRow(T("Field.Speaker"), speakerEdit);
		form->addRow(T("Field.Notes"), notesEdit);

		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->addLayout(form);

		if (creating) {
			templateCheck = new QCheckBox(T("Field.ConferenceTemplate"), this);
			templateCheck->setChecked(true);
			layout->addWidget(templateCheck);
		}

		QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
		buttons->button(QDialogButtonBox::Ok)->setText(creating ? T("Button.Create") : T("Button.Save"));
		buttons->button(QDialogButtonBox::Cancel)->setText(T("Button.Cancel"));
		connect(buttons, &QDialogButtonBox::accepted, this, &ProjectDialog::validateAndAccept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		layout->addWidget(buttons);
	}

	ProjectInfo info() const
	{
		ProjectInfo result;
		result.title = titleEdit->text().trimmed();
		result.date = dateEdit->date().toString(Qt::ISODate);
		result.place = placeEdit->text().trimmed();
		result.speaker = speakerEdit->text().trimmed();
		result.notes = notesEdit->toPlainText().trimmed();
		return result;
	}

	bool useConferenceTemplate() const { return templateCheck && templateCheck->isChecked(); }

private:
	void validateAndAccept()
	{
		if (titleEdit->text().trimmed().isEmpty()) {
			QMessageBox::warning(this, windowTitle(), T("Error.TitleRequired"));
			titleEdit->setFocus();
			return;
		}
		accept();
	}

	QLineEdit *titleEdit = nullptr;
	QDateEdit *dateEdit = nullptr;
	QLineEdit *placeEdit = nullptr;
	QLineEdit *speakerEdit = nullptr;
	QPlainTextEdit *notesEdit = nullptr;
	QCheckBox *templateCheck = nullptr;
};

/* ------------------------------------------------------------------------ */
/* Outils                                                                   */

std::vector<std::string> GetCollections()
{
	std::vector<std::string> names;
	char **list = obs_frontend_get_scene_collections();
	for (char **item = list; item && *item; item++) {
		names.emplace_back(*item);
	}
	bfree(list);
	return names;
}

std::string GetCurrentCollection()
{
	BPtr<char> name = obs_frontend_get_current_scene_collection();
	return name ? std::string(name.Get()) : std::string();
}

bool OutputsActive()
{
	return obs_frontend_streaming_active() || obs_frontend_recording_active() ||
	       obs_frontend_replay_buffer_active() || obs_frontend_virtualcam_active();
}

std::string UniqueCollectionName(const QString &title)
{
	std::vector<std::string> existing = GetCollections();
	auto exists = [&existing](const std::string &name) {
		for (const std::string &item : existing) {
			if (QString::fromStdString(item).compare(QString::fromStdString(name), Qt::CaseInsensitive) ==
			    0) {
				return true;
			}
		}
		return false;
	};

	std::string base = title.toStdString();
	std::string name = base;
	for (int i = 2; exists(name); i++) {
		name = base + " (" + std::to_string(i) + ")";
	}
	return name;
}

const char *CameraSourceId()
{
#if defined(_WIN32)
	return "dshow_input";
#elif defined(__APPLE__)
	return "macos-avcapture";
#else
	return "v4l2_input";
#endif
}

const char *CameraDeviceProperty()
{
#if defined(_WIN32)
	return "video_device_id";
#elif defined(__APPLE__)
	return "device";
#else
	return "device_id";
#endif
}

const char *TextSourceId()
{
#if defined(_WIN32)
	return obs_get_latest_input_type_id("text_gdiplus");
#else
	return obs_get_latest_input_type_id("text_ft2_source");
#endif
}

/* Choisit la première caméra disponible pour la source vidéo. */
void SelectFirstCamera(obs_source_t *camera)
{
	obs_properties_t *props = obs_source_properties(camera);
	if (!props) {
		return;
	}

	obs_property_t *list = obs_properties_get(props, CameraDeviceProperty());
	if (list && obs_property_list_format(list) == OBS_COMBO_FORMAT_STRING) {
		size_t count = obs_property_list_item_count(list);
		for (size_t i = 0; i < count; i++) {
			const char *value = obs_property_list_item_string(list, i);
			if (value && *value && !obs_property_list_item_disabled(list, i)) {
				OBSDataAutoRelease settings = obs_data_create();
				obs_data_set_string(settings, CameraDeviceProperty(), value);
				obs_source_update(camera, settings);
				blog(LOG_INFO, "[amal-projets] Caméra sélectionnée : %s",
				     obs_property_list_item_name(list, i));
				break;
			}
		}
	}

	obs_properties_destroy(props);
}

/* Prépare la scène « Conférence » : webcam plein écran et titre de l'événement. */
void BuildConferenceScene(const ProjectInfo &info)
{
	OBSSourceAutoRelease sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		blog(LOG_WARNING, "[amal-projets] Aucune scène courante : modèle Conférence non appliqué");
		return;
	}

	obs_source_set_name(sceneSource, obs_module_text("Scene.Conference"));

	obs_video_info ovi = {};
	obs_get_video_info(&ovi);
	const float width = ovi.base_width ? (float)ovi.base_width : 1920.0f;
	const float height = ovi.base_height ? (float)ovi.base_height : 1080.0f;

	/* Webcam, adaptée à toute l'image */
	OBSSourceAutoRelease camera = obs_source_create(CameraSourceId(), obs_module_text("Source.Webcam"), nullptr,
							 nullptr);
	if (camera) {
		SelectFirstCamera(camera);
		obs_sceneitem_t *item = obs_scene_add(scene, camera);
		if (item) {
			vec2 pos;
			vec2 bounds;
			vec2_set(&pos, 0.0f, 0.0f);
			vec2_set(&bounds, width, height);
			obs_sceneitem_set_pos(item, &pos);
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
			obs_sceneitem_set_bounds(item, &bounds);
		}
	} else {
		blog(LOG_WARNING, "[amal-projets] Source caméra '%s' indisponible", CameraSourceId());
	}

	/* Titre de l'événement, en bas à gauche */
	const char *textId = TextSourceId();
	if (!textId) {
		blog(LOG_WARNING, "[amal-projets] Source texte indisponible");
		return;
	}

	QString text = info.title;
	if (!info.speaker.isEmpty()) {
		text += "\n" + info.speaker;
	}

	OBSDataAutoRelease font = obs_data_create();
#if defined(_WIN32)
	obs_data_set_string(font, "face", "Segoe UI");
#else
	obs_data_set_string(font, "face", "Sans Serif");
#endif
	obs_data_set_string(font, "style", "Bold");
	obs_data_set_int(font, "size", 64);
	obs_data_set_int(font, "flags", 1); /* gras */

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "text", text.toUtf8().constData());
	obs_data_set_obj(settings, "font", font);
	obs_data_set_bool(settings, "outline", true);
	obs_data_set_int(settings, "outline_size", 4);
	obs_data_set_int(settings, "outline_color", 0xFF000000);

	OBSSourceAutoRelease title = obs_source_create(textId, obs_module_text("Source.Title"), settings, nullptr);
	if (title) {
		obs_sceneitem_t *item = obs_scene_add(scene, title);
		if (item) {
			vec2 pos;
			vec2_set(&pos, width * 0.04f, height * 0.78f);
			obs_sceneitem_set_pos(item, &pos);
		}
	}
}

void ShowWarning(const QString &text)
{
	QMessageBox::warning(static_cast<QWidget *>(obs_frontend_get_main_window()), T("Dock.Title"), text);
}

/* ------------------------------------------------------------------------ */
/* Panneau « Projets »                                                      */

class ProjectsDock : public QWidget {
public:
	explicit ProjectsDock(QWidget *parent) : QWidget(parent)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(8, 8, 8, 8);

		QLabel *currentHeader = new QLabel(T("Dock.CurrentProject"), this);
		currentHeader->setStyleSheet("font-weight: bold;");
		layout->addWidget(currentHeader);

		currentLabel = new QLabel(this);
		currentLabel->setWordWrap(true);
		currentLabel->setTextFormat(Qt::PlainText);
		layout->addWidget(currentLabel);

		QPushButton *editButton = new QPushButton(T("Dock.EditInfo"), this);
		connect(editButton, &QPushButton::clicked, this, &ProjectsDock::editCurrent);
		layout->addWidget(editButton);

		QLabel *listHeader = new QLabel(T("Dock.AllProjects"), this);
		listHeader->setStyleSheet("font-weight: bold; margin-top: 8px;");
		layout->addWidget(listHeader);

		list = new QListWidget(this);
		list->setMinimumHeight(140);
		connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { openSelected(); });
		layout->addWidget(list, 1);

		QHBoxLayout *buttons = new QHBoxLayout;
		QPushButton *newButton = new QPushButton(T("Dock.New"), this);
		QPushButton *openButton = new QPushButton(T("Dock.Open"), this);
		connect(newButton, &QPushButton::clicked, this, &ProjectsDock::createProject);
		connect(openButton, &QPushButton::clicked, this, &ProjectsDock::openSelected);
		buttons->addWidget(newButton);
		buttons->addWidget(openButton);
		layout->addLayout(buttons);

		saveLabel = new QLabel(this);
		QFont small = saveLabel->font();
		small.setPointSizeF(small.pointSizeF() * 0.9);
		saveLabel->setFont(small);
		layout->addWidget(saveLabel);

		autosaveTimer = new QTimer(this);
		autosaveTimer->setInterval(AUTOSAVE_INTERVAL_MS);
		connect(autosaveTimer, &QTimer::timeout, this, &ProjectsDock::autosave);
		autosaveTimer->start();

		refresh();
	}

	void refresh()
	{
		const std::string current = GetCurrentCollection();

		list->clear();
		for (const std::string &name : GetCollections()) {
			QListWidgetItem *item = new QListWidgetItem(QString::fromStdString(name), list);
			if (name == current) {
				QFont font = item->font();
				font.setBold(true);
				item->setFont(font);
				list->setCurrentItem(item);
			}
		}

		QString text = QString::fromStdString(current);
		if (!currentInfo.isEmpty()) {
			QStringList lines;
			if (!currentInfo.title.isEmpty()) {
				lines << currentInfo.title;
			}
			QDate date = QDate::fromString(currentInfo.date, Qt::ISODate);
			if (date.isValid()) {
				lines << T("Dock.DateLine").arg(date.toString("dd/MM/yyyy"));
			}
			if (!currentInfo.place.isEmpty()) {
				lines << T("Dock.PlaceLine").arg(currentInfo.place);
			}
			if (!currentInfo.speaker.isEmpty()) {
				lines << T("Dock.SpeakerLine").arg(currentInfo.speaker);
			}
			text = lines.join("\n");
		} else {
			text += "\n" + T("Dock.NoInfo");
		}
		currentLabel->setText(text);
	}

	void showSaved()
	{
		saveLabel->setText(T("Dock.SavedAt").arg(QTime::currentTime().toString("HH:mm:ss")));
	}

private:
	void autosave()
	{
		if (!infoLoaded) {
			return;
		}
		obs_frontend_save();
		showSaved();
	}

	void editCurrent()
	{
		ProjectInfo info = currentInfo;
		if (info.title.isEmpty()) {
			info.title = QString::fromStdString(GetCurrentCollection());
		}

		ProjectDialog dialog(this, info, false);
		if (dialog.exec() != QDialog::Accepted) {
			return;
		}

		currentInfo = dialog.info();
		obs_frontend_save();
		showSaved();
		refresh();
	}

	void createProject()
	{
		if (OutputsActive()) {
			ShowWarning(T("Error.OutputsActive"));
			return;
		}

		ProjectDialog dialog(this, ProjectInfo(), true);
		if (dialog.exec() != QDialog::Accepted) {
			return;
		}

		const ProjectInfo info = dialog.info();
		const std::string name = UniqueCollectionName(info.title);

		obs_frontend_save();
		if (!obs_frontend_add_scene_collection(name.c_str())) {
			ShowWarning(T("Error.CreateFailed"));
			return;
		}

		/* La nouvelle collection est maintenant active (chargement synchrone). */
		currentInfo = info;
		infoLoaded = true;

		if (dialog.useConferenceTemplate()) {
			BuildConferenceScene(info);
		}

		obs_frontend_save();
		showSaved();
		refresh();
		blog(LOG_INFO, "[amal-projets] Projet créé : %s", name.c_str());
	}

	void openSelected()
	{
		QListWidgetItem *item = list->currentItem();
		if (!item) {
			return;
		}

		const std::string name = item->text().toStdString();
		if (name == GetCurrentCollection()) {
			return;
		}

		if (OutputsActive()) {
			ShowWarning(T("Error.OutputsActive"));
			return;
		}

		obs_frontend_save();
		obs_frontend_set_current_scene_collection(name.c_str());
	}

	QLabel *currentLabel = nullptr;
	QLabel *saveLabel = nullptr;
	QListWidget *list = nullptr;
	QTimer *autosaveTimer = nullptr;
};

/* ------------------------------------------------------------------------ */
/* Lien avec le moteur                                                      */

void SaveCallback(obs_data_t *saveData, bool saving, void *)
{
	if (saving) {
		if (currentInfo.isEmpty()) {
			return;
		}
		OBSDataAutoRelease obj = obs_data_create();
		obs_data_set_string(obj, "titre", currentInfo.title.toUtf8().constData());
		obs_data_set_string(obj, "date", currentInfo.date.toUtf8().constData());
		obs_data_set_string(obj, "lieu", currentInfo.place.toUtf8().constData());
		obs_data_set_string(obj, "intervenant", currentInfo.speaker.toUtf8().constData());
		obs_data_set_string(obj, "notes", currentInfo.notes.toUtf8().constData());
		obs_data_set_int(obj, "version", 1);
		obs_data_set_obj(saveData, SAVE_KEY, obj);
		return;
	}

	/* Chargement d'un projet */
	currentInfo = ProjectInfo();
	OBSDataAutoRelease obj = obs_data_get_obj(saveData, SAVE_KEY);
	if (obj) {
		currentInfo.title = QString::fromUtf8(obs_data_get_string(obj, "titre"));
		currentInfo.date = QString::fromUtf8(obs_data_get_string(obj, "date"));
		currentInfo.place = QString::fromUtf8(obs_data_get_string(obj, "lieu"));
		currentInfo.speaker = QString::fromUtf8(obs_data_get_string(obj, "intervenant"));
		currentInfo.notes = QString::fromUtf8(obs_data_get_string(obj, "notes"));
	}
	infoLoaded = true;
	loadedSinceChanging = true;
}

/* Au premier lancement, le panneau est placé à droite de la fenêtre et affiché. */
void ShowDockOnFirstRun()
{
	config_t *config = obs_frontend_get_app_config();
	if (!config || config_get_bool(config, CONFIG_SECTION, "DockPlaced")) {
		return;
	}

	QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	/* Le panneau est dans une zone de défilement : on remonte jusqu'au dock */
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
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		ShowDockOnFirstRun();
		infoLoaded = true;
		if (dockWidget) {
			dockWidget->refresh();
		}
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		/* Nouvelle collection vide : aucune donnée transmise, la fiche repart de zéro. */
		if (!loadedSinceChanging) {
			currentInfo = ProjectInfo();
		}
		infoLoaded = true;
		if (dockWidget) {
			dockWidget->refresh();
		}
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_RENAMED:
		if (dockWidget) {
			dockWidget->refresh();
		}
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		infoLoaded = false;
		loadedSinceChanging = false;
		break;
	default:
		break;
	}
}

} // namespace

bool obs_module_load(void)
{
	QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	/* Zone de défilement : quand le panneau est petit, on fait défiler son contenu */
	QScrollArea *scroll = new QScrollArea(main);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	dockWidget = new ProjectsDock(scroll);
	scroll->setWidget(dockWidget);

	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), scroll)) {
		delete scroll;
		return false;
	}

	obs_frontend_add_save_callback(SaveCallback, nullptr);
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	blog(LOG_INFO, "[amal-projets] Module Projets chargé");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	obs_frontend_remove_save_callback(SaveCallback, nullptr);
}
