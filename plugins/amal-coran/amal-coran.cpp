/******************************************************************************
    Amal Studio - module « Coran » (sourate par sourate, verset par verset)
    Copyright (C) 2026 Amal Ben Aly

    Texte coranique : Tanzil Quran Text (Uthmani), (C) Tanzil.net,
    Creative Commons Attribution 3.0 — https://tanzil.net
    Le fichier XML original est livré tel quel (bloc de licence compris) et
    lu à l'exécution : le texte n'est jamais modifié. Seuls des numéros de
    verset (﴿…﴾) sont ajoutés à l'affichage.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/platform.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QXmlStreamReader>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-coran", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : Coran complet (texte Tanzil)";
}

namespace {

constexpr const char *DOCK_ID = "amal-coran";
constexpr const char *SAVE_KEY = "amal_coran";
constexpr const char *GRAPHIC_ID = "amal_graphique";
constexpr int MAX_RESULTS = 200;

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* ------------------------------------------------------------------------ */
/* Données                                                                  */

struct Sura {
	QString arabicName;
	QString transliteration;
	QString bismillah;
	QStringList ayas;
	QStringList normalized; /* pour la recherche (sans voyelles) */
};

std::vector<Sura> quran;
QString textVersion;

/* Retire voyelles et signes, unifie les formes de alif et de ya : pour chercher sans tashkil */
QString Normalize(const QString &text)
{
	QString out;
	out.reserve(text.size());
	for (const QChar c : text) {
		const ushort u = c.unicode();
		if ((u >= 0x0610 && u <= 0x061A) || (u >= 0x064B && u <= 0x065F) || u == 0x0670 ||
		    (u >= 0x06D6 && u <= 0x06ED) || u == 0x0640) {
			continue;
		}
		if (u == 0x0671 || u == 0x0623 || u == 0x0625 || u == 0x0622) {
			out += QChar(0x0627);
		} else if (u == 0x0649) {
			out += QChar(0x064A);
		} else if (u == 0x0629) {
			out += QChar(0x0647);
		} else {
			out += c;
		}
	}
	return out.simplified();
}

QString ModuleFilePath(const char *file)
{
	char *relative = obs_module_file(file);
	if (!relative) {
		return QString();
	}
	char *absolute = os_get_abs_path_ptr(relative);
	QString path = QString::fromUtf8(absolute ? absolute : relative);
	bfree(absolute);
	bfree(relative);
	return path;
}

bool LoadQuran()
{
	quran.clear();

	QFile file(ModuleFilePath("tanzil/quran-uthmani.xml"));
	if (!file.open(QIODevice::ReadOnly)) {
		blog(LOG_ERROR, "[amal-coran] Texte coranique introuvable : %s", file.fileName().toUtf8().constData());
		return false;
	}

	QXmlStreamReader xml(&file);
	while (!xml.atEnd()) {
		xml.readNext();
		if (xml.isComment() && textVersion.isEmpty()) {
			QRegularExpression version("Tanzil Quran Text \\(([^)]*)\\)");
			QRegularExpressionMatch match = version.match(xml.text().toString());
			if (match.hasMatch()) {
				textVersion = match.captured(1);
			}
		}
		if (!xml.isStartElement()) {
			continue;
		}
		if (xml.name() == QLatin1String("sura")) {
			Sura sura;
			sura.arabicName = xml.attributes().value("name").toString();
			quran.push_back(sura);
		} else if (xml.name() == QLatin1String("aya") && !quran.empty()) {
			Sura &sura = quran.back();
			const QString text = xml.attributes().value("text").toString();
			if (sura.ayas.isEmpty()) {
				sura.bismillah = xml.attributes().value("bismillah").toString();
			}
			sura.ayas.append(text);
			sura.normalized.append(Normalize(text));
		}
	}

	if (xml.hasError() || quran.size() != 114) {
		blog(LOG_ERROR, "[amal-coran] Lecture du texte coranique incomplète (%zu sourates) : %s", quran.size(),
		     xml.errorString().toUtf8().constData());
		quran.clear();
		return false;
	}

	/* Noms translittérés (métadonnées Tanzil) */
	QFile meta(ModuleFilePath("tanzil/quran-data.xml"));
	if (meta.open(QIODevice::ReadOnly)) {
		QXmlStreamReader reader(&meta);
		while (!reader.atEnd()) {
			reader.readNext();
			if (reader.isStartElement() && reader.name() == QLatin1String("sura") &&
			    reader.attributes().hasAttribute("tname")) {
				const int index = reader.attributes().value("index").toInt();
				if (index >= 1 && index <= 114) {
					quran[(size_t)index - 1].transliteration =
						reader.attributes().value("tname").toString();
				}
			}
		}
	}

	size_t total = 0;
	for (const Sura &s : quran) {
		total += (size_t)s.ayas.size();
	}
	blog(LOG_INFO, "[amal-coran] Texte coranique chargé : %zu sourates, %zu versets (Tanzil %s)", quran.size(),
	     total, textVersion.toUtf8().constData());
	return total == 6236;
}

/* Chiffres arabes orientaux pour la numérotation des versets */
QString ArabicNumber(int n)
{
	QString digits = QString::number(n);
	for (QChar &c : digits) {
		c = QChar(0x0660 + (c.unicode() - '0'));
	}
	return digits;
}

/* ------------------------------------------------------------------------ */
/* Panneau « Coran »                                                        */

class QuranDock;
QPointer<QuranDock> dockWidget;
obs_hotkey_id nextHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id previousHotkey = OBS_INVALID_HOTKEY_ID;

class QuranDock : public QWidget {
public:
	explicit QuranDock(QWidget *parent) : QWidget(parent)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(8, 8, 8, 8);

		if (quran.empty()) {
			QLabel *error = new QLabel(T("Error.Load"), this);
			error->setWordWrap(true);
			layout->addWidget(error);
			layout->addStretch(1);
			return;
		}

		/* Recherche */
		search = new QLineEdit(this);
		search->setPlaceholderText(T("Search.Placeholder"));
		search->setClearButtonEnabled(true);
		layout->addWidget(search);
		results = new QListWidget(this);
		results->setVisible(false);
		results->setMinimumHeight(120);
		results->setLayoutDirection(Qt::RightToLeft);
		layout->addWidget(results);
		connect(search, &QLineEdit::returnPressed, this, &QuranDock::runSearch);
		connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
			if (text.trimmed().isEmpty()) {
				results->clear();
				results->setVisible(false);
			}
		});
		connect(results, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
			goTo(item->data(Qt::UserRole).toInt(), item->data(Qt::UserRole + 1).toInt());
		});

		/* Sourate et verset */
		suraCombo = new QComboBox(this);
		for (size_t i = 0; i < quran.size(); i++) {
			const Sura &s = quran[i];
			suraCombo->addItem(QString("%1. %2 — %3 (%4)")
						   .arg(i + 1)
						   .arg(s.transliteration, s.arabicName)
						   .arg(s.ayas.size()));
		}
		layout->addWidget(suraCombo);

		QHBoxLayout *ayaRow = new QHBoxLayout;
		ayaRow->addWidget(new QLabel(T("Verse"), this));
		ayaSpin = new QSpinBox(this);
		ayaSpin->setMinimum(1);
		ayaRow->addWidget(ayaSpin, 1);
		ayaRow->addWidget(new QLabel(T("Count"), this));
		countSpin = new QSpinBox(this);
		countSpin->setRange(1, 5);
		countSpin->setToolTip(T("Count.Help"));
		ayaRow->addWidget(countSpin);
		layout->addLayout(ayaRow);

		/* Aperçu */
		preview = new QLabel(this);
		preview->setWordWrap(true);
		preview->setTextFormat(Qt::PlainText);
		preview->setLayoutDirection(Qt::RightToLeft);
		preview->setAlignment(Qt::AlignRight | Qt::AlignTop);
		preview->setMinimumHeight(90);
		preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
		QFont arabic = preview->font();
		const QString family = LoadArabicFont();
		if (!family.isEmpty()) {
			arabic.setFamily(family);
		}
		arabic.setPointSizeF(arabic.pointSizeF() * 1.7);
		preview->setFont(arabic);
		layout->addWidget(preview, 1);

		reference = new QLabel(this);
		layout->addWidget(reference);

		/* Navigation */
		QHBoxLayout *nav = new QHBoxLayout;
		QPushButton *previous = new QPushButton(T("Previous"), this);
		QPushButton *show = new QPushButton(T("Show"), this);
		QPushButton *next = new QPushButton(T("Next"), this);
		show->setMinimumHeight(36);
		QFont bold = show->font();
		bold.setBold(true);
		show->setFont(bold);
		nav->addWidget(previous);
		nav->addWidget(show, 1);
		nav->addWidget(next);
		layout->addLayout(nav);

		autoSend = new QCheckBox(T("AutoSend"), this);
		autoSend->setChecked(true);
		layout->addWidget(autoSend);
		bismillahCheck = new QCheckBox(T("Bismillah"), this);
		layout->addWidget(bismillahCheck);

		status = new QLabel(this);
		status->setWordWrap(true);
		layout->addWidget(status);

		QLabel *source = new QLabel(T("Provenance").arg(textVersion), this);
		source->setWordWrap(true);
		source->setOpenExternalLinks(true);
		source->setTextFormat(Qt::RichText);
		QFont small = source->font();
		small.setPointSizeF(small.pointSizeF() * 0.85);
		source->setFont(small);
		layout->addWidget(source);

		connect(suraCombo, &QComboBox::currentIndexChanged, this, [this](int) {
			ayaSpin->blockSignals(true);
			ayaSpin->setMaximum((int)currentSura().ayas.size());
			ayaSpin->setValue(1);
			ayaSpin->blockSignals(false);
			changed();
		});
		connect(ayaSpin, &QSpinBox::valueChanged, this, [this](int) { changed(); });
		connect(countSpin, &QSpinBox::valueChanged, this, [this](int) { changed(); });
		connect(bismillahCheck, &QCheckBox::toggled, this, [this](bool) { changed(); });
		connect(previous, &QPushButton::clicked, this, &QuranDock::previousVerse);
		connect(next, &QPushButton::clicked, this, &QuranDock::nextVerse);
		connect(show, &QPushButton::clicked, this, &QuranDock::send);

		ayaSpin->setMaximum((int)currentSura().ayas.size());
		updatePreview();
	}

	bool ready() const { return suraCombo != nullptr; }

	void nextVerse()
	{
		if (!ready()) {
			return;
		}
		const int step = countSpin->value();
		if (ayaSpin->value() + step <= ayaSpin->maximum()) {
			ayaSpin->setValue(ayaSpin->value() + step);
		} else if (suraCombo->currentIndex() + 1 < suraCombo->count()) {
			suraCombo->setCurrentIndex(suraCombo->currentIndex() + 1);
		}
	}

	void previousVerse()
	{
		if (!ready()) {
			return;
		}
		const int step = countSpin->value();
		if (ayaSpin->value() - step >= 1) {
			ayaSpin->setValue(ayaSpin->value() - step);
		} else if (ayaSpin->value() > 1) {
			ayaSpin->setValue(1);
		} else if (suraCombo->currentIndex() > 0) {
			suraCombo->setCurrentIndex(suraCombo->currentIndex() - 1);
			ayaSpin->setValue(ayaSpin->maximum());
		}
	}

	void save(obs_data_t *obj) const
	{
		if (!ready()) {
			return;
		}
		obs_data_set_int(obj, "sourate", suraCombo->currentIndex() + 1);
		obs_data_set_int(obj, "verset", ayaSpin->value());
		obs_data_set_int(obj, "nombre", countSpin->value());
		obs_data_set_bool(obj, "auto", autoSend->isChecked());
		obs_data_set_bool(obj, "basmala", bismillahCheck->isChecked());
	}

	void load(obs_data_t *obj)
	{
		if (!ready() || !obj) {
			return;
		}
		loading = true;
		const int sura = (int)obs_data_get_int(obj, "sourate");
		const int aya = (int)obs_data_get_int(obj, "verset");
		if (obs_data_has_user_value(obj, "nombre")) {
			countSpin->setValue((int)obs_data_get_int(obj, "nombre"));
		}
		if (obs_data_has_user_value(obj, "auto")) {
			autoSend->setChecked(obs_data_get_bool(obj, "auto"));
		}
		bismillahCheck->setChecked(obs_data_get_bool(obj, "basmala"));
		if (sura >= 1 && sura <= 114) {
			goTo(sura, aya >= 1 ? aya : 1);
		}
		loading = false;
		status->clear();
	}

private:
	static QString LoadArabicFont()
	{
		obs_module_t *graphics = obs_get_module("amal-graphiques");
		if (!graphics) {
			return QString();
		}
		char *relative = obs_find_module_file(graphics, "fonts/AmiriQuran.ttf");
		if (!relative) {
			return QString();
		}
		char *absolute = os_get_abs_path_ptr(relative);
		const int id = QFontDatabase::addApplicationFont(QString::fromUtf8(absolute ? absolute : relative));
		bfree(absolute);
		bfree(relative);
		const QStringList families = QFontDatabase::applicationFontFamilies(id);
		return families.isEmpty() ? QString() : families.first();
	}

	const Sura &currentSura() const { return quran[(size_t)suraCombo->currentIndex()]; }

	void goTo(int sura, int aya)
	{
		suraCombo->setCurrentIndex(sura - 1);
		ayaSpin->setMaximum((int)currentSura().ayas.size());
		ayaSpin->setValue(std::min(aya, ayaSpin->maximum()));
		changed();
	}

	/* Versets affichés : texte original, numéros ajoutés après chaque verset */
	QString displayedText() const
	{
		const Sura &sura = currentSura();
		const int first = ayaSpin->value();
		const int last = std::min(first + countSpin->value() - 1, (int)sura.ayas.size());

		QStringList parts;
		for (int a = first; a <= last; a++) {
			parts << sura.ayas[a - 1] + QString::fromUtf8(" \xEF\xB4\xBF") + ArabicNumber(a) +
					 QString::fromUtf8("\xEF\xB4\xBE");
		}
		QString text = parts.join(' ');
		if (first == 1 && bismillahCheck->isChecked() && !sura.bismillah.isEmpty()) {
			text = sura.bismillah + "\n" + text;
		}
		return text;
	}

	QString displayedReference() const
	{
		const Sura &sura = currentSura();
		const int number = suraCombo->currentIndex() + 1;
		const int first = ayaSpin->value();
		const int last = std::min(first + countSpin->value() - 1, (int)sura.ayas.size());
		const QString range = last > first ? QString("%1-%2").arg(first).arg(last) : QString::number(first);
		return QString("%1 (%2:%3)").arg(sura.transliteration).arg(number).arg(range);
	}

	void updatePreview()
	{
		preview->setText(displayedText());
		reference->setText(displayedReference());
	}

	void changed()
	{
		updatePreview();
		if (autoSend->isChecked() && !loading) {
			send();
		}
	}

	void runSearch()
	{
		const QString query = search->text().trimmed();
		results->clear();
		if (query.isEmpty()) {
			results->setVisible(false);
			return;
		}

		/* Référence directe « 2:255 » */
		QRegularExpression refExpr("^(\\d{1,3})\\s*[:.,]\\s*(\\d{1,3})$");
		QRegularExpressionMatch ref = refExpr.match(query);
		if (ref.hasMatch()) {
			const int sura = ref.captured(1).toInt();
			const int aya = ref.captured(2).toInt();
			if (sura >= 1 && sura <= 114 && aya >= 1 && aya <= quran[(size_t)sura - 1].ayas.size()) {
				results->setVisible(false);
				goTo(sura, aya);
				return;
			}
		}

		const QString needle = Normalize(query);
		int found = 0;
		for (size_t s = 0; s < quran.size() && found < MAX_RESULTS; s++) {
			const Sura &sura = quran[s];
			for (int a = 0; a < sura.ayas.size() && found < MAX_RESULTS; a++) {
				if (!sura.normalized[a].contains(needle)) {
					continue;
				}
				QListWidgetItem *item = new QListWidgetItem(
					QString("(%1:%2) %3").arg(s + 1).arg(a + 1).arg(sura.ayas[a]), results);
				item->setData(Qt::UserRole, (int)s + 1);
				item->setData(Qt::UserRole + 1, a + 1);
				found++;
			}
		}
		if (found == 0) {
			new QListWidgetItem(T("Search.None"), results);
		}
		results->setVisible(true);
		status->setText(found >= MAX_RESULTS ? T("Search.Many").arg(MAX_RESULTS)
						     : T("Search.Count").arg(found));
	}

	/* Envoi vers la source « Amal · Coran » de la scène affichée */
	void send()
	{
		const QString name = T("Source.Name");
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "modele", "verset");
		obs_data_set_string(settings, "texte_arabe", displayedText().toUtf8().constData());
		obs_data_set_string(settings, "reference", displayedReference().toUtf8().constData());
		obs_data_set_string(settings, "texte", "");
		obs_data_set_string(settings, "libelle", "");

		OBSSourceAutoRelease source = obs_get_source_by_name(name.toUtf8().constData());
		if (source) {
			const char *id = obs_source_get_unversioned_id(source);
			if (!id || strcmp(id, GRAPHIC_ID) != 0) {
				status->setText(T("Error.NameTaken").arg(name));
				return;
			}
			obs_source_update(source, settings);
		} else {
			source = obs_source_create(GRAPHIC_ID, name.toUtf8().constData(), settings, nullptr);
			if (!source) {
				status->setText(T("Error.NoGraphics"));
				return;
			}
		}

		/* En mode studio, le verset est préparé dans l'aperçu */
		OBSSourceAutoRelease sceneSource = obs_frontend_preview_program_mode_active()
							   ? obs_frontend_get_current_preview_scene()
							   : obs_frontend_get_current_scene();
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (scene && !obs_scene_find_source(scene, name.toUtf8().constData())) {
			obs_scene_add(scene, source);
		}
		status->setText(T("Status.Shown").arg(displayedReference()));
	}

	QLineEdit *search = nullptr;
	QListWidget *results = nullptr;
	QComboBox *suraCombo = nullptr;
	QSpinBox *ayaSpin = nullptr;
	QSpinBox *countSpin = nullptr;
	QLabel *preview = nullptr;
	QLabel *reference = nullptr;
	QCheckBox *autoSend = nullptr;
	QCheckBox *bismillahCheck = nullptr;
	QLabel *status = nullptr;
	bool loading = false;
};

/* ------------------------------------------------------------------------ */
/* Lien avec le moteur                                                      */

void HotkeyNext(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && dockWidget) {
		QMetaObject::invokeMethod(dockWidget, [] {
			if (dockWidget) {
				dockWidget->nextVerse();
			}
		}, Qt::QueuedConnection);
	}
}

void HotkeyPrevious(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && dockWidget) {
		QMetaObject::invokeMethod(dockWidget, [] {
			if (dockWidget) {
				dockWidget->previousVerse();
			}
		}, Qt::QueuedConnection);
	}
}

void SaveCallback(obs_data_t *saveData, bool saving, void *)
{
	if (saving) {
		OBSDataAutoRelease obj = obs_data_create();
		if (dockWidget) {
			dockWidget->save(obj);
		}
		OBSDataArrayAutoRelease next = obs_hotkey_save(nextHotkey);
		OBSDataArrayAutoRelease previous = obs_hotkey_save(previousHotkey);
		obs_data_set_array(obj, "raccourci_suivant", next);
		obs_data_set_array(obj, "raccourci_precedent", previous);
		obs_data_set_obj(saveData, SAVE_KEY, obj);
		return;
	}

	OBSDataAutoRelease obj = obs_data_get_obj(saveData, SAVE_KEY);
	if (!obj) {
		return;
	}
	OBSDataArrayAutoRelease next = obs_data_get_array(obj, "raccourci_suivant");
	OBSDataArrayAutoRelease previous = obs_data_get_array(obj, "raccourci_precedent");
	obs_hotkey_load(nextHotkey, next);
	obs_hotkey_load(previousHotkey, previous);
	if (dockWidget) {
		dockWidget->load(obj);
	}
}

} // namespace

bool obs_module_load(void)
{
	LoadQuran();

	QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	QScrollArea *scroll = new QScrollArea(main);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	dockWidget = new QuranDock(scroll);
	scroll->setWidget(dockWidget);

	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), scroll)) {
		delete scroll;
		return false;
	}

	nextHotkey = obs_hotkey_register_frontend("amal_coran_suivant", obs_module_text("Hotkey.Next"), HotkeyNext,
						  nullptr);
	previousHotkey = obs_hotkey_register_frontend("amal_coran_precedent", obs_module_text("Hotkey.Previous"),
						      HotkeyPrevious, nullptr);

	obs_frontend_add_save_callback(SaveCallback, nullptr);
	blog(LOG_INFO, "[amal-coran] Module Coran chargé");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_save_callback(SaveCallback, nullptr);
	obs_hotkey_unregister(nextHotkey);
	obs_hotkey_unregister(previousHotkey);
}
