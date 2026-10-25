/******************************************************************************
    Amal Studio - module « Licence Pro »
    Copyright (C) 2026 Amal Ben Aly

    - 30 jours d'essai complet, puis les fonctions Pro demandent une licence :
      multidiffusion, cadres d'événement, fonds d'événements religieux,
      intros vidéo, relecture au ralenti, transitions Pro.
    - Licence annuelle liée à un ordinateur (code ordinateur), signée par
      l'éditeur avec une clé Ed25519 ; Amal Studio ne contient que la clé
      publique et vérifie la signature (bibliothèque Monocypher).
    - Espace éditeur : génération des licences, visible seulement quand le
      fichier de clé privée de l'éditeur est chargé sur l'ordinateur.

    Les autres modules interrogent l'état par la procédure
    « amal_licence_etat » (voir shared/amal/amal-licence-client.hpp).

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

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostInfo>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

extern "C" {
#include "monocypher/monocypher.h"
#include "monocypher/monocypher-ed25519.h"
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-licence", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : licence Pro (essai, activation)";
}

namespace {

constexpr const char *SECTION = "AmalLicence";
constexpr int TRIAL_DAYS = 30;
constexpr const char *KEY_PREFIX = "AMAL1";

/* Clé publique de l'éditeur (Ed25519). La clé privée correspondante n'est
 * jamais dans le code : elle reste chez l'éditeur. */
const uint8_t PUBLIC_KEY[32] = {0x88, 0x24, 0xE9, 0x59, 0x12, 0xB5, 0xA0, 0x8B, 0x45, 0xE7, 0x9A,
				0x68, 0x9D, 0x45, 0xDF, 0xAB, 0x89, 0xB0, 0xA9, 0xBE, 0x01, 0x35,
				0x5E, 0x8A, 0x02, 0x5A, 0x93, 0x40, 0xD4, 0x22, 0x12, 0x08};

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* ------------------------------------------------------------------------ */
/* Code ordinateur                                                          */

QString RawMachineId()
{
#ifdef _WIN32
	QSettings reg("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography", QSettings::NativeFormat);
	const QString guid = reg.value("MachineGuid").toString();
	if (!guid.isEmpty()) {
		return guid;
	}
#else
	for (const char *path : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
		QFile f(QString::fromUtf8(path));
		if (f.open(QIODevice::ReadOnly)) {
			const QString id = QString::fromUtf8(f.readAll()).trimmed();
			if (!id.isEmpty()) {
				return id;
			}
		}
	}
#endif
	return QHostInfo::localHostName();
}

/* 16 caractères lisibles (sans I, L, O, U), groupés par 4 */
QString MachineCode()
{
	static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
	const QByteArray input = ("AMAL-MACHINE|" + RawMachineId().toLower()).toUtf8();
	uint8_t hash[10];
	crypto_blake2b(hash, sizeof(hash), reinterpret_cast<const uint8_t *>(input.constData()),
		       (size_t)input.size());
	QString out;
	uint32_t buffer = 0;
	int bits = 0;
	for (uint8_t byte : hash) {
		buffer = (buffer << 8) | byte;
		bits += 8;
		while (bits >= 5) {
			out += QChar(alphabet[(buffer >> (bits - 5)) & 31]);
			bits -= 5;
		}
	}
	QString grouped;
	for (int i = 0; i < out.size(); i++) {
		if (i && i % 4 == 0) {
			grouped += '-';
		}
		grouped += out.at(i);
	}
	return grouped;
}

QString Normalize(const QString &code)
{
	QString c = code.toUpper();
	c.remove(QRegularExpression("[^0-9A-Z]"));
	return c;
}

/* ------------------------------------------------------------------------ */
/* Clés de licence : AMAL1.<contenu base64url>.<signature base64url>        */
/* contenu = « AMAL1;<code ordinateur>;<AAAAMMJJ fin>;<nom du client> »      */

struct Licence {
	bool valid = false;
	QString machine;
	QDate expiry;
	QString client;
	QString error;
};

constexpr auto B64 = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;

Licence ParseLicence(QString text)
{
	Licence lic;
	text.remove(QRegularExpression("\\s"));
	const QStringList parts = text.split('.');
	if (parts.size() != 3 || parts.at(0) != KEY_PREFIX) {
		lic.error = T("Error.Format");
		return lic;
	}
	const QByteArray payload = QByteArray::fromBase64(parts.at(1).toLatin1(), B64);
	const QByteArray signature = QByteArray::fromBase64(parts.at(2).toLatin1(), B64);
	if (signature.size() != 64 || payload.isEmpty()) {
		lic.error = T("Error.Format");
		return lic;
	}
	if (crypto_ed25519_check(reinterpret_cast<const uint8_t *>(signature.constData()), PUBLIC_KEY,
				 reinterpret_cast<const uint8_t *>(payload.constData()), (size_t)payload.size()) != 0) {
		lic.error = T("Error.Signature");
		return lic;
	}
	const QStringList fields = QString::fromUtf8(payload).split(';');
	if (fields.size() < 4 || fields.at(0) != KEY_PREFIX) {
		lic.error = T("Error.Format");
		return lic;
	}
	lic.machine = fields.at(1);
	lic.expiry = QDate::fromString(fields.at(2), "yyyyMMdd");
	lic.client = fields.mid(3).join(';');
	if (Normalize(lic.machine) != Normalize(MachineCode())) {
		lic.error = T("Error.Machine");
		return lic;
	}
	if (!lic.expiry.isValid()) {
		lic.error = T("Error.Format");
		return lic;
	}
	lic.valid = true;
	return lic;
}

QString MakeLicence(const uint8_t seed[32], const QString &machine, const QDate &expiry, const QString &client)
{
	const QString cleanClient = QString(client).remove(';').trimmed();
	const QByteArray payload =
		QString("%1;%2;%3;%4").arg(KEY_PREFIX, machine.toUpper(), expiry.toString("yyyyMMdd"), cleanClient).toUtf8();
	uint8_t seedCopy[32];
	memcpy(seedCopy, seed, 32);
	uint8_t secret[64], pub[32], signature[64];
	crypto_ed25519_key_pair(secret, pub, seedCopy);
	crypto_ed25519_sign(signature, secret, reinterpret_cast<const uint8_t *>(payload.constData()),
			    (size_t)payload.size());
	crypto_wipe(secret, sizeof(secret));
	return QString("%1.%2.%3")
		.arg(KEY_PREFIX, QString::fromLatin1(payload.toBase64(B64)),
		     QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(signature), 64).toBase64(B64)));
}

/* ------------------------------------------------------------------------ */
/* État : essai, licence, expiré (calculé sur le fil de l'interface)        */

enum class Status { Trial, Licensed, Expired, BadClock };

std::mutex stateMutex;
std::atomic<bool> proActive{true};
std::atomic<int> daysLeft{TRIAL_DAYS};
std::atomic<int> currentStatus{(int)Status::Trial};
QDate licenceExpiry;
QString licenceClient;

config_t *Config()
{
	return obs_frontend_get_app_config();
}

QDate ReadDate(const char *key)
{
	config_t *cfg = Config();
	const char *value = cfg ? config_get_string(cfg, SECTION, key) : nullptr;
	return value ? QDate::fromString(QString::fromUtf8(value), "yyyy-MM-dd") : QDate();
}

void WriteDate(const char *key, const QDate &date)
{
	config_t *cfg = Config();
	if (cfg) {
		config_set_string(cfg, SECTION, key, date.toString("yyyy-MM-dd").toUtf8().constData());
	}
}

/* Début de l'essai : le plus ancien entre la configuration et le registre */
QDate TrialStart()
{
	QDate start = ReadDate("Debut");
#ifdef _WIN32
	QSettings reg("HKEY_CURRENT_USER\\Software\\Amal Studio\\Licence", QSettings::NativeFormat);
	const QDate regDate = QDate::fromString(reg.value("Debut").toString(), "yyyy-MM-dd");
	if (regDate.isValid() && (!start.isValid() || regDate < start)) {
		start = regDate;
	}
#endif
	const QDate today = QDate::currentDate();
	if (!start.isValid() || start > today) {
		start = start.isValid() ? start : today;
	}
	WriteDate("Debut", start);
#ifdef _WIN32
	reg.setValue("Debut", start.toString("yyyy-MM-dd"));
#endif
	return start;
}

void RefreshState()
{
	const QDate today = QDate::currentDate();
	std::lock_guard<std::mutex> lock(stateMutex);

	/* Horloge reculée de plus de 2 jours : on ne fait pas confiance à la date */
	const QDate last = ReadDate("DerniereDate");
	if (last.isValid() && today < last.addDays(-2)) {
		currentStatus = (int)Status::BadClock;
		proActive = false;
		daysLeft = 0;
		return;
	}
	if (!last.isValid() || today > last) {
		WriteDate("DerniereDate", today);
	}

	config_t *cfg = Config();
	const char *key = cfg ? config_get_string(cfg, SECTION, "Cle") : nullptr;
	if (key && *key) {
		const Licence lic = ParseLicence(QString::fromUtf8(key));
		if (lic.valid) {
			licenceExpiry = lic.expiry;
			licenceClient = lic.client;
			if (today <= lic.expiry) {
				currentStatus = (int)Status::Licensed;
				proActive = true;
				daysLeft = (int)today.daysTo(lic.expiry);
				return;
			}
			currentStatus = (int)Status::Expired;
			proActive = false;
			daysLeft = 0;
			return;
		}
	}

	const QDate start = TrialStart();
	const int left = TRIAL_DAYS - (int)start.daysTo(today);
	daysLeft = left > 0 ? left : 0;
	currentStatus = left > 0 ? (int)Status::Trial : (int)Status::Expired;
	proActive = left > 0;
	if (cfg) {
		config_save_safe(cfg, "tmp", nullptr);
	}
}

/* Les habillages et transitions se redessinent avec le nouvel état */
void RefreshConsumers()
{
	obs_enum_sources(
		[](void *, obs_source_t *source) {
			const char *id = obs_source_get_unversioned_id(source);
			if (id && strcmp(id, "amal_graphique") == 0) {
				OBSDataAutoRelease empty = obs_data_create();
				obs_source_update(source, empty);
			}
			return true;
		},
		nullptr);
	obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		obs_source_t *source = transitions.sources.array[i];
		const char *id = obs_source_get_unversioned_id(source);
		if (id && strcmp(id, "amal_transition") == 0) {
			OBSDataAutoRelease empty = obs_data_create();
			obs_source_update(source, empty);
		}
	}
	obs_frontend_source_list_free(&transitions);
}

/* ------------------------------------------------------------------------ */
/* Clé de l'éditeur (fichier sur l'ordinateur de l'éditeur seulement)       */

QString EditorKeyPath()
{
	char *path = obs_module_config_path("cle-editeur.txt");
	const QString result = QString::fromUtf8(path ? path : "");
	bfree(path);
	return result;
}

bool ReadEditorSeed(const QString &path, uint8_t seed[32])
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return false;
	}
	const QStringList lines = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
	if (lines.size() < 2 || lines.at(0).trimmed() != "AMAL-EDITEUR-1") {
		return false;
	}
	const QByteArray raw = QByteArray::fromBase64(lines.at(1).trimmed().toLatin1());
	if (raw.size() != 32) {
		return false;
	}
	uint8_t seedCopy[32], secret[64], pub[32];
	memcpy(seedCopy, raw.constData(), 32);
	crypto_ed25519_key_pair(secret, pub, seedCopy);
	crypto_wipe(secret, sizeof(secret));
	if (memcmp(pub, PUBLIC_KEY, 32) != 0) {
		return false; /* ce n'est pas la clé de cet éditeur */
	}
	memcpy(seed, raw.constData(), 32);
	return true;
}

/* ------------------------------------------------------------------------ */
/* Fenêtre « Licence Amal Studio Pro »                                      */

QString StatusText()
{
	switch ((Status)currentStatus.load()) {
	case Status::Trial:
		return T("Status.Trial").arg(daysLeft.load());
	case Status::Licensed:
		return T("Status.Licensed").arg(licenceClient, QLocale(QLocale::French).toString(licenceExpiry, "d MMMM yyyy"));
	case Status::BadClock:
		return T("Status.BadClock");
	case Status::Expired:
	default:
		return licenceExpiry.isValid()
			       ? T("Status.LicenceExpired").arg(QLocale(QLocale::French).toString(licenceExpiry, "d MMMM yyyy"))
			       : T("Status.TrialOver");
	}
}

class LicenceDialog : public QDialog {
public:
	explicit LicenceDialog(QWidget *parent) : QDialog(parent)
	{
		setWindowTitle(T("Title"));
		setMinimumWidth(560);
		QVBoxLayout *layout = new QVBoxLayout(this);

		status = new QLabel(this);
		status->setWordWrap(true);
		status->setTextFormat(Qt::RichText);
		layout->addWidget(status);

		QLabel *features = new QLabel(T("Features"), this);
		features->setWordWrap(true);
		layout->addWidget(features);

		QGroupBox *buy = new QGroupBox(T("Buy.Title"), this);
		QVBoxLayout *bl = new QVBoxLayout(buy);
		QLabel *how = new QLabel(T("Buy.How"), buy);
		how->setWordWrap(true);
		bl->addWidget(how);
		QHBoxLayout *codeRow = new QHBoxLayout;
		codeRow->addWidget(new QLabel(T("Machine"), buy));
		QLineEdit *code = new QLineEdit(MachineCode(), buy);
		code->setReadOnly(true);
		QFont mono = code->font();
		mono.setFamily("Consolas");
		mono.setPointSizeF(mono.pointSizeF() * 1.2);
		code->setFont(mono);
		QPushButton *copy = new QPushButton(T("Copy"), buy);
		codeRow->addWidget(code, 1);
		codeRow->addWidget(copy);
		bl->addLayout(codeRow);
		layout->addWidget(buy);

		QGroupBox *activate = new QGroupBox(T("Activate.Title"), this);
		QVBoxLayout *al = new QVBoxLayout(activate);
		keyEdit = new QPlainTextEdit(activate);
		keyEdit->setPlaceholderText(T("Activate.Placeholder"));
		keyEdit->setFixedHeight(80);
		al->addWidget(keyEdit);
		QPushButton *activateButton = new QPushButton(T("Activate"), activate);
		activateButton->setMinimumHeight(32);
		al->addWidget(activateButton);
		message = new QLabel(activate);
		message->setWordWrap(true);
		al->addWidget(message);
		layout->addWidget(activate);

		/* Espace éditeur : seulement avec la clé privée de l'éditeur */
		editor = new QGroupBox(T("Editor.Title"), this);
		QFormLayout *ef = new QFormLayout(editor);
		editorMachine = new QLineEdit(editor);
		editorMachine->setPlaceholderText("XXXX-XXXX-XXXX-XXXX");
		editorClient = new QLineEdit(editor);
		editorDuration = new QComboBox(editor);
		editorDuration->addItem(T("Editor.Months12"), 12);
		editorDuration->addItem(T("Editor.Months6"), 6);
		editorDuration->addItem(T("Editor.Months3"), 3);
		editorDuration->addItem(T("Editor.Months1"), 1);
		editorDuration->addItem(T("Editor.Months24"), 24);
		QPushButton *generate = new QPushButton(T("Editor.Generate"), editor);
		editorResult = new QPlainTextEdit(editor);
		editorResult->setReadOnly(true);
		editorResult->setFixedHeight(110);
		QPushButton *copyResult = new QPushButton(T("Editor.CopyMessage"), editor);
		ef->addRow(T("Editor.Machine"), editorMachine);
		ef->addRow(T("Editor.Client"), editorClient);
		ef->addRow(T("Editor.Duration"), editorDuration);
		ef->addRow(generate);
		ef->addRow(editorResult);
		ef->addRow(copyResult);
		layout->addWidget(editor);

		QHBoxLayout *bottom = new QHBoxLayout;
		QPushButton *loadEditor = new QPushButton(T("Editor.Load"), this);
		loadEditor->setFlat(true);
		QDialogButtonBox *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
		bottom->addWidget(loadEditor);
		bottom->addStretch(1);
		bottom->addWidget(close);
		layout->addLayout(bottom);

		connect(copy, &QPushButton::clicked, this, [this, code] {
			QApplication::clipboard()->setText(code->text());
			message->setText(T("Copied"));
		});
		connect(activateButton, &QPushButton::clicked, this, &LicenceDialog::activateKey);
		connect(generate, &QPushButton::clicked, this, &LicenceDialog::generateKey);
		connect(copyResult, &QPushButton::clicked, this, [this] {
			QApplication::clipboard()->setText(editorResult->toPlainText());
		});
		connect(loadEditor, &QPushButton::clicked, this, &LicenceDialog::loadEditorKey);
		connect(close, &QDialogButtonBox::rejected, this, &QDialog::accept);

		refresh();
	}

private:
	void refresh()
	{
		RefreshState();
		status->setText(QString("<b>%1</b>").arg(StatusText().toHtmlEscaped()));
		uint8_t seed[32];
		const bool isEditor = ReadEditorSeed(EditorKeyPath(), seed);
		crypto_wipe(seed, sizeof(seed));
		editor->setVisible(isEditor);
	}

	void activateKey()
	{
		const Licence lic = ParseLicence(keyEdit->toPlainText());
		if (!lic.valid) {
			message->setText(lic.error);
			return;
		}
		if (lic.expiry < QDate::currentDate()) {
			message->setText(T("Error.Expired"));
			return;
		}
		QString clean = keyEdit->toPlainText();
		clean.remove(QRegularExpression("\\s"));
		config_t *cfg = Config();
		config_set_string(cfg, SECTION, "Cle", clean.toUtf8().constData());
		config_save_safe(cfg, "tmp", nullptr);
		refresh();
		RefreshConsumers();
		message->setText(T("Activated"));
		blog(LOG_INFO, "[amal-licence] Licence activée jusqu'au %s",
		     lic.expiry.toString("yyyy-MM-dd").toUtf8().constData());
	}

	void generateKey()
	{
		uint8_t seed[32];
		if (!ReadEditorSeed(EditorKeyPath(), seed)) {
			editorResult->setPlainText(T("Editor.NoKey"));
			return;
		}
		const QString machine = Normalize(editorMachine->text());
		if (machine.size() != 16) {
			crypto_wipe(seed, sizeof(seed));
			editorResult->setPlainText(T("Editor.BadMachine"));
			return;
		}
		QString grouped;
		for (int i = 0; i < 16; i++) {
			if (i && i % 4 == 0) {
				grouped += '-';
			}
			grouped += machine.at(i);
		}
		const QDate expiry = QDate::currentDate().addMonths(editorDuration->currentData().toInt());
		const QString key = MakeLicence(seed, grouped, expiry, editorClient->text());
		crypto_wipe(seed, sizeof(seed));
		editorResult->setPlainText(
			T("Editor.Message")
				.arg(editorClient->text().trimmed(), QLocale(QLocale::French).toString(expiry, "d MMMM yyyy"), key));
	}

	void loadEditorKey()
	{
		const QString file = QFileDialog::getOpenFileName(this, T("Editor.Load"), QString(), "*.txt");
		if (file.isEmpty()) {
			return;
		}
		uint8_t seed[32];
		if (!ReadEditorSeed(file, seed)) {
			message->setText(T("Editor.Invalid"));
			return;
		}
		crypto_wipe(seed, sizeof(seed));
		char *dir = obs_module_config_path("");
		if (dir) {
			os_mkdirs(dir);
			bfree(dir);
		}
		QFile::remove(EditorKeyPath());
		if (!QFile::copy(file, EditorKeyPath())) {
			message->setText(T("Editor.Invalid"));
			return;
		}
		message->setText(T("Editor.Loaded"));
		refresh();
	}

	QLabel *status = nullptr;
	QLabel *message = nullptr;
	QPlainTextEdit *keyEdit = nullptr;
	QGroupBox *editor = nullptr;
	QLineEdit *editorMachine = nullptr;
	QLineEdit *editorClient = nullptr;
	QComboBox *editorDuration = nullptr;
	QPlainTextEdit *editorResult = nullptr;
};

void OpenDialog()
{
	LicenceDialog dialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
	dialog.exec();
}

/* ------------------------------------------------------------------------ */
/* Procédures pour les autres modules                                       */

/* « void amal_licence_etat(out bool pro, out int jours, out string etat) » */
void ProcState(void *, calldata_t *cd)
{
	calldata_set_bool(cd, "pro", proActive.load());
	calldata_set_int(cd, "jours", daysLeft.load());
	const char *names[] = {"essai", "licence", "expire", "horloge"};
	calldata_set_string(cd, "etat", names[currentStatus.load()]);
}

/* « void amal_licence_ouvrir() » : ouvre la fenêtre (menu Amal) */
void ProcOpen(void *, calldata_t *)
{
	QMetaObject::invokeMethod(
		static_cast<QObject *>(obs_frontend_get_main_window()), [] { OpenDialog(); }, Qt::QueuedConnection);
}

/* Rappel au démarrage : fin d'essai proche ou licence expirée, une fois par jour */
void Reminder()
{
	const Status s = (Status)currentStatus.load();
	const bool soon = (s == Status::Trial && daysLeft <= 5) || (s == Status::Licensed && daysLeft <= 7);
	if (!soon && s != Status::Expired && s != Status::BadClock) {
		return;
	}
	const QDate today = QDate::currentDate();
	if (ReadDate("DernierRappel") == today) {
		return;
	}
	WriteDate("DernierRappel", today);
	QTimer::singleShot(4000, static_cast<QWidget *>(obs_frontend_get_main_window()), [] {
		if (QApplication::activeModalWidget()) {
			return;
		}
		OpenDialog();
	});
}

void FrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		RefreshState();
		Reminder();
	}
}

QTimer *dailyTimer = nullptr;

} // namespace

bool obs_module_load(void)
{
	RefreshState();
	proc_handler_t *ph = obs_get_proc_handler();
	proc_handler_add(ph, "void amal_licence_etat(out bool pro, out int jours, out string etat)", ProcState,
			 nullptr);
	proc_handler_add(ph, "void amal_licence_ouvrir()", ProcOpen, nullptr);
	obs_frontend_add_event_callback(FrontendEvent, nullptr);

	/* Réévaluation régulière (fin d'essai pendant une longue session) */
	dailyTimer = new QTimer(static_cast<QObject *>(obs_frontend_get_main_window()));
	QObject::connect(dailyTimer, &QTimer::timeout, [] {
		const bool before = proActive.load();
		RefreshState();
		if (before != proActive.load()) {
			RefreshConsumers();
		}
	});
	dailyTimer->start(60 * 60 * 1000);
	blog(LOG_INFO, "[amal-licence] État : %s, %d jour(s)",
	     proActive.load() ? "fonctions Pro actives" : "fonctions Pro verrouillées", daysLeft.load());
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
}
