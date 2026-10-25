/******************************************************************************
    Amal Studio - assistant micro
    Copyright (C) 2026 Amal Ben Aly

    Amal Studio est utilisé avec des micros très différents : micro USB,
    micro-cravate, micro sans fil, micro XLR avec carte son, sono de la salle,
    micro intégré, micro branché au téléphone, casque Bluetooth.
    L'assistant explique comment brancher chaque type, puis règle la source
    avec des filtres adaptés à l'usage (parole ou récitation du Coran) et au
    lieu (calme, bruyant, grande salle). Un vumètre permet de vérifier le son.

    Les filtres ajoutés portent le préfixe « Amal · » : ils sont remplacés à
    chaque nouveau réglage, et ceux de l'utilisateur ne sont jamais touchés.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "amal-assistant.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace amal {
namespace {

#if defined(_WIN32)
constexpr const char *MIC_ID = "wasapi_input_capture";
#elif defined(__APPLE__)
constexpr const char *MIC_ID = "coreaudio_input_capture";
#else
constexpr const char *MIC_ID = "pulse_input_capture";
#endif
constexpr const char *FILTER_PREFIX = "Amal \xC2\xB7 ";

/* Types de micros */
enum MicType { Usb, Lavalier, Wireless, Xlr, Desk, BuiltIn, Phone, Bluetooth, TypeCount };
const char *TYPE_KEYS[TypeCount] = {"Mic.Type.Usb",	  "Mic.Type.Lavalier", "Mic.Type.Wireless",
				    "Mic.Type.Xlr",	  "Mic.Type.Desk",     "Mic.Type.BuiltIn",
				    "Mic.Type.Phone", "Mic.Type.Bluetooth"};
const char *HELP_KEYS[TypeCount] = {"Mic.Help.Usb",   "Mic.Help.Lavalier", "Mic.Help.Wireless",
				    "Mic.Help.Xlr",   "Mic.Help.Desk",     "Mic.Help.BuiltIn",
				    "Mic.Help.Phone", "Mic.Help.Bluetooth"};

enum Use { Speech, Recitation };
enum Place { Calm, Noisy, Hall };

/* Sources audio existantes (micros, téléphones…) */
std::vector<QString> AudioSources()
{
	std::vector<QString> names;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			const uint32_t flags = obs_source_get_output_flags(source);
			const char *id = obs_source_get_unversioned_id(source);
			if ((flags & OBS_SOURCE_AUDIO) == 0 || !id || strcmp(id, "amal_graphique") == 0 ||
			    strcmp(id, "browser_source") == 0) {
				return true;
			}
			static_cast<std::vector<QString> *>(param)->push_back(
				QString::fromUtf8(obs_source_get_name(source)));
			return true;
		},
		&names);
	/* Canaux audio globaux (Audio du bureau, Mic/Aux…) */
	for (uint32_t channel = 1; channel <= 6; channel++) {
		OBSSourceAutoRelease source = obs_get_output_source(channel);
		if (source) {
			const QString name = QString::fromUtf8(obs_source_get_name(source));
			bool present = false;
			for (const QString &n : names) {
				present = present || n == name;
			}
			if (!present) {
				names.push_back(name);
			}
		}
	}
	return names;
}

std::vector<std::pair<QString, QString>> InputDevices()
{
	std::vector<std::pair<QString, QString>> devices;
	OBSSourceAutoRelease probe = obs_source_create_private(MIC_ID, "amal-micro-detection", nullptr);
	if (!probe) {
		return devices;
	}
	obs_properties_t *props = obs_source_properties(probe);
	obs_property_t *list = props ? obs_properties_get(props, "device_id") : nullptr;
	if (list && obs_property_list_format(list) == OBS_COMBO_FORMAT_STRING) {
		for (size_t i = 0; i < obs_property_list_item_count(list); i++) {
			const char *value = obs_property_list_item_string(list, i);
			const char *name = obs_property_list_item_name(list, i);
			if (value && *value && strcmp(value, "default") != 0) {
				devices.emplace_back(QString::fromUtf8(name ? name : value), QString::fromUtf8(value));
			}
		}
	}
	obs_properties_destroy(props);
	return devices;
}

/* Retire les filtres ajoutés précédemment par l'assistant */
void RemoveAmalFilters(obs_source_t *source)
{
	std::vector<OBSSource> old;
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			if (strncmp(obs_source_get_name(filter), FILTER_PREFIX, strlen(FILTER_PREFIX)) == 0) {
				static_cast<std::vector<OBSSource> *>(param)->emplace_back(filter);
			}
		},
		&old);
	for (OBSSource &filter : old) {
		obs_source_filter_remove(source, filter);
	}
}

void AddFilter(obs_source_t *source, const char *id, const char *nameKey, obs_data_t *settings)
{
	const QString name = QString::fromUtf8(FILTER_PREFIX) + T(nameKey);
	OBSSourceAutoRelease filter = obs_source_create(id, name.toUtf8().constData(), settings, nullptr);
	if (filter) {
		obs_source_filter_add(source, filter);
	}
}

/* Réglages adaptés au type de micro, à l'usage et au lieu */
void ApplyPreset(obs_source_t *source, MicType type, Use use, Place place)
{
	RemoveAmalFilters(source);
	const bool recitation = use == Recitation;
	const bool cleanSource = type == Xlr || type == Desk || type == Usb || type == Wireless;

	/* 1. Réduction de bruit : douce pour la récitation (préserve les notes tenues) */
	if (place == Noisy || place == Hall || !cleanSource) {
		OBSDataAutoRelease s = obs_data_create();
		if (recitation || place == Hall || type == Desk) {
			obs_data_set_string(s, "method", "speex");
			obs_data_set_int(s, "suppress_level", place == Noisy ? -20 : -12);
		} else {
			obs_data_set_string(s, "method", "rnnoise");
		}
		AddFilter(source, "noise_suppress_filter_v2", "Filter.Noise", s);
	}

	/* 2. Porte de bruit : parole en lieu bruyant seulement (elle couperait la fin des récitations) */
	if (!recitation && place == Noisy) {
		OBSDataAutoRelease s = obs_data_create();
		obs_data_set_double(s, "open_threshold", -26.0);
		obs_data_set_double(s, "close_threshold", -32.0);
		obs_data_set_int(s, "attack_time", 25);
		obs_data_set_int(s, "hold_time", 200);
		obs_data_set_int(s, "release_time", 150);
		AddFilter(source, "noise_gate_filter", "Filter.Gate", s);
	}

	/* 3. Compresseur : voix régulière (parole) ou dynamique préservée (récitation) */
	{
		OBSDataAutoRelease s = obs_data_create();
		if (recitation) {
			obs_data_set_double(s, "ratio", 2.0);
			obs_data_set_double(s, "threshold", -14.0);
			obs_data_set_int(s, "attack_time", 10);
			obs_data_set_int(s, "release_time", 150);
			obs_data_set_double(s, "output_gain", 0.0);
		} else {
			obs_data_set_double(s, "ratio", type == Desk ? 2.0 : 3.0);
			obs_data_set_double(s, "threshold", -18.0);
			obs_data_set_int(s, "attack_time", 6);
			obs_data_set_int(s, "release_time", 60);
			obs_data_set_double(s, "output_gain", type == BuiltIn || type == Phone ? 4.0 : 2.0);
		}
		AddFilter(source, "compressor_filter", "Filter.Compressor", s);
	}

	/* 4. Limiteur : évite toute saturation (montées de voix, takbir…) */
	{
		OBSDataAutoRelease s = obs_data_create();
		obs_data_set_double(s, "threshold", -2.0);
		obs_data_set_int(s, "release_time", 60);
		AddFilter(source, "limiter_filter", "Filter.Limiter", s);
	}

	/* Micro XLR, cravate ou sono branchés sur une seule entrée : son au centre (mono) */
	uint32_t flags = obs_source_get_flags(source);
	if (type == Xlr || type == Desk || type == Lavalier) {
		flags |= OBS_SOURCE_FLAG_FORCE_MONO;
	}
	obs_source_set_flags(source, flags);
}

/* ------------------------------------------------------------------------ */

class MicDialog : public QDialog {
public:
	explicit MicDialog(QWidget *parent) : QDialog(parent)
	{
		setWindowTitle(T("Mic.Title"));
		setMinimumWidth(620);
		QVBoxLayout *layout = new QVBoxLayout(this);

		QFormLayout *form = new QFormLayout;
		type = new QComboBox(this);
		for (int i = 0; i < TypeCount; i++) {
			type->addItem(T(TYPE_KEYS[i]), i);
		}
		form->addRow(T("Mic.Type"), type);
		layout->addLayout(form);

		help = new QLabel(this);
		help->setWordWrap(true);
		help->setTextFormat(Qt::PlainText);
		help->setStyleSheet("padding: 8px; border: 1px solid rgba(255,255,255,0.15); border-radius: 6px;");
		layout->addWidget(help);

		QFormLayout *settings = new QFormLayout;
		target = new QComboBox(this);
		use = new QComboBox(this);
		use->addItem(T("Mic.Use.Speech"), Speech);
		use->addItem(T("Mic.Use.Recitation"), Recitation);
		place = new QComboBox(this);
		place->addItem(T("Mic.Place.Calm"), Calm);
		place->addItem(T("Mic.Place.Noisy"), Noisy);
		place->addItem(T("Mic.Place.Hall"), Hall);
		settings->addRow(T("Mic.Target"), target);
		settings->addRow(T("Mic.Use"), use);
		settings->addRow(T("Mic.Place"), place);
		layout->addLayout(settings);

		QPushButton *apply = new QPushButton(T("Mic.Apply"), this);
		apply->setMinimumHeight(34);
		QFont bold = apply->font();
		bold.setBold(true);
		apply->setFont(bold);
		layout->addWidget(apply);

		QLabel *meterLabel = new QLabel(T("Mic.Meter"), this);
		meterLabel->setWordWrap(true);
		layout->addWidget(meterLabel);
		meter = new QProgressBar(this);
		meter->setRange(-60, 0);
		meter->setValue(-60);
		meter->setFormat("%v dB");
		layout->addWidget(meter);
		status = new QLabel(this);
		status->setWordWrap(true);
		layout->addWidget(status);

		QPushButton *close = new QPushButton(T("Mic.Close"), this);
		layout->addWidget(close, 0, Qt::AlignRight);

		connect(type, &QComboBox::currentIndexChanged, this, [this](int) { typeChanged(); });
		connect(target, &QComboBox::currentIndexChanged, this, [this](int) { watchSelected(); });
		connect(apply, &QPushButton::clicked, this, &MicDialog::apply);
		connect(close, &QPushButton::clicked, this, &QDialog::accept);

		timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, &MicDialog::updateMeter);
		timer->start(60);

		typeChanged();
	}

	~MicDialog() override { detachMeter(); }

private:
	void typeChanged()
	{
		const int t = type->currentData().toInt();
		help->setText(T(HELP_KEYS[t]));
		if (t == Bluetooth) {
			place->setCurrentIndex(place->findData(Noisy));
		}

		/* Liste : appareils (nouveau micro) et sources existantes ; pour le téléphone, ses sources d'abord */
		target->blockSignals(true);
		target->clear();
		std::vector<QString> sources = AudioSources();
		if (t == Phone) {
			for (const QString &name : sources) {
				if (name.contains(T("Mic.PhoneMatch"), Qt::CaseInsensitive)) {
					target->addItem(T("Mic.Existing").arg(name), "source:" + name);
				}
			}
			if (target->count() == 0) {
				target->addItem(T("Mic.NoPhone"), QString());
			}
		} else {
			for (const auto &device : InputDevices()) {
				target->addItem(T("Mic.Device").arg(device.first), "device:" + device.second);
			}
			for (const QString &name : sources) {
				target->addItem(T("Mic.Existing").arg(name), "source:" + name);
			}
		}
		target->blockSignals(false);
		watchSelected();
	}

	OBSSource resolveTarget(bool create)
	{
		const QString choice = target->currentData().toString();
		if (choice.startsWith("source:")) {
			OBSSourceAutoRelease source = obs_get_source_by_name(choice.mid(7).toUtf8().constData());
			return OBSSource(source.Get());
		}
		if (!choice.startsWith("device:") || !create) {
			return OBSSource();
		}
		/* Nouveau micro : source audio globale sur un canal Mic/Aux libre (3 à 6) */
		const QString device = choice.mid(7);
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "device_id", device.toUtf8().constData());
		for (uint32_t channel = 3; channel <= 6; channel++) {
			OBSSourceAutoRelease existing = obs_get_output_source(channel);
			if (existing) {
				OBSDataAutoRelease current = obs_source_get_settings(existing);
				if (device == QString::fromUtf8(obs_data_get_string(current, "device_id"))) {
					return OBSSource(existing.Get()); /* ce micro est déjà installé */
				}
				continue;
			}
			const QString name = T("Mic.SourceName").arg(type->currentText().section(" (", 0, 0));
			QString unique = name;
			for (int n = 2; n < 50; n++) {
				OBSSourceAutoRelease taken = obs_get_source_by_name(unique.toUtf8().constData());
				if (!taken) {
					break;
				}
				unique = QString("%1 %2").arg(name).arg(n);
			}
			OBSSourceAutoRelease created =
				obs_source_create(MIC_ID, unique.toUtf8().constData(), settings, nullptr);
			if (created) {
				obs_set_output_source(channel, created);
			}
			return OBSSource(created.Get());
		}
		status->setText(T("Mic.NoChannel"));
		return OBSSource();
	}

	void apply()
	{
		OBSSource source = resolveTarget(true);
		if (!source) {
			if (status->text().isEmpty()) {
				status->setText(T("Mic.NoTarget"));
			}
			return;
		}
		ApplyPreset(source, (MicType)type->currentData().toInt(), (Use)use->currentData().toInt(),
			    (Place)place->currentData().toInt());
		status->setText(T("Mic.Applied").arg(QString::fromUtf8(obs_source_get_name(source))));
		attachMeter(source);
		/* Le nouveau micro apparaît dans la liste */
		const QString name = QString::fromUtf8(obs_source_get_name(source));
		typeChanged();
		const int index = target->findData("source:" + name);
		if (index >= 0) {
			target->setCurrentIndex(index);
		}
	}

	/* ---------- Vumètre ---------- */

	static void MeterCallback(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
				  const float[MAX_AUDIO_CHANNELS], const float[MAX_AUDIO_CHANNELS])
	{
		MicDialog *dialog = static_cast<MicDialog *>(param);
		float level = magnitude[0];
		if (magnitude[1] > level && std::isfinite(magnitude[1])) {
			level = magnitude[1];
		}
		dialog->level = std::isfinite(level) ? level : -60.0f;
	}

	void watchSelected()
	{
		OBSSource source = resolveTarget(false);
		if (source) {
			attachMeter(source);
		} else {
			detachMeter();
		}
	}

	void attachMeter(obs_source_t *source)
	{
		detachMeter();
		volmeter = obs_volmeter_create(OBS_FADER_LOG);
		obs_volmeter_add_callback(volmeter, MeterCallback, this);
		obs_volmeter_attach_source(volmeter, source);
	}

	void detachMeter()
	{
		if (volmeter) {
			obs_volmeter_remove_callback(volmeter, MeterCallback, this);
			obs_volmeter_destroy(volmeter);
			volmeter = nullptr;
		}
		level = -60.0f;
	}

	void updateMeter()
	{
		const float db = level.load();
		const int value = (int)std::lround(db < -60.0f ? -60.0f : db > 0.0f ? 0.0f : db);
		meter->setValue(value);
		const char *color = value > -6 ? "#E74C3C" : value > -20 ? "#2ECC71" : "#F1C40F";
		meter->setStyleSheet(QString("QProgressBar::chunk { background: %1; }").arg(color));
	}

	QComboBox *type = nullptr;
	QComboBox *target = nullptr;
	QComboBox *use = nullptr;
	QComboBox *place = nullptr;
	QLabel *help = nullptr;
	QLabel *status = nullptr;
	QProgressBar *meter = nullptr;
	QTimer *timer = nullptr;
	obs_volmeter_t *volmeter = nullptr;
	std::atomic<float> level{-60.0f};
};

} // namespace

void OpenMicAssistant()
{
	MicDialog dialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
	dialog.exec();
}

} // namespace amal
