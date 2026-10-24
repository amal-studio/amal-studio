/******************************************************************************
    Amal Studio - module « Bibliothèque » (hadiths, séquence, mes textes)
    Copyright (C) 2026 Amal Ben Aly

    - Hadiths : sept recueils en arabe (projet hadith-api, domaine public),
      lus tels quels ; les degrés affichés sont ceux de la source, avec le
      nom du savant. Aucun degré n'est calculé ni inventé.
    - Séquence : liste préparée à l'avance (versets, hadiths, textes), que
      l'on fait défiler à l'écran avec « Suivant » ou un raccourci clavier.
      Enregistrée avec le projet.
    - Mes textes : invocations et rappels saisis par l'utilisateur, qui en
      garde la responsabilité (texte et référence affichés tels quels).

    Tous les textes s'affichent dans une même source : « Amal · Citation ».

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

#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-bibliotheque", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : bibliothèque (hadiths, séquence, mes textes)";
}

namespace {

constexpr const char *DOCK_ID = "amal-bibliotheque";
constexpr const char *SAVE_KEY = "amal_bibliotheque";
constexpr const char *CONFIG_SECTION = "AmalBibliotheque";
constexpr const char *GRAPHIC_ID = "amal_graphique";
constexpr int MAX_RESULTS = 200;
constexpr int LONG_TEXT = 900; /* au-delà, le texte devient petit à l'écran */

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* ------------------------------------------------------------------------ */
/* Élément affichable (verset, hadith, invocation, rappel)                  */

struct Item {
	QString type; /* verset | hadith | invocation | rappel */
	QString label;
	QString arabic;
	QString french;
	QString reference;

	QJsonObject toJson() const
	{
		return QJsonObject{{"type", type}, {"libelle", label}, {"arabe", arabic}, {"francais", french},
				   {"reference", reference}};
	}

	static Item fromJson(const QJsonObject &o)
	{
		return Item{o.value("type").toString("rappel"), o.value("libelle").toString(),
			    o.value("arabe").toString(), o.value("francais").toString(),
			    o.value("reference").toString()};
	}

	QString summary() const
	{
		const QString text = !arabic.isEmpty() ? arabic : french;
		QString head = reference.isEmpty() ? text.left(60) : reference;
		return QString("[%1] %2").arg(TypeName(type), head);
	}

	static QString TypeName(const QString &t)
	{
		if (t == "verset")
			return T("Type.Verse");
		if (t == "hadith")
			return T("Type.Hadith");
		if (t == "invocation")
			return T("Type.Invocation");
		return T("Type.Reminder");
	}
};

/* Affiche un élément dans la source commune « Amal · Citation » */
QString ShowItem(const Item &item)
{
	const QString name = T("Source.Name");
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "modele", item.type.toUtf8().constData());
	obs_data_set_string(settings, "libelle", item.label.toUtf8().constData());
	obs_data_set_string(settings, "texte_arabe", item.arabic.toUtf8().constData());
	obs_data_set_string(settings, "texte", item.french.toUtf8().constData());
	obs_data_set_string(settings, "reference", item.reference.toUtf8().constData());

	OBSSourceAutoRelease source = obs_get_source_by_name(name.toUtf8().constData());
	if (source) {
		const char *id = obs_source_get_unversioned_id(source);
		if (!id || strcmp(id, GRAPHIC_ID) != 0) {
			return T("Error.NameTaken").arg(name);
		}
		obs_source_update(source, settings);
	} else {
		source = obs_source_create(GRAPHIC_ID, name.toUtf8().constData(), settings, nullptr);
		if (!source) {
			return T("Error.NoGraphics");
		}
	}

	OBSSourceAutoRelease sceneSource = obs_frontend_preview_program_mode_active()
						   ? obs_frontend_get_current_preview_scene()
						   : obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (scene && !obs_scene_find_source(scene, name.toUtf8().constData())) {
		obs_scene_add(scene, source);
	}
	return T("Status.Shown").arg(item.reference.isEmpty() ? Item::TypeName(item.type) : item.reference);
}

/* ------------------------------------------------------------------------ */
/* Hadiths                                                                  */

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

struct Hadith {
	QString number;
	QString text;
	QString normalized;
	QStringList grades; /* « Savant : degré », tels que fournis par la source */
	QString albani;     /* degré d'Al-Albani s'il est fourni */
};

struct Book {
	const char *file;
	const char *nameKey;
	std::vector<Hadith> hadiths;
	bool loaded = false;
};

std::array<Book, 7> books{{{"ara-bukhari", "Book.Bukhari", {}, false},
			   {"ara-muslim", "Book.Muslim", {}, false},
			   {"ara-abudawud", "Book.AbuDawud", {}, false},
			   {"ara-tirmidhi", "Book.Tirmidhi", {}, false},
			   {"ara-nasai", "Book.Nasai", {}, false},
			   {"ara-ibnmajah", "Book.IbnMajah", {}, false},
			   {"ara-malik", "Book.Malik", {}, false}}};

QString ModuleFilePath(const QString &file)
{
	char *relative = obs_module_file(file.toUtf8().constData());
	if (!relative) {
		return QString();
	}
	char *absolute = os_get_abs_path_ptr(relative);
	QString path = QString::fromUtf8(absolute ? absolute : relative);
	bfree(absolute);
	bfree(relative);
	return path;
}

bool LoadBook(Book &book)
{
	if (book.loaded) {
		return !book.hadiths.empty();
	}
	book.loaded = true;

	QFile file(ModuleFilePath(QString("hadiths/%1.min.json").arg(book.file)));
	if (!file.open(QIODevice::ReadOnly)) {
		blog(LOG_ERROR, "[amal-bibliotheque] Recueil introuvable : %s", book.file);
		return false;
	}
	QJsonParseError error{};
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError) {
		blog(LOG_ERROR, "[amal-bibliotheque] Recueil illisible (%s) : %s", book.file,
		     error.errorString().toUtf8().constData());
		return false;
	}

	const QJsonArray list = doc.object().value("hadiths").toArray();
	book.hadiths.reserve((size_t)list.size());
	for (const QJsonValue &value : list) {
		const QJsonObject o = value.toObject();
		Hadith h;
		h.text = o.value("text").toString().trimmed();
		if (h.text.isEmpty()) {
			continue;
		}
		h.number = QString::number(o.value("hadithnumber").toDouble(), 'g', 10);
		h.normalized = Normalize(h.text);
		for (const QJsonValue &g : o.value("grades").toArray()) {
			const QString who = g.toObject().value("name").toString();
			const QString grade = g.toObject().value("grade").toString();
			if (grade.isEmpty()) {
				continue;
			}
			h.grades << QString("%1 : %2").arg(who, grade);
			if (who == "Al-Albani") {
				h.albani = grade;
			}
		}
		book.hadiths.push_back(std::move(h));
	}
	blog(LOG_INFO, "[amal-bibliotheque] Recueil chargé : %s (%zu hadiths)", book.file, book.hadiths.size());
	return !book.hadiths.empty();
}

/* ------------------------------------------------------------------------ */
/* Panneau « Bibliothèque »                                                 */

class LibraryDock;
QPointer<LibraryDock> dockWidget;
obs_hotkey_id nextHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id previousHotkey = OBS_INVALID_HOTKEY_ID;

class LibraryDock : public QWidget {
public:
	explicit LibraryDock(QWidget *parent) : QWidget(parent)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(4, 4, 4, 4);
		tabs = new QTabWidget(this);
		tabs->addTab(buildHadithTab(), T("Tab.Hadiths"));
		tabs->addTab(buildSequenceTab(), T("Tab.Sequence"));
		tabs->addTab(buildTextsTab(), T("Tab.Texts"));
		layout->addWidget(tabs);

		status = new QLabel(this);
		status->setWordWrap(true);
		layout->addWidget(status);

		loadTexts();
	}

	/* --- Séquence (appelée aussi depuis le panneau Coran) --- */

	void addToSequence(const Item &item)
	{
		sequence.push_back(item);
		refreshSequence();
		sequenceList->setCurrentRow((int)sequence.size() - 1);
		status->setText(T("Status.Added").arg(item.reference.isEmpty() ? Item::TypeName(item.type)
										: item.reference));
	}

	void sequenceStep(int delta)
	{
		if (sequence.empty()) {
			status->setText(T("Sequence.Empty"));
			return;
		}
		int row = sequenceList->currentRow();
		row = row < 0 ? 0 : std::clamp(row + delta, 0, (int)sequence.size() - 1);
		sequenceList->setCurrentRow(row);
		showSequenceRow(row);
	}

	void save(obs_data_t *obj) const
	{
		QJsonArray list;
		for (const Item &item : sequence) {
			list.append(item.toJson());
		}
		obs_data_set_string(obj, "sequence",
				    QJsonDocument(list).toJson(QJsonDocument::Compact).constData());
		obs_data_set_int(obj, "recueil", bookCombo->currentIndex());
		obs_data_set_int(obj, "degre", gradeCombo->currentIndex());
	}

	void load(obs_data_t *obj)
	{
		sequence.clear();
		if (obj) {
			const QJsonArray list =
				QJsonDocument::fromJson(QByteArray(obs_data_get_string(obj, "sequence"))).array();
			for (const QJsonValue &v : list) {
				sequence.push_back(Item::fromJson(v.toObject()));
			}
			if (obs_data_has_user_value(obj, "degre")) {
				gradeCombo->setCurrentIndex((int)obs_data_get_int(obj, "degre"));
			}
		}
		refreshSequence();
	}

private:
	/* ======================== Onglet Hadiths ======================== */

	QWidget *buildHadithTab()
	{
		QWidget *page = new QWidget(this);
		QVBoxLayout *layout = new QVBoxLayout(page);

		bookCombo = new QComboBox(page);
		for (const Book &book : books) {
			bookCombo->addItem(T(book.nameKey));
		}
		layout->addWidget(bookCombo);

		search = new QLineEdit(page);
		search->setPlaceholderText(T("Search.Placeholder"));
		search->setClearButtonEnabled(true);
		layout->addWidget(search);
		results = new QListWidget(page);
		results->setVisible(false);
		results->setMinimumHeight(120);
		results->setLayoutDirection(Qt::RightToLeft);
		layout->addWidget(results);

		QHBoxLayout *nav = new QHBoxLayout;
		QPushButton *previous = new QPushButton(QString::fromUtf8("\xE2\x97\x80"), page);
		indexSpin = new QSpinBox(page);
		indexSpin->setMinimum(1);
		indexSpin->setPrefix(T("Position.Prefix"));
		QPushButton *next = new QPushButton(QString::fromUtf8("\xE2\x96\xB6"), page);
		previous->setFixedWidth(40);
		next->setFixedWidth(40);
		nav->addWidget(previous);
		nav->addWidget(indexSpin, 1);
		nav->addWidget(next);
		layout->addLayout(nav);

		hadithText = new QLabel(page);
		hadithText->setWordWrap(true);
		hadithText->setTextFormat(Qt::PlainText);
		hadithText->setLayoutDirection(Qt::RightToLeft);
		hadithText->setAlignment(Qt::AlignRight | Qt::AlignTop);
		hadithText->setTextInteractionFlags(Qt::TextSelectableByMouse);
		hadithText->setMinimumHeight(120);
		QFont arabic = hadithText->font();
		const QString family = LoadArabicFont();
		if (!family.isEmpty()) {
			arabic.setFamily(family);
		}
		arabic.setPointSizeF(arabic.pointSizeF() * 1.4);
		hadithText->setFont(arabic);
		layout->addWidget(hadithText, 1);

		hadithRef = new QLabel(page);
		hadithRef->setWordWrap(true);
		layout->addWidget(hadithRef);
		gradesLabel = new QLabel(page);
		gradesLabel->setWordWrap(true);
		layout->addWidget(gradesLabel);

		QHBoxLayout *gradeRow = new QHBoxLayout;
		gradeRow->addWidget(new QLabel(T("Grade.OnScreen"), page));
		gradeCombo = new QComboBox(page);
		gradeCombo->addItem(T("Grade.None"));
		gradeCombo->addItem(T("Grade.Albani"));
		gradeCombo->addItem(T("Grade.All"));
		gradeCombo->setCurrentIndex(1);
		gradeRow->addWidget(gradeCombo, 1);
		layout->addLayout(gradeRow);

		QHBoxLayout *actions = new QHBoxLayout;
		QPushButton *show = new QPushButton(T("Show"), page);
		QPushButton *add = new QPushButton(T("AddToSequence"), page);
		QFont bold = show->font();
		bold.setBold(true);
		show->setFont(bold);
		actions->addWidget(show, 1);
		actions->addWidget(add);
		layout->addLayout(actions);

		QLabel *source = new QLabel(T("Hadith.Provenance"), page);
		source->setWordWrap(true);
		source->setOpenExternalLinks(true);
		source->setTextFormat(Qt::RichText);
		QFont small = source->font();
		small.setPointSizeF(small.pointSizeF() * 0.85);
		source->setFont(small);
		layout->addWidget(source);

		connect(bookCombo, &QComboBox::currentIndexChanged, this, [this](int) { selectBook(); });
		connect(indexSpin, &QSpinBox::valueChanged, this, [this](int) { updateHadith(); });
		connect(previous, &QPushButton::clicked, this, [this] { indexSpin->setValue(indexSpin->value() - 1); });
		connect(next, &QPushButton::clicked, this, [this] { indexSpin->setValue(indexSpin->value() + 1); });
		connect(search, &QLineEdit::returnPressed, this, &LibraryDock::runSearch);
		connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
			if (text.trimmed().isEmpty()) {
				results->clear();
				results->setVisible(false);
			}
		});
		connect(results, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
			const int index = item->data(Qt::UserRole).toInt();
			if (index > 0) {
				indexSpin->setValue(index);
			}
		});
		connect(show, &QPushButton::clicked, this, [this] { status->setText(ShowItem(currentHadithItem())); });
		connect(add, &QPushButton::clicked, this, [this] { addToSequence(currentHadithItem()); });

		selectBook();
		return page;
	}

	static QString LoadArabicFont()
	{
		obs_module_t *graphics = obs_get_module("amal-graphiques");
		char *relative = graphics ? obs_find_module_file(graphics, "fonts/Amiri-Regular.ttf") : nullptr;
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

	Book &currentBook() { return books[(size_t)bookCombo->currentIndex()]; }

	void selectBook()
	{
		Book &book = currentBook();
		if (!LoadBook(book)) {
			hadithText->setText(T("Error.Load"));
			indexSpin->setMaximum(1);
			return;
		}
		indexSpin->blockSignals(true);
		indexSpin->setMaximum((int)book.hadiths.size());
		indexSpin->setSuffix(QString(" / %1").arg(book.hadiths.size()));
		indexSpin->setValue(1);
		indexSpin->blockSignals(false);
		results->clear();
		results->setVisible(false);
		updateHadith();
	}

	const Hadith *currentHadith()
	{
		Book &book = currentBook();
		const int index = indexSpin->value() - 1;
		if (index < 0 || index >= (int)book.hadiths.size()) {
			return nullptr;
		}
		return &book.hadiths[(size_t)index];
	}

	void updateHadith()
	{
		const Hadith *h = currentHadith();
		if (!h) {
			return;
		}
		hadithText->setText(h->text);
		QString ref = T("Hadith.Reference").arg(T(currentBook().nameKey), h->number);
		if (h->text.size() > LONG_TEXT) {
			ref += "\n" + T("Hadith.Long");
		}
		hadithRef->setText(ref);
		gradesLabel->setText(h->grades.isEmpty() ? T("Grade.NoneInSource")
							 : T("Grade.FromSource").arg(h->grades.join(QString::fromUtf8(" · "))));
	}

	Item currentHadithItem()
	{
		const Hadith *h = currentHadith();
		if (!h) {
			return Item{"hadith", QString(), QString(), QString(), QString()};
		}
		QString reference = T("Hadith.Reference").arg(T(currentBook().nameKey), h->number);
		if (gradeCombo->currentIndex() == 1 && !h->albani.isEmpty()) {
			reference += QString::fromUtf8(" \xC2\xB7 ") + T("Grade.AlbaniShort").arg(h->albani);
		} else if (gradeCombo->currentIndex() == 2 && !h->grades.isEmpty()) {
			reference += QString::fromUtf8(" \xC2\xB7 ") + h->grades.join(QString::fromUtf8(" \xC2\xB7 "));
		}
		return Item{"hadith", QString(), h->text, QString(), reference};
	}

	void runSearch()
	{
		const QString query = search->text().trimmed();
		results->clear();
		if (query.isEmpty()) {
			results->setVisible(false);
			return;
		}
		Book &book = currentBook();

		/* Numéro de hadith */
		bool isNumber = false;
		query.toDouble(&isNumber);
		if (isNumber) {
			for (size_t i = 0; i < book.hadiths.size(); i++) {
				if (book.hadiths[i].number == query) {
					results->setVisible(false);
					indexSpin->setValue((int)i + 1);
					return;
				}
			}
			status->setText(T("Search.NoNumber").arg(query));
			return;
		}

		const QString needle = Normalize(query);
		int found = 0;
		for (size_t i = 0; i < book.hadiths.size() && found < MAX_RESULTS; i++) {
			const Hadith &h = book.hadiths[i];
			if (!h.normalized.contains(needle)) {
				continue;
			}
			QListWidgetItem *item =
				new QListWidgetItem(QString("(%1) %2").arg(h.number, h.text.left(90)), results);
			item->setData(Qt::UserRole, (int)i + 1);
			found++;
		}
		if (!found) {
			new QListWidgetItem(T("Search.None"), results);
		}
		results->setVisible(true);
		status->setText(found >= MAX_RESULTS ? T("Search.Many").arg(MAX_RESULTS)
						     : T("Search.Count").arg(found));
	}

	/* ======================== Onglet Séquence ======================== */

	QWidget *buildSequenceTab()
	{
		QWidget *page = new QWidget(this);
		QVBoxLayout *layout = new QVBoxLayout(page);

		QLabel *help = new QLabel(T("Sequence.Help"), page);
		help->setWordWrap(true);
		layout->addWidget(help);

		sequenceList = new QListWidget(page);
		sequenceList->setMinimumHeight(160);
		layout->addWidget(sequenceList, 1);

		QHBoxLayout *edit = new QHBoxLayout;
		QPushButton *up = new QPushButton(QString::fromUtf8("\xE2\x96\xB2"), page);
		QPushButton *down = new QPushButton(QString::fromUtf8("\xE2\x96\xBC"), page);
		QPushButton *remove = new QPushButton(T("Remove"), page);
		QPushButton *clear = new QPushButton(T("Clear"), page);
		edit->addWidget(up);
		edit->addWidget(down);
		edit->addWidget(remove);
		edit->addWidget(clear);
		layout->addLayout(edit);

		QHBoxLayout *nav = new QHBoxLayout;
		QPushButton *previous = new QPushButton(T("Previous"), page);
		QPushButton *show = new QPushButton(T("Show"), page);
		QPushButton *next = new QPushButton(T("Next"), page);
		show->setMinimumHeight(36);
		QFont bold = show->font();
		bold.setBold(true);
		show->setFont(bold);
		nav->addWidget(previous);
		nav->addWidget(show, 1);
		nav->addWidget(next);
		layout->addLayout(nav);

		connect(up, &QPushButton::clicked, this, [this] { moveSelected(-1); });
		connect(down, &QPushButton::clicked, this, [this] { moveSelected(1); });
		connect(remove, &QPushButton::clicked, this, [this] {
			const int row = sequenceList->currentRow();
			if (row >= 0 && row < (int)sequence.size()) {
				sequence.erase(sequence.begin() + row);
				refreshSequence();
				sequenceList->setCurrentRow(std::min(row, (int)sequence.size() - 1));
			}
		});
		connect(clear, &QPushButton::clicked, this, [this] {
			sequence.clear();
			refreshSequence();
		});
		connect(previous, &QPushButton::clicked, this, [this] { sequenceStep(-1); });
		connect(next, &QPushButton::clicked, this, [this] { sequenceStep(1); });
		connect(show, &QPushButton::clicked, this, [this] { showSequenceRow(sequenceList->currentRow()); });
		connect(sequenceList, &QListWidget::itemDoubleClicked, this,
			[this](QListWidgetItem *) { showSequenceRow(sequenceList->currentRow()); });
		return page;
	}

	void refreshSequence()
	{
		const int row = sequenceList->currentRow();
		sequenceList->clear();
		for (size_t i = 0; i < sequence.size(); i++) {
			sequenceList->addItem(QString("%1. %2").arg(i + 1).arg(sequence[i].summary()));
		}
		if (row >= 0 && row < (int)sequence.size()) {
			sequenceList->setCurrentRow(row);
		}
	}

	void moveSelected(int delta)
	{
		const int row = sequenceList->currentRow();
		const int target = row + delta;
		if (row < 0 || target < 0 || target >= (int)sequence.size()) {
			return;
		}
		std::swap(sequence[(size_t)row], sequence[(size_t)target]);
		refreshSequence();
		sequenceList->setCurrentRow(target);
	}

	void showSequenceRow(int row)
	{
		if (row < 0 || row >= (int)sequence.size()) {
			return;
		}
		status->setText(QString("%1/%2 · ").arg(row + 1).arg(sequence.size()) +
				ShowItem(sequence[(size_t)row]));
	}

	/* ======================== Onglet Mes textes ======================== */

	QWidget *buildTextsTab()
	{
		QWidget *page = new QWidget(this);
		QVBoxLayout *layout = new QVBoxLayout(page);

		QLabel *warning = new QLabel(T("Texts.Warning"), page);
		warning->setWordWrap(true);
		layout->addWidget(warning);

		QFormLayout *form = new QFormLayout;
		textType = new QComboBox(page);
		textType->addItem(T("Type.Invocation"), "invocation");
		textType->addItem(T("Type.Reminder"), "rappel");
		textLabel = new QLineEdit(page);
		textArabic = new QPlainTextEdit(page);
		textArabic->setFixedHeight(70);
		textArabic->setLayoutDirection(Qt::RightToLeft);
		textFrench = new QPlainTextEdit(page);
		textFrench->setFixedHeight(60);
		textReference = new QLineEdit(page);
		form->addRow(T("Field.Type"), textType);
		form->addRow(T("Field.Label"), textLabel);
		form->addRow(T("Field.Arabic"), textArabic);
		form->addRow(T("Field.French"), textFrench);
		form->addRow(T("Field.Reference"), textReference);
		layout->addLayout(form);

		QPushButton *save = new QPushButton(T("Texts.Save"), page);
		layout->addWidget(save);

		textsList = new QListWidget(page);
		textsList->setMinimumHeight(110);
		layout->addWidget(textsList, 1);

		QHBoxLayout *actions = new QHBoxLayout;
		QPushButton *show = new QPushButton(T("Show"), page);
		QPushButton *add = new QPushButton(T("AddToSequence"), page);
		QPushButton *remove = new QPushButton(T("Remove"), page);
		actions->addWidget(show, 1);
		actions->addWidget(add);
		actions->addWidget(remove);
		layout->addLayout(actions);

		connect(save, &QPushButton::clicked, this, [this] {
			Item item{textType->currentData().toString(), textLabel->text().trimmed(),
				  textArabic->toPlainText().trimmed(), textFrench->toPlainText().trimmed(),
				  textReference->text().trimmed()};
			if (item.arabic.isEmpty() && item.french.isEmpty()) {
				status->setText(T("Texts.EmptyError"));
				return;
			}
			texts.push_back(item);
			saveTexts();
			refreshTexts();
			textArabic->clear();
			textFrench->clear();
			textReference->clear();
			textLabel->clear();
			status->setText(T("Texts.Saved"));
		});
		connect(show, &QPushButton::clicked, this, [this] {
			const int row = textsList->currentRow();
			if (row >= 0 && row < (int)texts.size()) {
				status->setText(ShowItem(texts[(size_t)row]));
			}
		});
		connect(add, &QPushButton::clicked, this, [this] {
			const int row = textsList->currentRow();
			if (row >= 0 && row < (int)texts.size()) {
				addToSequence(texts[(size_t)row]);
			}
		});
		connect(remove, &QPushButton::clicked, this, [this] {
			const int row = textsList->currentRow();
			if (row >= 0 && row < (int)texts.size()) {
				texts.erase(texts.begin() + row);
				saveTexts();
				refreshTexts();
			}
		});
		return page;
	}

	static QString TextsPath()
	{
		char *path = obs_module_config_path("mes-textes.json");
		QString result = QString::fromUtf8(path ? path : "");
		bfree(path);
		return result;
	}

	void loadTexts()
	{
		texts.clear();
		QFile file(TextsPath());
		if (file.open(QIODevice::ReadOnly)) {
			for (const QJsonValue &v : QJsonDocument::fromJson(file.readAll()).array()) {
				texts.push_back(Item::fromJson(v.toObject()));
			}
		}
		refreshTexts();
	}

	void saveTexts()
	{
		char *dir = obs_module_config_path("");
		if (dir) {
			os_mkdirs(dir);
			bfree(dir);
		}
		QJsonArray list;
		for (const Item &item : texts) {
			list.append(item.toJson());
		}
		QSaveFile file(TextsPath());
		if (file.open(QIODevice::WriteOnly)) {
			file.write(QJsonDocument(list).toJson());
			file.commit();
		}
	}

	void refreshTexts()
	{
		textsList->clear();
		for (const Item &item : texts) {
			textsList->addItem(item.label.isEmpty() ? item.summary()
								 : QString("[%1] %2").arg(Item::TypeName(item.type), item.label));
		}
	}

	QTabWidget *tabs = nullptr;
	QLabel *status = nullptr;

	QComboBox *bookCombo = nullptr;
	QLineEdit *search = nullptr;
	QListWidget *results = nullptr;
	QSpinBox *indexSpin = nullptr;
	QLabel *hadithText = nullptr;
	QLabel *hadithRef = nullptr;
	QLabel *gradesLabel = nullptr;
	QComboBox *gradeCombo = nullptr;

	QListWidget *sequenceList = nullptr;
	std::vector<Item> sequence;

	QComboBox *textType = nullptr;
	QLineEdit *textLabel = nullptr;
	QPlainTextEdit *textArabic = nullptr;
	QPlainTextEdit *textFrench = nullptr;
	QLineEdit *textReference = nullptr;
	QListWidget *textsList = nullptr;
	std::vector<Item> texts;
};

/* ------------------------------------------------------------------------ */
/* Lien avec le moteur et avec le panneau Coran                             */

/* Appelé par le panneau Coran : « Ajouter à la séquence » */
void ProcAddToSequence(void *, calldata_t *cd)
{
	Item item{QString::fromUtf8(calldata_string(cd, "type")), QString::fromUtf8(calldata_string(cd, "label")),
		  QString::fromUtf8(calldata_string(cd, "ar")), QString::fromUtf8(calldata_string(cd, "fr")),
		  QString::fromUtf8(calldata_string(cd, "ref"))};
	if (!dockWidget) {
		calldata_set_bool(cd, "ok", false);
		return;
	}
	QMetaObject::invokeMethod(
		dockWidget,
		[item] {
			if (dockWidget) {
				dockWidget->addToSequence(item);
			}
		},
		Qt::QueuedConnection);
	calldata_set_bool(cd, "ok", true);
}

void HotkeyNext(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && dockWidget) {
		QMetaObject::invokeMethod(
			dockWidget,
			[] {
				if (dockWidget) {
					dockWidget->sequenceStep(1);
				}
			},
			Qt::QueuedConnection);
	}
}

void HotkeyPrevious(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && dockWidget) {
		QMetaObject::invokeMethod(
			dockWidget,
			[] {
				if (dockWidget) {
					dockWidget->sequenceStep(-1);
				}
			},
			Qt::QueuedConnection);
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
	if (obj) {
		OBSDataArrayAutoRelease next = obs_data_get_array(obj, "raccourci_suivant");
		OBSDataArrayAutoRelease previous = obs_data_get_array(obj, "raccourci_precedent");
		obs_hotkey_load(nextHotkey, next);
		obs_hotkey_load(previousHotkey, previous);
	}
	if (dockWidget) {
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
	dockWidget = new LibraryDock(scroll);
	scroll->setWidget(dockWidget);
	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), scroll)) {
		delete scroll;
		return false;
	}

	proc_handler_add(obs_get_proc_handler(),
			 "void amal_sequence_ajouter(in string type, in string label, in string ar, in string fr, "
			 "in string ref, out bool ok)",
			 ProcAddToSequence, nullptr);

	nextHotkey = obs_hotkey_register_frontend("amal_sequence_suivant", obs_module_text("Hotkey.Next"), HotkeyNext,
						  nullptr);
	previousHotkey = obs_hotkey_register_frontend("amal_sequence_precedent", obs_module_text("Hotkey.Previous"),
						      HotkeyPrevious, nullptr);
	obs_frontend_add_save_callback(SaveCallback, nullptr);
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	blog(LOG_INFO, "[amal-bibliotheque] Module Bibliothèque chargé");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	obs_frontend_remove_save_callback(SaveCallback, nullptr);
	obs_hotkey_unregister(nextHotkey);
	obs_hotkey_unregister(previousHotkey);
}
