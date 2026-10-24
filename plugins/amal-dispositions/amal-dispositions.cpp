/******************************************************************************
    Amal Studio - module « Dispositions » (1 à 4 intervenants)
    Copyright (C) 2026 Amal Ben Aly

    Un clic sur « 1 », « 2 », « 3 » ou « 4 » compose automatiquement une scène :
    fond décoratif, une caméra par cadre (recadrée pour remplir le cadre) et,
    par-dessus, les bordures et les noms des intervenants dessinés par la
    source « Amal Graphique ». Les choix sont enregistrés dans le projet.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/config-file.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <array>
#include <cstring>
#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-dispositions", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : dispositions multi-intervenants";
}

namespace {

constexpr const char *DOCK_ID = "amal-dispositions";
constexpr const char *SAVE_KEY = "amal_dispositions";
constexpr const char *CONFIG_SECTION = "AmalDispositions";
constexpr const char *GRAPHIC_ID = "amal_graphique";
constexpr int MAX_SLOTS = 4;

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* Rectangle en unités de conception (image de 1920 x 1080) */
struct Rect {
	double x, y, w, h;
};

/* Emplacements des cadres selon le nombre d'intervenants (cadres en 16:9) */
std::vector<Rect> LayoutFor(int count, bool borderless)
{
	switch (count) {
	case 2:
		return {{60, 293, 876, 493}, {984, 293, 876, 493}};
	case 3:
		return {{60, 203, 1196, 673}, {1304, 203, 556, 313}, {1304, 564, 556, 313}};
	case 4:
		return {{90, 40, 846, 476}, {984, 40, 846, 476}, {90, 564, 846, 476}, {984, 564, 846, 476}};
	default:
		if (borderless) {
			return {{0, 0, 1920, 1080}};
		}
		return {{48, 27, 1824, 1026}};
	}
}

/* ------------------------------------------------------------------------ */
/* Liste des sources vidéo, mise à jour à chaque ouverture                  */

class VideoSourceCombo : public QComboBox {
public:
	explicit VideoSourceCombo(QWidget *parent) : QComboBox(parent) { refresh(); }

	void showPopup() override
	{
		refresh();
		QComboBox::showPopup();
	}

	QString selectedSource() const { return currentData().toString(); }

	void selectSource(const QString &name)
	{
		refresh();
		int index = findData(name);
		if (index < 0 && !name.isEmpty()) {
			/* Source absente pour l'instant (projet en cours de chargement) : on la garde */
			addItem(name, name);
			index = count() - 1;
		}
		setCurrentIndex(index < 0 ? 0 : index);
	}

	void refresh()
	{
		const QString current = currentData().toString();
		blockSignals(true);
		clear();
		addItem(T("Slot.NoCamera"), QString());

		obs_enum_sources(
			[](void *param, obs_source_t *source) {
				VideoSourceCombo *combo = static_cast<VideoSourceCombo *>(param);
				const uint32_t flags = obs_source_get_output_flags(source);
				const char *id = obs_source_get_unversioned_id(source);
				if ((flags & OBS_SOURCE_VIDEO) == 0 || (id && strcmp(id, GRAPHIC_ID) == 0)) {
					return true;
				}
				const QString name = QString::fromUtf8(obs_source_get_name(source));
				combo->addItem(name, name);
				return true;
			},
			this);

		int index = findData(current);
		setCurrentIndex(index < 0 ? 0 : index);
		blockSignals(false);
	}
};

/* ------------------------------------------------------------------------ */
/* Panneau « Dispositions »                                                 */

class LayoutsDock;
QPointer<LayoutsDock> dockWidget;

class LayoutsDock : public QWidget {
public:
	explicit LayoutsDock(QWidget *parent) : QWidget(parent)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(8, 8, 8, 8);

		QLabel *help = new QLabel(T("Dock.Help"), this);
		help->setWordWrap(true);
		layout->addWidget(help);

		/* Boutons 1 à 4 */
		QHBoxLayout *countRow = new QHBoxLayout;
		countGroup = new QButtonGroup(this);
		for (int n = 1; n <= MAX_SLOTS; n++) {
			QPushButton *button = new QPushButton(QString::number(n), this);
			button->setCheckable(true);
			button->setMinimumHeight(44);
			QFont font = button->font();
			font.setPointSizeF(font.pointSizeF() * 1.4);
			font.setBold(true);
			button->setFont(font);
			button->setToolTip(n == 1 ? T("Count.One") : T("Count.Many").arg(n));
			countGroup->addButton(button, n);
			countRow->addWidget(button);
		}
		layout->addLayout(countRow);
		connect(countGroup, &QButtonGroup::idClicked, this, [this](int n) {
			count = n;
			updateRows();
			apply();
		});

		/* Caméra et nom de chaque cadre */
		QGridLayout *grid = new QGridLayout;
		for (int i = 0; i < MAX_SLOTS; i++) {
			labels[i] = new QLabel(T("Slot.Label").arg(i + 1), this);
			cameras[i] = new VideoSourceCombo(this);
			names[i] = new QLineEdit(this);
			names[i]->setPlaceholderText(T("Slot.NamePlaceholder"));
			grid->addWidget(labels[i], i * 2, 0);
			grid->addWidget(cameras[i], i * 2, 1);
			grid->addWidget(names[i], i * 2 + 1, 1);
		}
		grid->setColumnStretch(1, 1);
		layout->addLayout(grid);

		/* Style */
		QHBoxLayout *styleRow = new QHBoxLayout;
		styleRow->addWidget(new QLabel(T("Style"), this));
		styleCombo = new QComboBox(this);
		styleCombo->addItem(T("Style.None"), "aucune");
		styleCombo->addItem(T("Style.Thin"), "fine");
		styleCombo->addItem(T("Style.Thick"), "epaisse");
		styleCombo->addItem(T("Style.Ornate"), "ornee");
		styleCombo->setCurrentIndex(1);
		styleRow->addWidget(styleCombo, 1);
		layout->addLayout(styleRow);

		backgroundCheck = new QCheckBox(T("Background"), this);
		backgroundCheck->setChecked(true);
		layout->addWidget(backgroundCheck);

		QPushButton *applyButton = new QPushButton(T("Apply"), this);
		connect(applyButton, &QPushButton::clicked, this, &LayoutsDock::apply);
		layout->addWidget(applyButton);

		status = new QLabel(this);
		status->setWordWrap(true);
		layout->addWidget(status);
		layout->addStretch(1);

		countGroup->button(count)->setChecked(true);
		updateRows();
	}

	void save(obs_data_t *obj) const
	{
		obs_data_set_int(obj, "nombre", count);
		obs_data_set_string(obj, "style", styleCombo->currentData().toString().toUtf8().constData());
		obs_data_set_bool(obj, "fond", backgroundCheck->isChecked());
		OBSDataArrayAutoRelease slotList = obs_data_array_create();
		for (int i = 0; i < MAX_SLOTS; i++) {
			OBSDataAutoRelease slot = obs_data_create();
			obs_data_set_string(slot, "source", cameras[i]->selectedSource().toUtf8().constData());
			obs_data_set_string(slot, "nom", names[i]->text().toUtf8().constData());
			obs_data_array_push_back(slotList, slot);
		}
		obs_data_set_array(obj, "cadres", slotList);
	}

	void load(obs_data_t *obj)
	{
		count = 2;
		int style = 1;
		bool background = true;
		std::array<QString, MAX_SLOTS> sources;
		std::array<QString, MAX_SLOTS> labelsText;

		if (obj) {
			count = (int)obs_data_get_int(obj, "nombre");
			if (count < 1 || count > MAX_SLOTS) {
				count = 2;
			}
			int found = styleCombo->findData(QString::fromUtf8(obs_data_get_string(obj, "style")));
			style = found < 0 ? 1 : found;
			background = obs_data_get_bool(obj, "fond");
			OBSDataArrayAutoRelease slotList = obs_data_get_array(obj, "cadres");
			for (size_t i = 0; slotList && i < obs_data_array_count(slotList) && i < MAX_SLOTS; i++) {
				OBSDataAutoRelease slot = obs_data_array_item(slotList, i);
				sources[i] = QString::fromUtf8(obs_data_get_string(slot, "source"));
				labelsText[i] = QString::fromUtf8(obs_data_get_string(slot, "nom"));
			}
		}

		styleCombo->setCurrentIndex(style);
		backgroundCheck->setChecked(background);
		for (int i = 0; i < MAX_SLOTS; i++) {
			cameras[i]->selectSource(sources[i]);
			names[i]->setText(labelsText[i]);
		}
		countGroup->button(count)->setChecked(true);
		updateRows();
		status->clear();
	}

private:
	void updateRows()
	{
		for (int i = 0; i < MAX_SLOTS; i++) {
			const bool visible = i < count;
			labels[i]->setVisible(visible);
			cameras[i]->setVisible(visible);
			names[i]->setVisible(visible);
		}
	}

	QString sceneName() const
	{
		return count == 1 ? T("Scene.One") : T("Scene.Many").arg(count);
	}

	/* Source « Amal Graphique » réutilisable, créée au besoin */
	static obs_source_t *GetOrCreateGraphic(const QString &name, obs_data_t *settings)
	{
		obs_source_t *source = obs_get_source_by_name(name.toUtf8().constData());
		if (source) {
			obs_source_update(source, settings);
			return source;
		}
		return obs_source_create(GRAPHIC_ID, name.toUtf8().constData(), settings, nullptr);
	}

	void apply()
	{
		obs_video_info ovi = {};
		if (!obs_get_video_info(&ovi) || !ovi.base_width || !ovi.base_height) {
			status->setText(T("Error.NoVideo"));
			return;
		}
		const double sx = (double)ovi.base_width / 1920.0;
		const double sy = (double)ovi.base_height / 1080.0;

		const QString style = styleCombo->currentData().toString();
		const bool borderless = style == "aucune" && !backgroundCheck->isChecked();
		const std::vector<Rect> rects = LayoutFor(count, borderless);

		/* Scène de la disposition : créée ou vidée */
		const QString name = sceneName();
		OBSSourceAutoRelease sceneSource = obs_get_source_by_name(name.toUtf8().constData());
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		OBSSceneAutoRelease created;
		if (!scene) {
			if (sceneSource) {
				status->setText(T("Error.NameTaken").arg(name));
				return;
			}
			created = obs_scene_create(name.toUtf8().constData());
			scene = created;
		} else {
			std::vector<obs_sceneitem_t *> items;
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
					obs_sceneitem_addref(item);
					static_cast<std::vector<obs_sceneitem_t *> *>(param)->push_back(item);
					return true;
				},
				&items);
			for (obs_sceneitem_t *item : items) {
				obs_sceneitem_remove(item);
				obs_sceneitem_release(item);
			}
		}

		/* 1. Fond décoratif */
		if (backgroundCheck->isChecked()) {
			OBSDataAutoRelease settings = obs_data_create();
			obs_data_set_string(settings, "modele", "fond");
			OBSSourceAutoRelease background = GetOrCreateGraphic(T("Source.Background"), settings);
			if (background) {
				obs_sceneitem_t *item = obs_scene_add(scene, background);
				if (item) {
					obs_sceneitem_set_locked(item, true);
				}
			}
		}

		/* 2. Une caméra par cadre, recadrée pour remplir le cadre */
		OBSDataArrayAutoRelease frames = obs_data_array_create();
		int missing = 0;
		for (int i = 0; i < count && i < (int)rects.size(); i++) {
			const Rect &r = rects[(size_t)i];
			const QString cameraName = cameras[i]->selectedSource();

			OBSDataAutoRelease frame = obs_data_create();
			obs_data_set_double(frame, "x", r.x);
			obs_data_set_double(frame, "y", r.y);
			obs_data_set_double(frame, "w", r.w);
			obs_data_set_double(frame, "h", r.h);
			obs_data_set_string(frame, "label", names[i]->text().trimmed().toUtf8().constData());
			obs_data_array_push_back(frames, frame);

			if (cameraName.isEmpty()) {
				missing++;
				continue;
			}
			OBSSourceAutoRelease camera = obs_get_source_by_name(cameraName.toUtf8().constData());
			if (!camera) {
				missing++;
				continue;
			}
			obs_sceneitem_t *item = obs_scene_add(scene, camera);
			if (!item) {
				continue;
			}
			vec2 pos;
			vec2 bounds;
			vec2_set(&pos, (float)(r.x * sx), (float)(r.y * sy));
			vec2_set(&bounds, (float)(r.w * sx), (float)(r.h * sy));
			obs_sceneitem_set_pos(item, &pos);
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_OUTER);
			obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
			obs_sceneitem_set_bounds(item, &bounds);
			obs_sceneitem_set_bounds_crop(item, true);
		}

		/* 3. Bordures et noms par-dessus */
		OBSDataAutoRelease wrapper = obs_data_create();
		obs_data_set_array(wrapper, "frames", frames);
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "modele", "cadres");
		obs_data_set_string(settings, "style_cadre", style.toUtf8().constData());
		obs_data_set_string(settings, "cadres_json", obs_data_get_json(wrapper));
		OBSSourceAutoRelease overlay = GetOrCreateGraphic(T("Source.Frames").arg(count), settings);
		if (overlay) {
			obs_sceneitem_t *item = obs_scene_add(scene, overlay);
			if (item) {
				obs_sceneitem_set_locked(item, true);
			}
		}

		obs_frontend_set_current_scene(obs_scene_get_source(scene));
		obs_frontend_save();

		status->setText(missing ? T("Status.Missing").arg(name).arg(missing) : T("Status.Done").arg(name));
		blog(LOG_INFO, "[amal-dispositions] Disposition appliquée : %s", name.toUtf8().constData());
	}

	int count = 2;
	QButtonGroup *countGroup = nullptr;
	std::array<QLabel *, MAX_SLOTS> labels{};
	std::array<VideoSourceCombo *, MAX_SLOTS> cameras{};
	std::array<QLineEdit *, MAX_SLOTS> names{};
	QComboBox *styleCombo = nullptr;
	QCheckBox *backgroundCheck = nullptr;
	QLabel *status = nullptr;
};

/* ------------------------------------------------------------------------ */
/* Lien avec le moteur                                                      */

void SaveCallback(obs_data_t *saveData, bool saving, void *)
{
	if (!dockWidget) {
		return;
	}
	if (saving) {
		OBSDataAutoRelease obj = obs_data_create();
		dockWidget->save(obj);
		obs_data_set_obj(saveData, SAVE_KEY, obj);
	} else {
		OBSDataAutoRelease obj = obs_data_get_obj(saveData, SAVE_KEY);
		dockWidget->load(obj);
	}
}

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
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		ShowDockOnFirstRun();
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
	dockWidget = new LayoutsDock(scroll);
	scroll->setWidget(dockWidget);

	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), scroll)) {
		delete scroll;
		return false;
	}

	obs_frontend_add_save_callback(SaveCallback, nullptr);
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	blog(LOG_INFO, "[amal-dispositions] Module Dispositions chargé");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	obs_frontend_remove_save_callback(SaveCallback, nullptr);
}
