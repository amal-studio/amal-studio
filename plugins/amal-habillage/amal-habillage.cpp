/******************************************************************************
    Amal Studio - module « Studio graphique »
    Copyright (C) 2026 Amal Ben Aly

    - Thème du projet : couleurs, style d'habillage, motif et logo, choisis
      une seule fois pour tous les habillages Amal (versets, titres, bandeaux,
      cadres, transitions). Huit thèmes prêts à l'emploi, plus les thèmes de
      l'utilisateur (enregistrés, exportés, importés).
    - Ajout en un clic des habillages courants dans la scène affichée.

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
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTimer>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cstring>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-habillage", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : studio graphique (thème du projet, habillages en un clic)";
}

namespace {

constexpr const char *DOCK_ID = "amal-habillage";
constexpr const char *SAVE_KEY = "amal_theme";
constexpr const char *CONFIG_SECTION = "AmalHabillage";
constexpr const char *GRAPHIC_ID = "amal_graphique";
constexpr const char *TRANSITION_ID = "amal_transition";

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* Couleur OBS (0xAABBGGRR) <-> QColor */
long long ToObs(const QColor &c)
{
	return (long long)(0xFF000000u | ((uint32_t)c.blue() << 16) | ((uint32_t)c.green() << 8) | (uint32_t)c.red());
}

QColor FromObs(long long value)
{
	const uint32_t v = (uint32_t)value;
	return QColor((int)(v & 0xFF), (int)((v >> 8) & 0xFF), (int)((v >> 16) & 0xFF));
}

struct Theme {
	QString name;
	QColor primary;
	QColor accent;
	QColor text;
	QString look; /* classique | moderne | epure | verre */
	bool motif = true;
	QString logo;

	QJsonObject toJson() const
	{
		return QJsonObject{{"nom", name},
				   {"couleur_principale", primary.name()},
				   {"couleur_accent", accent.name()},
				   {"couleur_texte", text.name()},
				   {"habillage", look},
				   {"motif", motif},
				   {"logo", logo}};
	}

	static Theme fromJson(const QJsonObject &o)
	{
		Theme t;
		t.name = o.value("nom").toString();
		t.primary = QColor(o.value("couleur_principale").toString("#0B6B4F"));
		t.accent = QColor(o.value("couleur_accent").toString("#D4AF37"));
		t.text = QColor(o.value("couleur_texte").toString("#FFFFFF"));
		t.look = o.value("habillage").toString("classique");
		t.motif = o.value("motif").toBool(true);
		t.logo = o.value("logo").toString();
		for (QColor *c : {&t.primary, &t.accent, &t.text}) {
			if (!c->isValid()) {
				*c = Qt::white;
			}
		}
		return t;
	}

	/* Réglages au format des sources Amal (couleurs OBS) */
	OBSDataAutoRelease toSettings(bool withLook) const
	{
		OBSDataAutoRelease data = obs_data_create();
		obs_data_set_int(data, "couleur_principale", ToObs(primary));
		obs_data_set_int(data, "couleur_accent", ToObs(accent));
		if (withLook) {
			obs_data_set_int(data, "couleur_texte", ToObs(text));
			obs_data_set_string(data, "habillage", look.toUtf8().constData());
			obs_data_set_bool(data, "motif", motif);
		}
		if (!logo.isEmpty()) {
			obs_data_set_string(data, "logo", logo.toUtf8().constData());
		}
		return data;
	}
};

struct Pack {
	const char *nameKey;
	const char *primary;
	const char *accent;
	const char *text;
	const char *look;
	bool motif;
};

/* Thèmes prêts à l'emploi */
const Pack PACKS[] = {
	{"Pack.Emerald", "#0B6B4F", "#D4AF37", "#FFFFFF", "classique", true},
	{"Pack.Medina", "#13294B", "#C9A54C", "#FFFFFF", "classique", true},
	{"Pack.Burgundy", "#6B1E2E", "#D9B44A", "#FFFFFF", "classique", true},
	{"Pack.Violet", "#3B2A6B", "#E3BC5A", "#FFFFFF", "classique", true},
	{"Pack.Sand", "#7A4E26", "#F0D9A8", "#FFFFFF", "moderne", true},
	{"Pack.Turquoise", "#0E6E78", "#F2C14E", "#FFFFFF", "moderne", false},
	{"Pack.White", "#0B6B4F", "#C9A227", "#1B2B26", "epure", false},
	{"Pack.Glass", "#101418", "#D4AF37", "#FFFFFF", "verre", false},
};
constexpr int PACK_COUNT = (int)(sizeof(PACKS) / sizeof(PACKS[0]));

Theme PackTheme(int index)
{
	const Pack &p = PACKS[index];
	Theme t;
	t.name = T(p.nameKey);
	t.primary = QColor(p.primary);
	t.accent = QColor(p.accent);
	t.text = QColor(p.text);
	t.look = p.look;
	t.motif = p.motif;
	return t;
}

QIcon Swatch(const Theme &t)
{
	QPixmap pix(42, 16);
	pix.fill(Qt::transparent);
	QPainter painter(&pix);
	painter.fillRect(0, 0, 24, 16, t.look == "epure" ? QColor(Qt::white) : t.primary);
	painter.fillRect(24, 0, 10, 16, t.accent);
	painter.fillRect(34, 0, 8, 16, t.look == "epure" ? t.primary : t.text);
	painter.setPen(QColor(0, 0, 0, 90));
	painter.drawRect(0, 0, 41, 15);
	return QIcon(pix);
}

/* Habillages proposés en un clic : modèle, nom de la source, placement */
struct QuickGraphic {
	const char *model;
	const char *labelKey;
	const char *nameKey;
	bool bottom;
	bool openProperties;
	const char *eventLayout; /* cadre d'événement : « cadre » ou « lateral » */
};

const QuickGraphic QUICK[] = {
	{"evenement", "Quick.EventFrame", "Name.EventFrame", false, true, "cadre"},
	{"evenement", "Quick.EventSide", "Name.EventSide", false, true, "lateral"},
	{"titre", "Quick.Title", "Name.Title", false, true, nullptr},
	{"intervenant", "Quick.Speaker", "Name.Speaker", false, true, nullptr},
	{"direct", "Quick.Broadcast", "Name.Broadcast", false, true, nullptr},
	{"bandeau", "Quick.Ticker", "Name.Ticker", false, true, nullptr},
	{"rappel", "Quick.Reminder", "Name.Reminder", false, true, nullptr},
	{"fond", "Quick.Background", "Name.Background", true, false, nullptr},
};

/* Fenêtre transparente des cadres d'événement (unités 1920 x 1080), identique à overlay.js */
struct Window {
	double x, y, w, h;
};

Window EventWindow(const char *layout)
{
	if (layout && strcmp(layout, "lateral") == 0) {
		return {600, 0, 1320, 870};
	}
	return {132, 92, 1656, 760};
}

/* Liste des sources vidéo (caméras, téléphones…), mise à jour à chaque ouverture */
class VideoSourceCombo : public QComboBox {
public:
	explicit VideoSourceCombo(QWidget *parent) : QComboBox(parent) { refresh(); }

	void showPopup() override
	{
		refresh();
		QComboBox::showPopup();
	}

	void refresh()
	{
		const QString current = currentData().toString();
		blockSignals(true);
		clear();
		addItem(T("Camera.None"), QString());
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
		const int index = findData(current);
		setCurrentIndex(index < 0 ? 0 : index);
		blockSignals(false);
	}
};

/* Fonds d'événement : clé, salutation arabe proposée (formule courante, modifiable) */
struct EventBackground {
	const char *key;
	const char *arabic;
};

const EventBackground EVENT_BACKGROUNDS[] = {
	{"ramadan", "\xD8\xB1\xD9\x85\xD8\xB6\xD8\xA7\xD9\x86 \xD9\x85\xD8\xA8\xD8\xA7\xD8\xB1\xD9\x83"},
	{"laylat", "\xD9\x84\xD9\x8A\xD9\x84\xD8\xA9 \xD8\xA7\xD9\x84\xD9\x82\xD8\xAF\xD8\xB1"},
	{"aid-fitr", "\xD8\xB9\xD9\x8A\xD8\xAF \xD9\x85\xD8\xA8\xD8\xA7\xD8\xB1\xD9\x83"},
	{"aid-adha", "\xD8\xB9\xD9\x8A\xD8\xAF \xD8\xA3\xD8\xB6\xD8\xAD\xD9\x89 \xD9\x85\xD8\xA8\xD8\xA7\xD8\xB1\xD9\x83"},
	{"hajj", "\xD8\xAD\xD8\xAC \xD9\x85\xD8\xA8\xD8\xB1\xD9\x88\xD8\xB1"},
	{"hegire", "\xD8\xB1\xD8\xA3\xD8\xB3 \xD8\xA7\xD9\x84\xD8\xB3\xD9\x86\xD8\xA9 \xD8\xA7\xD9\x84\xD9\x87\xD8\xAC\xD8\xB1\xD9\x8A\xD8\xA9"},
	{"achoura", "\xD8\xB9\xD8\xA7\xD8\xB4\xD9\x88\xD8\xB1\xD8\xA7\xD8\xA1"},
	{"maouloud", "\xD8\xA7\xD9\x84\xD9\x85\xD9\x88\xD9\x84\xD8\xAF \xD8\xA7\xD9\x84\xD9\x86\xD8\xA8\xD9\x88\xD9\x8A"},
	{"isra", "\xD8\xA7\xD9\x84\xD8\xA5\xD8\xB3\xD8\xB1\xD8\xA7\xD8\xA1 \xD9\x88\xD8\xA7\xD9\x84\xD9\x85\xD8\xB9\xD8\xB1\xD8\xA7\xD8\xAC"},
	{"jumua", "\xD9\x8A\xD9\x88\xD9\x85 \xD8\xA7\xD9\x84\xD8\xAC\xD9\x85\xD8\xB9\xD8\xA9"},
};

/* Liste des scènes, mise à jour à chaque ouverture */
class SceneCombo : public QComboBox {
public:
	explicit SceneCombo(QWidget *parent) : QComboBox(parent) { refresh(); }

	void showPopup() override
	{
		refresh();
		QComboBox::showPopup();
	}

	void refresh()
	{
		const QString current = currentData().toString();
		blockSignals(true);
		clear();
		addItem(T("Intro.NextScene.None"), QString());
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; i++) {
			const QString name = QString::fromUtf8(obs_source_get_name(scenes.sources.array[i]));
			addItem(name, name);
		}
		obs_frontend_source_list_free(&scenes);
		const int index = findData(current);
		setCurrentIndex(index < 0 ? 0 : index);
		blockSignals(false);
	}
};

/* Montre une scène à l'antenne (en mode studio : aperçu puis transition) */
void ShowOnProgram(obs_source_t *scene)
{
	if (obs_frontend_preview_program_mode_active()) {
		obs_frontend_set_current_preview_scene(scene);
		obs_frontend_preview_program_trigger_transition();
	} else {
		obs_frontend_set_current_scene(scene);
	}
}

void SetGlobalTheme(const Theme *theme)
{
	QByteArray json;
	if (theme) {
		OBSDataAutoRelease data = theme->toSettings(true);
		json = obs_data_get_json(data);
	}
	calldata_t cd = {};
	calldata_set_string(&cd, "json", json.constData());
	proc_handler_call(obs_get_proc_handler(), "amal_theme_definir", &cd);
	calldata_free(&cd);
}

/* ------------------------------------------------------------------------ */
/* Panneau « Studio graphique »                                             */

class StudioDock;
QPointer<StudioDock> dockWidget;

class StudioDock : public QWidget {
public:
	explicit StudioDock(QWidget *parent) : QWidget(parent)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(6, 6, 6, 6);

		/* ---------- 1. Thème du projet ---------- */
		QGroupBox *themeBox = new QGroupBox(T("Section.Theme"), this);
		QVBoxLayout *tl = new QVBoxLayout(themeBox);

		QLabel *help = new QLabel(T("Theme.Help"), themeBox);
		help->setWordWrap(true);
		tl->addWidget(help);

		packCombo = new QComboBox(themeBox);
		packCombo->setIconSize(QSize(42, 16));
		tl->addWidget(packCombo);

		QGridLayout *colors = new QGridLayout;
		const char *colorKeys[] = {"Color.Primary", "Color.Accent", "Color.Text"};
		for (int i = 0; i < 3; i++) {
			colorButtons[i] = new QPushButton(themeBox);
			colorButtons[i]->setMinimumHeight(26);
			colors->addWidget(new QLabel(T(colorKeys[i]), themeBox), i, 0);
			colors->addWidget(colorButtons[i], i, 1);
			connect(colorButtons[i], &QPushButton::clicked, this, [this, i] { pickColor(i); });
		}
		tl->addLayout(colors);

		QHBoxLayout *lookRow = new QHBoxLayout;
		lookRow->addWidget(new QLabel(T("Look"), themeBox));
		lookCombo = new QComboBox(themeBox);
		lookCombo->addItem(T("Look.Classic"), "classique");
		lookCombo->addItem(T("Look.Modern"), "moderne");
		lookCombo->addItem(T("Look.Light"), "epure");
		lookCombo->addItem(T("Look.Glass"), "verre");
		lookRow->addWidget(lookCombo, 1);
		tl->addLayout(lookRow);

		motifCheck = new QCheckBox(T("Pattern"), themeBox);
		tl->addWidget(motifCheck);

		QHBoxLayout *logoRow = new QHBoxLayout;
		logoRow->addWidget(new QLabel(T("Logo"), themeBox));
		logoEdit = new QLineEdit(themeBox);
		logoEdit->setPlaceholderText(T("Logo.Placeholder"));
		logoEdit->setClearButtonEnabled(true);
		QPushButton *browse = new QPushButton(T("Browse"), themeBox);
		logoRow->addWidget(logoEdit, 1);
		logoRow->addWidget(browse);
		tl->addLayout(logoRow);

		QPushButton *apply = new QPushButton(T("ApplyAll"), themeBox);
		apply->setMinimumHeight(34);
		QFont bold = apply->font();
		bold.setBold(true);
		apply->setFont(bold);
		tl->addWidget(apply);

		QHBoxLayout *manage = new QHBoxLayout;
		QPushButton *saveAs = new QPushButton(T("SaveTheme"), themeBox);
		deleteButton = new QPushButton(T("DeleteTheme"), themeBox);
		QPushButton *exportButton = new QPushButton(T("Export"), themeBox);
		QPushButton *importButton = new QPushButton(T("Import"), themeBox);
		manage->addWidget(saveAs);
		manage->addWidget(deleteButton);
		manage->addWidget(exportButton);
		manage->addWidget(importButton);
		tl->addLayout(manage);
		layout->addWidget(themeBox);

		/* ---------- 2. Ajouter un habillage ---------- */
		QGroupBox *addBox = new QGroupBox(T("Section.Add"), this);
		QVBoxLayout *al = new QVBoxLayout(addBox);
		QLabel *addHelp = new QLabel(T("Add.Help"), addBox);
		addHelp->setWordWrap(true);
		al->addWidget(addHelp);
		QHBoxLayout *cameraRow = new QHBoxLayout;
		cameraRow->addWidget(new QLabel(T("Camera"), addBox));
		cameraCombo = new VideoSourceCombo(addBox);
		cameraCombo->setToolTip(T("Camera.Help"));
		cameraRow->addWidget(cameraCombo, 1);
		al->addLayout(cameraRow);

		QGridLayout *grid = new QGridLayout;
		int index = 0;
		for (const QuickGraphic &q : QUICK) {
			QPushButton *button = new QPushButton(T(q.labelKey), addBox);
			button->setMinimumHeight(30);
			grid->addWidget(button, index / 2, index % 2);
			connect(button, &QPushButton::clicked, this, [this, q] { addGraphic(q); });
			index++;
		}
		al->addLayout(grid);
		layout->addWidget(addBox);

		/* ---------- 3. Événements et intro ---------- */
		QGroupBox *eventBox = new QGroupBox(T("Section.Events"), this);
		QVBoxLayout *el = new QVBoxLayout(eventBox);
		QHBoxLayout *eventRow = new QHBoxLayout;
		eventCombo = new QComboBox(eventBox);
		eventCombo->addItem(T("EventBg.none"), QString());
		for (const EventBackground &e : EVENT_BACKGROUNDS) {
			const std::string key = std::string("EventBg.") + e.key;
			eventCombo->addItem(T(key.c_str()), e.key);
		}
		QPushButton *addBackground = new QPushButton(T("Events.AddBackground"), eventBox);
		eventRow->addWidget(eventCombo, 1);
		eventRow->addWidget(addBackground);
		el->addLayout(eventRow);
		QLabel *eventHelp = new QLabel(T("Events.Help"), eventBox);
		eventHelp->setWordWrap(true);
		el->addWidget(eventHelp);

		QFormLayout *introForm = new QFormLayout;
		introStyle = new QComboBox(eventBox);
		introStyle->addItem(T("Intro.Style.Star"), "etoile");
		introStyle->addItem(T("Intro.Style.Arches"), "arches");
		introStyle->addItem(T("Intro.Style.Light"), "lumiere");
		introDuration = new QSpinBox(eventBox);
		introDuration->setRange(3, 30);
		introDuration->setValue(8);
		introDuration->setSuffix(" s");
		nextScene = new SceneCombo(eventBox);
		introForm->addRow(T("Intro.Style"), introStyle);
		introForm->addRow(T("Intro.Duration"), introDuration);
		introForm->addRow(T("Intro.NextScene"), nextScene);
		el->addLayout(introForm);
		QHBoxLayout *introRow = new QHBoxLayout;
		QPushButton *createIntro = new QPushButton(T("Intro.Create"), eventBox);
		recordButton = new QPushButton(T("Intro.Record"), eventBox);
		introRow->addWidget(createIntro);
		introRow->addWidget(recordButton);
		el->addLayout(introRow);
		layout->addWidget(eventBox);

		connect(addBackground, &QPushButton::clicked, this, &StudioDock::addEventBackground);
		connect(createIntro, &QPushButton::clicked, this, &StudioDock::createIntroScene);
		connect(recordButton, &QPushButton::clicked, this, &StudioDock::recordIntro);

		status = new QLabel(this);
		status->setWordWrap(true);
		layout->addWidget(status);
		layout->addStretch(1);

		loadCustomThemes();
		refreshPacks();
		setTheme(PackTheme(0));

		connect(packCombo, &QComboBox::activated, this, [this](int row) { choosePack(row); });
		connect(lookCombo, &QComboBox::activated, this, [this](int) { lookChanged(); });
		connect(motifCheck, &QCheckBox::toggled, this, [this](bool) { themeEdited(); });
		connect(logoEdit, &QLineEdit::editingFinished, this, [this] { themeEdited(); });
		connect(browse, &QPushButton::clicked, this, [this] {
			const QString file = QFileDialog::getOpenFileName(
				this, T("Logo"), logoEdit->text(), "Images (*.png *.jpg *.jpeg *.webp *.svg *.gif)");
			if (!file.isEmpty()) {
				logoEdit->setText(file);
				themeEdited();
			}
		});
		connect(apply, &QPushButton::clicked, this, &StudioDock::applyToAll);
		connect(saveAs, &QPushButton::clicked, this, &StudioDock::saveCurrentAsTheme);
		connect(deleteButton, &QPushButton::clicked, this, &StudioDock::deleteCustomTheme);
		connect(exportButton, &QPushButton::clicked, this, &StudioDock::exportTheme);
		connect(importButton, &QPushButton::clicked, this, &StudioDock::importTheme);
	}

	Theme theme() const
	{
		Theme t;
		t.name = packCombo->currentText();
		t.primary = colorsValue[0];
		t.accent = colorsValue[1];
		t.text = colorsValue[2];
		t.look = lookCombo->currentData().toString();
		t.motif = motifCheck->isChecked();
		t.logo = logoEdit->text().trimmed();
		return t;
	}

	void save(obs_data_t *obj) const
	{
		const QByteArray json = QJsonDocument(theme().toJson()).toJson(QJsonDocument::Compact);
		obs_data_set_string(obj, "theme", json.constData());
	}

	void load(obs_data_t *obj)
	{
		Theme t = PackTheme(0);
		if (obj) {
			const QJsonObject o = QJsonDocument::fromJson(QByteArray(obs_data_get_string(obj, "theme"))).object();
			if (!o.isEmpty()) {
				t = Theme::fromJson(o);
			}
		}
		setTheme(t);
		status->clear();
	}

private:
	void setTheme(const Theme &t)
	{
		colorsValue[0] = t.primary;
		colorsValue[1] = t.accent;
		colorsValue[2] = t.text;
		updateColorButtons();
		const int look = lookCombo->findData(t.look);
		lookCombo->setCurrentIndex(look >= 0 ? look : 0);
		motifCheck->blockSignals(true);
		motifCheck->setChecked(t.motif);
		motifCheck->blockSignals(false);
		logoEdit->setText(t.logo);
		selectMatchingPack(t);
		SetGlobalTheme(&t);
	}

	void updateColorButtons()
	{
		for (int i = 0; i < 3; i++) {
			const QColor &c = colorsValue[i];
			const QString ink = c.lightnessF() > 0.6 ? "#000000" : "#FFFFFF";
			colorButtons[i]->setText(c.name().toUpper());
			colorButtons[i]->setStyleSheet(
				QString("background-color: %1; color: %2; border: 1px solid #555; border-radius: 4px;")
					.arg(c.name(), ink));
		}
	}

	/* Le nom affiché suit les réglages : un thème prêt, un thème perso, ou « Personnalisé » */
	void selectMatchingPack(const Theme &t)
	{
		auto same = [&t](const Theme &o) {
			return o.primary == t.primary && o.accent == t.accent && o.text == t.text && o.look == t.look &&
			       o.motif == t.motif;
		};
		int match = -1;
		for (int i = 0; i < PACK_COUNT && match < 0; i++) {
			if (same(PackTheme(i))) {
				match = i;
			}
		}
		for (int i = 0; i < (int)customThemes.size() && match < 0; i++) {
			if (same(customThemes[(size_t)i])) {
				match = PACK_COUNT + i;
			}
		}
		const int customRow = packCombo->findData(-1);
		if (match < 0) {
			if (customRow < 0) {
				packCombo->addItem(T("Pack.Custom"), -1);
			}
			packCombo->setCurrentIndex(packCombo->findData(-1));
		} else {
			if (customRow >= 0) {
				packCombo->removeItem(customRow);
			}
			packCombo->setCurrentIndex(packCombo->findData(match));
		}
		deleteButton->setEnabled(packCombo->currentData().toInt() >= PACK_COUNT);
	}

	void refreshPacks()
	{
		packCombo->clear();
		for (int i = 0; i < PACK_COUNT; i++) {
			const Theme t = PackTheme(i);
			packCombo->addItem(Swatch(t), t.name, i);
		}
		for (int i = 0; i < (int)customThemes.size(); i++) {
			const Theme &t = customThemes[(size_t)i];
			packCombo->addItem(Swatch(t), QString::fromUtf8("\xE2\x98\x85 ") + t.name, PACK_COUNT + i);
		}
	}

	void choosePack(int row)
	{
		const int id = packCombo->itemData(row).toInt();
		if (id < 0) {
			return;
		}
		Theme t = id < PACK_COUNT ? PackTheme(id) : customThemes[(size_t)(id - PACK_COUNT)];
		if (t.logo.isEmpty()) {
			t.logo = logoEdit->text().trimmed(); /* le logo du projet est conservé */
		}
		setTheme(t);
		status->setText(T("Status.ThemeChosen"));
	}

	void pickColor(int i)
	{
		const QColor c = QColorDialog::getColor(colorsValue[i], this, T(i == 0   ? "Color.Primary"
										 : i == 1 ? "Color.Accent"
											  : "Color.Text"));
		if (!c.isValid()) {
			return;
		}
		colorsValue[i] = c;
		updateColorButtons();
		themeEdited();
	}

	/* Style « Épuré clair » : texte foncé sur fond blanc (et l'inverse en quittant ce style) */
	void lookChanged()
	{
		const bool light = lookCombo->currentData().toString() == "epure";
		const double lightness = colorsValue[2].lightnessF();
		if (light && lightness > 0.6) {
			colorsValue[2] = QColor("#1B2B26");
			updateColorButtons();
		} else if (!light && lightness < 0.4) {
			colorsValue[2] = QColor("#FFFFFF");
			updateColorButtons();
		}
		themeEdited();
	}

	void themeEdited()
	{
		const Theme t = theme();
		selectMatchingPack(t);
		SetGlobalTheme(&t);
		status->setText(T("Status.Edited"));
	}

	/* Met tous les habillages et transitions Amal du projet aux couleurs du thème */
	void applyToAll()
	{
		const Theme t = theme();
		SetGlobalTheme(&t);
		int count = 0;

		struct Ctx {
			const Theme *theme;
			int *count;
		} ctx{&t, &count};
		obs_enum_sources(
			[](void *param, obs_source_t *source) {
				Ctx *c = static_cast<Ctx *>(param);
				const char *id = obs_source_get_unversioned_id(source);
				if (id && strcmp(id, GRAPHIC_ID) == 0) {
					OBSDataAutoRelease settings = c->theme->toSettings(true);
					obs_source_update(source, settings);
					(*c->count)++;
				}
				return true;
			},
			&ctx);

		obs_frontend_source_list transitions = {};
		obs_frontend_get_transitions(&transitions);
		for (size_t i = 0; i < transitions.sources.num; i++) {
			obs_source_t *source = transitions.sources.array[i];
			const char *id = obs_source_get_unversioned_id(source);
			if (id && strcmp(id, TRANSITION_ID) == 0) {
				OBSDataAutoRelease settings = t.toSettings(false);
				obs_source_update(source, settings);
				count++;
			}
		}
		obs_frontend_source_list_free(&transitions);

		status->setText(count ? T("Status.Applied").arg(count) : T("Status.NothingToApply"));
	}

	/* ---------- Ajout d'un habillage ---------- */

	static QString UniqueName(const QString &base)
	{
		QString name = base;
		for (int n = 2; n < 100; n++) {
			OBSSourceAutoRelease existing = obs_get_source_by_name(name.toUtf8().constData());
			if (!existing) {
				break;
			}
			name = QString("%1 %2").arg(base).arg(n);
		}
		return name;
	}

	void addGraphic(const QuickGraphic &q)
	{
		OBSSourceAutoRelease sceneSource = obs_frontend_preview_program_mode_active()
							   ? obs_frontend_get_current_preview_scene()
							   : obs_frontend_get_current_scene();
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (!scene) {
			status->setText(T("Error.NoScene"));
			return;
		}

		const Theme t = theme();
		SetGlobalTheme(&t);
		OBSDataAutoRelease settings = t.toSettings(true);
		obs_data_set_string(settings, "modele", q.model);
		if (q.eventLayout) {
			obs_data_set_string(settings, "disposition_evt", q.eventLayout);
			if (strcmp(q.eventLayout, "lateral") == 0) {
				obs_data_set_string(settings, "libelle", T("Placeholder.SideTitle").toUtf8().constData());
				obs_data_set_string(settings, "texte", T("Placeholder.Cards").toUtf8().constData());
			}
			placeCamera(scene, EventWindow(q.eventLayout));
		}
		if (strcmp(q.model, "rappel") == 0) {
			/* Pas de texte religieux par défaut : l'utilisateur saisit le sien */
			obs_data_set_string(settings, "texte_arabe", "");
			obs_data_set_string(settings, "reference", "");
			obs_data_set_string(settings, "texte", T("Placeholder.Reminder").toUtf8().constData());
		}

		const QString name = UniqueName(T(q.nameKey));
		OBSSourceAutoRelease source = obs_source_create(GRAPHIC_ID, name.toUtf8().constData(), settings, nullptr);
		if (!source) {
			status->setText(T("Error.NoGraphics"));
			return;
		}
		obs_sceneitem_t *item = obs_scene_add(scene, source);
		if (item && q.bottom) {
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
		}
		status->setText(T("Status.Added").arg(name));
		if (q.openProperties) {
			obs_frontend_open_source_properties(source);
		}
	}

	/* Place la caméra choisie dans la fenêtre du cadre, recadrée pour la remplir */
	void placeCamera(obs_scene_t *scene, const Window &w)
	{
		const QString cameraName = cameraCombo->currentData().toString();
		if (cameraName.isEmpty()) {
			return;
		}
		OBSSourceAutoRelease camera = obs_get_source_by_name(cameraName.toUtf8().constData());
		if (!camera) {
			return;
		}
		obs_sceneitem_t *item = obs_scene_find_source(scene, cameraName.toUtf8().constData());
		if (!item) {
			item = obs_scene_add(scene, camera);
		}
		if (!item) {
			return;
		}
		obs_video_info ovi = {};
		double sx = 1.0, sy = 1.0;
		if (obs_get_video_info(&ovi) && ovi.base_width && ovi.base_height) {
			sx = ovi.base_width / 1920.0;
			sy = ovi.base_height / 1080.0;
		}
		vec2 pos;
		vec2 bounds;
		vec2_set(&pos, (float)(w.x * sx), (float)(w.y * sy));
		vec2_set(&bounds, (float)(w.w * sx), (float)(w.h * sy));
		obs_sceneitem_set_rot(item, 0.0f);
		obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_pos(item, &pos);
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_OUTER);
		obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
		obs_sceneitem_set_bounds(item, &bounds);
		obs_sceneitem_set_bounds_crop(item, true);
		obs_sceneitem_set_visible(item, true);
	}

	/* ---------- Événements et intro ---------- */

	static obs_scene_t *CurrentScene(OBSSourceAutoRelease &holder)
	{
		holder = obs_frontend_preview_program_mode_active() ? obs_frontend_get_current_preview_scene()
								    : obs_frontend_get_current_scene();
		return obs_scene_from_source(holder);
	}

	void addEventBackground()
	{
		OBSSourceAutoRelease holder;
		obs_scene_t *scene = CurrentScene(holder);
		if (!scene) {
			status->setText(T("Error.NoScene"));
			return;
		}
		const QString key = eventCombo->currentData().toString();
		const Theme t = theme();
		SetGlobalTheme(&t);
		OBSDataAutoRelease settings = t.toSettings(true);
		obs_data_set_string(settings, "modele", "fond");
		obs_data_set_string(settings, "fond_evenement", key.toUtf8().constData());
		obs_data_set_bool(settings, "salutation", !key.isEmpty());
		obs_data_set_string(settings, "texte_arabe", "");
		obs_data_set_string(settings, "titre", "");
		for (const EventBackground &e : EVENT_BACKGROUNDS) {
			if (key == e.key) {
				const std::string greet = std::string("Greeting.") + e.key;
				obs_data_set_string(settings, "texte_arabe", e.arabic);
				obs_data_set_string(settings, "titre", obs_module_text(greet.c_str()));
			}
		}
		const QString name = UniqueName(T("Name.EventBackground"));
		OBSSourceAutoRelease source = obs_source_create(GRAPHIC_ID, name.toUtf8().constData(), settings, nullptr);
		if (!source) {
			status->setText(T("Error.NoGraphics"));
			return;
		}
		obs_sceneitem_t *item = obs_scene_add(scene, source);
		if (item) {
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
		}
		status->setText(T("Status.Added").arg(name));
		obs_frontend_open_source_properties(source);
	}

	void createIntroScene()
	{
		const QString sceneName = UniqueName(T("Name.IntroScene"));
		OBSSceneAutoRelease scene = obs_scene_create(sceneName.toUtf8().constData());
		if (!scene) {
			status->setText(T("Error.NoScene"));
			return;
		}
		const Theme t = theme();
		SetGlobalTheme(&t);
		OBSDataAutoRelease settings = t.toSettings(true);
		obs_data_set_string(settings, "modele", "intro");
		obs_data_set_string(settings, "style_intro", introStyle->currentData().toString().toUtf8().constData());
		obs_data_set_int(settings, "duree_intro", introDuration->value());
		obs_data_set_string(settings, "scene_suivante", nextScene->currentData().toString().toUtf8().constData());
		obs_data_set_string(settings, "fond_evenement",
				    eventCombo->currentData().toString().toUtf8().constData());
		obs_data_set_string(settings, "texte_arabe", "");
		const QString name = UniqueName(T("Name.Intro"));
		OBSSourceAutoRelease source = obs_source_create(GRAPHIC_ID, name.toUtf8().constData(), settings, nullptr);
		if (!source) {
			status->setText(T("Error.NoGraphics"));
			return;
		}
		obs_scene_add(scene, source);
		introSceneName = sceneName;
		status->setText(T("Status.IntroCreated").arg(sceneName));
		obs_frontend_open_source_properties(source);
	}

	/* Scène d'intro à enregistrer : la dernière créée, sinon la première qui contient une intro */
	OBSSource findIntroScene(OBSSource &introSource)
	{
		OBSSource found;
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num && !found; i++) {
			obs_source_t *sceneSource = scenes.sources.array[i];
			if (!introSceneName.isEmpty() &&
			    introSceneName != QString::fromUtf8(obs_source_get_name(sceneSource))) {
				continue;
			}
			struct Search {
				obs_source_t *intro = nullptr;
			} search;
			obs_scene_enum_items(
				obs_scene_from_source(sceneSource),
				[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
					obs_source_t *s = obs_sceneitem_get_source(item);
					const char *id = obs_source_get_unversioned_id(s);
					if (id && strcmp(id, GRAPHIC_ID) == 0) {
						OBSDataAutoRelease st = obs_source_get_settings(s);
						if (strcmp(obs_data_get_string(st, "modele"), "intro") == 0) {
							static_cast<Search *>(param)->intro = s;
							return false;
						}
					}
					return true;
				},
				&search);
			if (search.intro) {
				found = sceneSource;
				introSource = search.intro;
			}
		}
		obs_frontend_source_list_free(&scenes);
		return found;
	}

	void recordIntro()
	{
		if (obs_frontend_recording_active()) {
			status->setText(T("Error.AlreadyRecording"));
			return;
		}
		OBSSource intro;
		OBSSource scene = findIntroScene(intro);
		if (!scene && !introSceneName.isEmpty()) {
			introSceneName.clear();
			scene = findIntroScene(intro);
		}
		if (!scene) {
			status->setText(T("Error.NoIntro"));
			return;
		}
		recordingIntro = intro;
		ShowOnProgram(scene);
		recordButton->setEnabled(false);
		status->setText(T("Status.Recording"));
		obs_frontend_recording_start();
	}

public:
	/* Enregistrement démarré : l'intro repart du début, puis l'enregistrement s'arrête à la fin */
	void recordingStarted()
	{
		OBSSource intro = recordingIntro;
		if (!intro) {
			return;
		}
		OBSDataAutoRelease restart = obs_data_create();
		obs_data_set_int(restart, "relance", (long long)(os_gettime_ns() / 1000000) % 1000000);
		obs_source_update(intro, restart);
		OBSDataAutoRelease settings = obs_source_get_settings(intro);
		const int ms = (int)obs_data_get_int(settings, "duree_intro") * 1000 + 900;
		QTimer::singleShot(ms, this, [] {
			if (obs_frontend_recording_active()) {
				obs_frontend_recording_stop();
			}
		});
	}

	void recordingStopped()
	{
		if (!recordingIntro) {
			return;
		}
		recordingIntro = nullptr;
		recordButton->setEnabled(true);
		char *file = obs_frontend_get_last_recording();
		status->setText(T("Status.Recorded").arg(QString::fromUtf8(file ? file : "")));
		bfree(file);
	}

private:
	/* ---------- Thèmes de l'utilisateur ---------- */

	static QString ThemesPath()
	{
		char *path = obs_module_config_path("themes.json");
		QString result = QString::fromUtf8(path ? path : "");
		bfree(path);
		return result;
	}

	void loadCustomThemes()
	{
		customThemes.clear();
		QFile file(ThemesPath());
		if (file.open(QIODevice::ReadOnly)) {
			for (const QJsonValue &v : QJsonDocument::fromJson(file.readAll()).array()) {
				customThemes.push_back(Theme::fromJson(v.toObject()));
			}
		}
	}

	void saveCustomThemes()
	{
		char *dir = obs_module_config_path("");
		if (dir) {
			os_mkdirs(dir);
			bfree(dir);
		}
		QJsonArray list;
		for (const Theme &t : customThemes) {
			list.append(t.toJson());
		}
		QSaveFile file(ThemesPath());
		if (file.open(QIODevice::WriteOnly)) {
			file.write(QJsonDocument(list).toJson());
			file.commit();
		}
	}

	void addCustomTheme(Theme t)
	{
		for (Theme &existing : customThemes) {
			if (existing.name == t.name) {
				existing = t;
				t.name.clear();
				break;
			}
		}
		if (!t.name.isEmpty()) {
			customThemes.push_back(t);
		}
		saveCustomThemes();
		const Theme current = theme();
		refreshPacks();
		selectMatchingPack(current);
	}

	void saveCurrentAsTheme()
	{
		bool ok = false;
		const QString name = QInputDialog::getText(this, T("SaveTheme"), T("SaveTheme.Name"), QLineEdit::Normal,
							   QString(), &ok)
					     .trimmed();
		if (!ok || name.isEmpty()) {
			return;
		}
		Theme t = theme();
		t.name = name;
		addCustomTheme(t);
		status->setText(T("Status.ThemeSaved").arg(name));
	}

	void deleteCustomTheme()
	{
		const int id = packCombo->currentData().toInt();
		if (id < PACK_COUNT) {
			return;
		}
		const QString name = customThemes[(size_t)(id - PACK_COUNT)].name;
		customThemes.erase(customThemes.begin() + (id - PACK_COUNT));
		saveCustomThemes();
		const Theme current = theme();
		refreshPacks();
		selectMatchingPack(current);
		status->setText(T("Status.ThemeDeleted").arg(name));
	}

	void exportTheme()
	{
		const QString file = QFileDialog::getSaveFileName(this, T("Export"), "theme.amaltheme",
								  T("ThemeFiles") + " (*.amaltheme)");
		if (file.isEmpty()) {
			return;
		}
		Theme t = theme();
		t.logo.clear(); /* le chemin du logo n'a de sens que sur cet ordinateur */
		if (t.name.isEmpty() || packCombo->currentData().toInt() < 0) {
			t.name = QFileInfo(file).completeBaseName();
		}
		QSaveFile out(file);
		if (out.open(QIODevice::WriteOnly)) {
			out.write(QJsonDocument(t.toJson()).toJson());
			if (out.commit()) {
				status->setText(T("Status.Exported"));
				return;
			}
		}
		status->setText(T("Error.File"));
	}

	void importTheme()
	{
		const QString file = QFileDialog::getOpenFileName(this, T("Import"), QString(),
								  T("ThemeFiles") + " (*.amaltheme *.json)");
		if (file.isEmpty()) {
			return;
		}
		QFile in(file);
		const QJsonObject o = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object()
								   : QJsonObject();
		if (o.isEmpty() || !o.contains("couleur_principale")) {
			status->setText(T("Error.File"));
			return;
		}
		Theme t = Theme::fromJson(o);
		t.logo.clear();
		if (t.name.isEmpty()) {
			t.name = QFileInfo(file).completeBaseName();
		}
		addCustomTheme(t);
		t.logo = logoEdit->text().trimmed();
		setTheme(t);
		status->setText(T("Status.Imported").arg(t.name));
	}

	QComboBox *packCombo = nullptr;
	QPushButton *colorButtons[3] = {};
	QColor colorsValue[3];
	QComboBox *lookCombo = nullptr;
	QCheckBox *motifCheck = nullptr;
	QLineEdit *logoEdit = nullptr;
	QPushButton *deleteButton = nullptr;
	QLabel *status = nullptr;
	VideoSourceCombo *cameraCombo = nullptr;
	QComboBox *eventCombo = nullptr;
	QComboBox *introStyle = nullptr;
	QSpinBox *introDuration = nullptr;
	SceneCombo *nextScene = nullptr;
	QPushButton *recordButton = nullptr;
	QString introSceneName;
	OBSSource recordingIntro;
	std::vector<Theme> customThemes;
};

/* ------------------------------------------------------------------------ */

void SaveCallback(obs_data_t *saveData, bool saving, void *)
{
	if (!dockWidget) {
		return;
	}
	if (saving) {
		OBSDataAutoRelease obj = obs_data_create();
		dockWidget->save(obj);
		obs_data_set_obj(saveData, SAVE_KEY, obj);
		return;
	}
	OBSDataAutoRelease obj = obs_data_get_obj(saveData, SAVE_KEY);
	dockWidget->load(obj);
}

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

/* Intro : à la fin, passage automatique à la scène choisie */
void IntroActivated(void *, calldata_t *cd)
{
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	const char *id = source ? obs_source_get_unversioned_id(source) : nullptr;
	if (!id || strcmp(id, GRAPHIC_ID) != 0) {
		return;
	}
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	if (strcmp(obs_data_get_string(settings, "modele"), "intro") != 0) {
		return;
	}
	const QString next = QString::fromUtf8(obs_data_get_string(settings, "scene_suivante"));
	if (next.isEmpty() || !dockWidget) {
		return;
	}
	const int ms = (int)obs_data_get_int(settings, "duree_intro") * 1000;
	OBSWeakSource weak = OBSGetWeakRef(source);
	QMetaObject::invokeMethod(
		dockWidget,
		[weak, next, ms] {
			QTimer::singleShot(ms, dockWidget, [weak, next] {
				OBSSource intro = OBSGetStrongRef(weak);
				if (!intro || !obs_source_active(intro)) {
					return; /* l'intro n'est plus à l'antenne : ne rien faire */
				}
				OBSSourceAutoRelease scene = obs_get_source_by_name(next.toUtf8().constData());
				if (scene && obs_scene_from_source(scene)) {
					ShowOnProgram(scene);
				}
			});
		},
		Qt::QueuedConnection);
}

void FrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED && dockWidget) {
		dockWidget->recordingStarted();
	} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED && dockWidget) {
		dockWidget->recordingStopped();
	}
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		ShowDockOnFirstRun();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP) {
		/* Le projet suivant ne doit pas hériter du thème de celui-ci pendant son chargement */
		SetGlobalTheme(nullptr);
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
	dockWidget = new StudioDock(scroll);
	scroll->setWidget(dockWidget);
	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), scroll)) {
		delete scroll;
		return false;
	}
	SetGlobalTheme(nullptr);
	signal_handler_connect(obs_get_signal_handler(), "source_activate", IntroActivated, nullptr);
	obs_frontend_add_save_callback(SaveCallback, nullptr);
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	blog(LOG_INFO, "[amal-habillage] Studio graphique chargé");
	return true;
}

void obs_module_unload(void)
{
	signal_handler_disconnect(obs_get_signal_handler(), "source_activate", IntroActivated, nullptr);
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	obs_frontend_remove_save_callback(SaveCallback, nullptr);
}
