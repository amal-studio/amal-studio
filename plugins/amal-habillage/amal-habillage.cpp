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
};

const QuickGraphic QUICK[] = {
	{"titre", "Quick.Title", "Name.Title", false, true},
	{"intervenant", "Quick.Speaker", "Name.Speaker", false, true},
	{"direct", "Quick.Broadcast", "Name.Broadcast", false, true},
	{"bandeau", "Quick.Ticker", "Name.Ticker", false, true},
	{"rappel", "Quick.Reminder", "Name.Reminder", false, true},
	{"fond", "Quick.Background", "Name.Background", true, false},
};

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

void FrontendEvent(enum obs_frontend_event event, void *)
{
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
	obs_frontend_add_save_callback(SaveCallback, nullptr);
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	blog(LOG_INFO, "[amal-habillage] Studio graphique chargé");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	obs_frontend_remove_save_callback(SaveCallback, nullptr);
}
