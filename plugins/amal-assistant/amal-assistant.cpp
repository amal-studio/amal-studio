/******************************************************************************
    Amal Studio - module « Assistant de démarrage »
    Copyright (C) 2026 Amal Ben Aly

    À la première ouverture (et à tout moment depuis le menu Outils), quelques
    questions simples : émission, caméra, micro, scènes voulues, thème.
    L'assistant crée ensuite un projet prêt à l'emploi :
      1 · Attente, 2 · Intro, 3 · Direct, 4 · Coran et citations,
      5 · Informations et dons, 6 · Pause.

    Aucun texte religieux n'est ajouté par l'assistant (seule la source de
    citation existante garde sa valeur par défaut : la Basmala, texte Tanzil).

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/config-file.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>

#include <cstring>
#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-assistant", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : assistant de démarrage";
}

namespace {

constexpr const char *CONFIG_SECTION = "AmalAssistant";
constexpr const char *GRAPHIC_ID = "amal_graphique";

#if defined(_WIN32)
constexpr const char *CAMERA_ID = "dshow_input";
constexpr const char *CAMERA_KEY = "video_device_id";
constexpr const char *MIC_ID = "wasapi_input_capture";
#elif defined(__APPLE__)
constexpr const char *CAMERA_ID = "macos-avcapture";
constexpr const char *CAMERA_KEY = "device";
constexpr const char *MIC_ID = "coreaudio_input_capture";
#else
constexpr const char *CAMERA_ID = "v4l2_input";
constexpr const char *CAMERA_KEY = "device_id";
constexpr const char *MIC_ID = "pulse_input_capture";
#endif
constexpr const char *MIC_KEY = "device_id";
constexpr uint32_t MIC_CHANNEL = 3; /* canal « Mic/Aux » du logiciel */

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* Appareils proposés par un type de source (caméras, micros), via une source temporaire */
std::vector<std::pair<QString, QString>> ListDevices(const char *sourceId, const char *property)
{
	std::vector<std::pair<QString, QString>> devices;
	OBSSourceAutoRelease probe = obs_source_create_private(sourceId, "amal-assistant-detection", nullptr);
	if (!probe) {
		return devices;
	}
	obs_properties_t *props = obs_source_properties(probe);
	obs_property_t *list = props ? obs_properties_get(props, property) : nullptr;
	if (list && obs_property_get_type(list) == OBS_PROPERTY_LIST &&
	    obs_property_list_format(list) == OBS_COMBO_FORMAT_STRING) {
		const size_t count = obs_property_list_item_count(list);
		for (size_t i = 0; i < count; i++) {
			const char *value = obs_property_list_item_string(list, i);
			const char *name = obs_property_list_item_name(list, i);
			if (value && *value && !obs_property_list_item_disabled(list, i)) {
				devices.emplace_back(QString::fromUtf8(name ? name : value), QString::fromUtf8(value));
			}
		}
	}
	obs_properties_destroy(props);
	return devices;
}

/* ------------------------------------------------------------------------ */
/* Pages de l'assistant                                                     */

class AssistantWizard : public QWizard {
public:
	explicit AssistantWizard(QWidget *parent) : QWizard(parent)
	{
		setWindowTitle(T("Title"));
		setWizardStyle(QWizard::ModernStyle);
		setMinimumSize(640, 520);
		setButtonText(QWizard::NextButton, T("Next"));
		setButtonText(QWizard::BackButton, T("Back"));
		setButtonText(QWizard::FinishButton, T("Finish"));
		setButtonText(QWizard::CancelButton, T("Cancel"));

		addPage(welcomePage());
		addPage(devicesPage());
		addPage(scenesPage());
		addPage(themePage());
	}

	/* Création du projet à partir des réponses */
	void build();

private:
	QWizardPage *welcomePage()
	{
		QWizardPage *page = new QWizardPage(this);
		page->setTitle(T("Welcome.Title"));
		QVBoxLayout *v = new QVBoxLayout(page);
		QLabel *text = new QLabel(T("Welcome.Text"), page);
		text->setWordWrap(true);
		v->addWidget(text);
		QFormLayout *form = new QFormLayout;
		showName = new QLineEdit(page);
		showName->setPlaceholderText(T("Welcome.ShowName.Placeholder"));
		organisation = new QLineEdit(page);
		organisation->setPlaceholderText(T("Welcome.Organisation.Placeholder"));
		form->addRow(T("Welcome.ShowName"), showName);
		form->addRow(T("Welcome.Organisation"), organisation);
		v->addLayout(form);
		v->addStretch(1);
		return page;
	}

	QWizardPage *devicesPage()
	{
		QWizardPage *page = new QWizardPage(this);
		page->setTitle(T("Devices.Title"));
		page->setSubTitle(T("Devices.Subtitle"));
		QFormLayout *form = new QFormLayout(page);

		camera = new QComboBox(page);
		camera->addItem(T("Devices.NoCamera"), QString());
		/* Sources vidéo déjà présentes (caméra, téléphone…) */
		obs_enum_sources(
			[](void *param, obs_source_t *source) {
				QComboBox *combo = static_cast<QComboBox *>(param);
				const char *id = obs_source_get_unversioned_id(source);
				if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) == 0 ||
				    (id && strcmp(id, GRAPHIC_ID) == 0)) {
					return true;
				}
				const QString name = QString::fromUtf8(obs_source_get_name(source));
				combo->addItem(T("Devices.Existing").arg(name), "source:" + name);
				return true;
			},
			camera);
		for (const auto &device : ListDevices(CAMERA_ID, CAMERA_KEY)) {
			camera->addItem(T("Devices.Device").arg(device.first), "device:" + device.second);
		}
		if (camera->count() > 1) {
			camera->setCurrentIndex(1);
		}

		mic = new QComboBox(page);
		mic->addItem(T("Devices.KeepMic"), QString());
		for (const auto &device : ListDevices(MIC_ID, MIC_KEY)) {
			mic->addItem(device.first, device.second);
		}

		form->addRow(T("Devices.Camera"), camera);
		form->addRow(T("Devices.Mic"), mic);
		QLabel *help = new QLabel(T("Devices.Help"), page);
		help->setWordWrap(true);
		form->addRow(help);
		return page;
	}

	QWizardPage *scenesPage()
	{
		QWizardPage *page = new QWizardPage(this);
		page->setTitle(T("Scenes.Title"));
		page->setSubTitle(T("Scenes.Subtitle"));
		QVBoxLayout *v = new QVBoxLayout(page);
		const char *keys[] = {"Scene.Waiting", "Scene.Intro", "Scene.Live", "Scene.Quran", "Scene.Info",
				      "Scene.Pause"};
		for (int i = 0; i < 6; i++) {
			scenes[i] = new QCheckBox(T(keys[i]), page);
			scenes[i]->setChecked(true);
			v->addWidget(scenes[i]);
		}
		v->addStretch(1);
		return page;
	}

	QWizardPage *themePage()
	{
		QWizardPage *page = new QWizardPage(this);
		page->setTitle(T("Theme.Title"));
		page->setSubTitle(T("Theme.Subtitle"));
		QFormLayout *form = new QFormLayout(page);
		theme = new QComboBox(page);
		const char *packs[] = {"Pack.Emerald", "Pack.Medina", "Pack.Burgundy", "Pack.Violet",
				       "Pack.Sand",    "Pack.Turquoise", "Pack.White", "Pack.Glass"};
		for (int i = 0; i < 8; i++) {
			theme->addItem(T(packs[i]), i);
		}
		QHBoxLayout *logoRow = new QHBoxLayout;
		logo = new QLineEdit(page);
		logo->setPlaceholderText(T("Theme.Logo.Placeholder"));
		QPushButton *browse = new QPushButton(T("Browse"), page);
		logoRow->addWidget(logo, 1);
		logoRow->addWidget(browse);
		form->addRow(T("Theme.Pack"), theme);
		form->addRow(T("Theme.Logo"), logoRow);
		QLabel *help = new QLabel(T("Theme.Help"), page);
		help->setWordWrap(true);
		form->addRow(help);
		connect(browse, &QPushButton::clicked, this, [this] {
			const QString file = QFileDialog::getOpenFileName(
				this, T("Theme.Logo"), QString(), "Images (*.png *.jpg *.jpeg *.webp *.svg *.gif)");
			if (!file.isEmpty()) {
				logo->setText(file);
			}
		});
		return page;
	}

	QLineEdit *showName = nullptr;
	QLineEdit *organisation = nullptr;
	QComboBox *camera = nullptr;
	QComboBox *mic = nullptr;
	QCheckBox *scenes[6] = {};
	QComboBox *theme = nullptr;
	QLineEdit *logo = nullptr;
};

/* ------------------------------------------------------------------------ */
/* Construction des scènes                                                  */

QString UniqueName(const QString &base)
{
	QString name = base;
	for (int n = 2; n < 100; n++) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.toUtf8().constData());
		if (!existing) {
			break;
		}
		name = QString("%1 (%2)").arg(base).arg(n);
	}
	return name;
}

OBSSource CreateGraphic(const QString &name, obs_data_t *settings)
{
	OBSSourceAutoRelease source = obs_source_create(GRAPHIC_ID, UniqueName(name).toUtf8().constData(), settings,
							 nullptr);
	return OBSSource(source.Get());
}

/* Remplit toute l'image (recadrage sans déformation) */
void FillCanvas(obs_sceneitem_t *item)
{
	obs_video_info ovi = {};
	if (!item || !obs_get_video_info(&ovi)) {
		return;
	}
	vec2 pos;
	vec2 bounds;
	vec2_set(&pos, 0.0f, 0.0f);
	vec2_set(&bounds, (float)ovi.base_width, (float)ovi.base_height);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_OUTER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_set_bounds_crop(item, true);
}

/* Fenêtre du panneau latéral (unités 1920 x 1080), identique à overlay.js */
void PlaceInSideWindow(obs_sceneitem_t *item)
{
	obs_video_info ovi = {};
	if (!item || !obs_get_video_info(&ovi)) {
		return;
	}
	const float sx = (float)ovi.base_width / 1920.0f, sy = (float)ovi.base_height / 1080.0f;
	vec2 pos;
	vec2 bounds;
	vec2_set(&pos, 600.0f * sx, 0.0f);
	vec2_set(&bounds, 1320.0f * sx, 870.0f * sy);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_OUTER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_set_bounds_crop(item, true);
}

void AssistantWizard::build()
{
	const QString show = showName->text().trimmed();
	const QString org = organisation->text().trimmed();

	/* 1. Thème du projet (Studio graphique) : défini avant de créer les habillages */
	{
		calldata_t cd = {};
		const QByteArray logoPath = logo->text().trimmed().toUtf8();
		calldata_set_int(&cd, "pack", theme->currentData().toInt());
		calldata_set_string(&cd, "logo", logoPath.constData());
		proc_handler_call(obs_get_proc_handler(), "amal_theme_choisir", &cd);
		calldata_free(&cd);
	}

	/* 2. Caméra */
	OBSSource cameraSource;
	const QString cameraChoice = camera->currentData().toString();
	if (cameraChoice.startsWith("source:")) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(cameraChoice.mid(7).toUtf8().constData());
		cameraSource = existing.Get();
	} else if (cameraChoice.startsWith("device:")) {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, CAMERA_KEY, cameraChoice.mid(7).toUtf8().constData());
		OBSSourceAutoRelease created = obs_source_create(
			CAMERA_ID, UniqueName(T("Name.Camera")).toUtf8().constData(), settings, nullptr);
		cameraSource = created.Get();
	}

	/* 3. Micro : canal « Mic/Aux » */
	const QString micDevice = mic->currentData().toString();
	if (!micDevice.isEmpty()) {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, MIC_KEY, micDevice.toUtf8().constData());
		OBSSourceAutoRelease current = obs_get_output_source(MIC_CHANNEL);
		if (current) {
			obs_source_update(current, settings);
		} else {
			OBSSourceAutoRelease created =
				obs_source_create(MIC_ID, T("Name.Mic").toUtf8().constData(), settings, nullptr);
			obs_set_output_source(MIC_CHANNEL, created);
		}
	}

	auto newScene = [](const char *key) {
		OBSSceneAutoRelease scene = obs_scene_create(UniqueName(T(key)).toUtf8().constData());
		return OBSScene(scene.Get());
	};
	auto addCamera = [&cameraSource](obs_scene_t *scene, bool side) {
		if (!cameraSource) {
			return;
		}
		obs_sceneitem_t *item = obs_scene_add(scene, cameraSource);
		if (side) {
			PlaceInSideWindow(item);
		} else {
			FillCanvas(item);
		}
	};

	OBSScene firstScene;
	OBSScene liveScene;
	std::vector<std::pair<OBSScene, OBSSource>> intros;

	/* Scène 1 : attente */
	if (scenes[0]->isChecked()) {
		OBSScene scene = newScene("Scene.Name.Waiting");
		OBSDataAutoRelease s = obs_data_create();
		obs_data_set_string(s, "modele", "attente");
		obs_data_set_string(s, "titre", T("Text.WaitTitle").toUtf8().constData());
		obs_data_set_string(s, "sous_titre", show.toUtf8().constData());
		obs_data_set_string(s, "texte", T("Text.WaitDone").toUtf8().constData());
		obs_data_set_string(s, "texte_arabe", "");
		obs_data_set_int(s, "compte_minutes", 10);
		OBSSource g = CreateGraphic(T("Name.Waiting"), s);
		obs_scene_add(scene, g);
		firstScene = scene;
	}

	/* Scène 2 : intro (enchaîne sur le direct) */
	if (scenes[1]->isChecked()) {
		OBSScene scene = newScene("Scene.Name.Intro");
		OBSDataAutoRelease s = obs_data_create();
		obs_data_set_string(s, "modele", "intro");
		obs_data_set_string(s, "titre", (show.isEmpty() ? T("Text.ShowPlaceholder") : show).toUtf8().constData());
		obs_data_set_string(s, "sous_titre", org.toUtf8().constData());
		obs_data_set_string(s, "texte_arabe", "");
		obs_data_set_int(s, "duree_intro", 8);
		OBSSource g = CreateGraphic(T("Name.Intro"), s);
		obs_scene_add(scene, g);
		intros.emplace_back(scene, g);
		if (!firstScene) {
			firstScene = scene;
		}
	}

	/* Scène 3 : direct (caméra, bandeau TV, intervenant prêt à afficher) */
	if (scenes[2]->isChecked()) {
		OBSScene scene = newScene("Scene.Name.Live");
		addCamera(scene, false);
		OBSDataAutoRelease tv = obs_data_create();
		obs_data_set_string(tv, "modele", "direct");
		obs_data_set_string(tv, "titre", show.toUtf8().constData());
		obs_data_set_string(tv, "sous_titre", org.toUtf8().constData());
		obs_data_set_string(tv, "texte_arabe", "");
		OBSSource tvGraphic = CreateGraphic(T("Name.Broadcast"), tv);
		obs_scene_add(scene, tvGraphic);
		OBSDataAutoRelease speaker = obs_data_create();
		obs_data_set_string(speaker, "modele", "intervenant");
		obs_data_set_string(speaker, "texte_arabe", "");
		OBSSource speakerGraphic = CreateGraphic(T("Name.Speaker"), speaker);
		obs_sceneitem_t *item = obs_scene_add(scene, speakerGraphic);
		if (item) {
			obs_sceneitem_set_visible(item, false); /* l'œil dans « Sources » l'affiche */
		}
		liveScene = scene;
		if (!firstScene) {
			firstScene = scene;
		}
	}

	/* Scène 4 : Coran et citations (source commune « Amal · Citation ») */
	if (scenes[3]->isChecked()) {
		OBSScene scene = newScene("Scene.Name.Quran");
		addCamera(scene, false);
		const QString citation = T("Name.Citation");
		OBSSourceAutoRelease existing = obs_get_source_by_name(citation.toUtf8().constData());
		OBSSource citationSource = existing.Get();
		if (!citationSource) {
			OBSDataAutoRelease s = obs_data_create();
			obs_data_set_string(s, "modele", "verset");
			OBSSourceAutoRelease created =
				obs_source_create(GRAPHIC_ID, citation.toUtf8().constData(), s, nullptr);
			citationSource = created.Get();
		}
		obs_scene_add(scene, citationSource);
		if (!firstScene) {
			firstScene = scene;
		}
	}

	/* Scène 5 : informations et dons (panneau latéral) */
	if (scenes[4]->isChecked()) {
		OBSScene scene = newScene("Scene.Name.Info");
		addCamera(scene, true);
		OBSDataAutoRelease s = obs_data_create();
		obs_data_set_string(s, "modele", "evenement");
		obs_data_set_string(s, "disposition_evt", "lateral");
		obs_data_set_string(s, "libelle", T("Text.InfoTitle").toUtf8().constData());
		obs_data_set_string(s, "texte", T("Text.InfoCards").toUtf8().constData());
		obs_data_set_string(s, "titre", show.toUtf8().constData());
		obs_data_set_string(s, "sous_titre", org.toUtf8().constData());
		obs_data_set_string(s, "texte_arabe", "");
		OBSSource g = CreateGraphic(T("Name.Info"), s);
		obs_scene_add(scene, g);
		if (!firstScene) {
			firstScene = scene;
		}
	}

	/* Scène 6 : pause */
	if (scenes[5]->isChecked()) {
		OBSScene scene = newScene("Scene.Name.Pause");
		OBSDataAutoRelease bg = obs_data_create();
		obs_data_set_string(bg, "modele", "fond");
		OBSSource background = CreateGraphic(T("Name.Background"), bg);
		obs_scene_add(scene, background);
		OBSDataAutoRelease title = obs_data_create();
		obs_data_set_string(title, "modele", "titre");
		obs_data_set_string(title, "titre", T("Text.PauseTitle").toUtf8().constData());
		obs_data_set_string(title, "sous_titre", T("Text.PauseSubtitle").toUtf8().constData());
		obs_data_set_string(title, "reference", show.toUtf8().constData());
		obs_data_set_string(title, "texte_arabe", "");
		obs_data_set_string(title, "position", "centre");
		OBSSource g = CreateGraphic(T("Name.Pause"), title);
		obs_scene_add(scene, g);
		if (!firstScene) {
			firstScene = scene;
		}
	}

	/* L'intro enchaîne sur la scène « Direct » */
	if (liveScene) {
		const char *liveName = obs_source_get_name(obs_scene_get_source(liveScene));
		for (auto &intro : intros) {
			OBSDataAutoRelease s = obs_data_create();
			obs_data_set_string(s, "scene_suivante", liveName);
			obs_source_update(intro.second, s);
		}
	}

	if (firstScene) {
		obs_frontend_set_current_scene(obs_scene_get_source(firstScene));
	}

	config_t *config = obs_frontend_get_app_config();
	config_set_bool(config, CONFIG_SECTION, "Done", true);
	config_save_safe(config, "tmp", nullptr);
	obs_frontend_save();
	blog(LOG_INFO, "[amal-assistant] Projet de départ créé");
}

void OpenAssistant()
{
	QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	AssistantWizard wizard(main);
	if (wizard.exec() == QDialog::Accepted) {
		wizard.build();
	} else {
		/* Ne pas reproposer à chaque ouverture : l'assistant reste dans le menu Outils */
		config_t *config = obs_frontend_get_app_config();
		config_set_bool(config, CONFIG_SECTION, "Done", true);
		config_save_safe(config, "tmp", nullptr);
	}
}

/* Attend que les autres fenêtres de premier démarrage (réglages automatiques) soient fermées */
void OpenWhenFree()
{
	if (QApplication::activeModalWidget()) {
		QTimer::singleShot(2000, static_cast<QWidget *>(obs_frontend_get_main_window()), [] { OpenWhenFree(); });
		return;
	}
	OpenAssistant();
}

void FrontendEvent(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		return;
	}
	config_t *config = obs_frontend_get_app_config();
	if (config && !config_get_bool(config, CONFIG_SECTION, "Done")) {
		/* Laisse la fenêtre principale s'afficher avant l'assistant */
		QTimer::singleShot(1500, static_cast<QWidget *>(obs_frontend_get_main_window()), [] { OpenWhenFree(); });
	}
}

} // namespace

bool obs_module_load(void)
{
	obs_frontend_add_tools_menu_item(obs_module_text("Menu"), [](void *) { OpenAssistant(); }, nullptr);
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
}
