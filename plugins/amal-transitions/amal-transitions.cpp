/******************************************************************************
    Amal Studio - transitions personnalisées et relecture (ralenti)
    Copyright (C) 2026 Amal Ben Aly

    1. « Transition Amal » : transition utilisable entre deux scènes.
       - mode animation : volet, rideau ou cercle aux couleurs d'Amal Studio,
         avec le logo et un texte au centre ; l'écran est entièrement couvert
         à mi-parcours, au moment où la scène change ;
       - mode vidéo : la vidéo de l'événement choisie par l'utilisateur, avec
         un point de coupure réglable (comme la transition « Stinger »).
    2. Panneau « Relecture » : rejoue l'action précédente (mémoire de
       relecture du moteur) à vitesse normale ou au ralenti, encadrée par la
       transition Amal, puis revient automatiquement à la scène d'origine.

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
#include <QComboBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cstdio>
#include <cstring>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-transitions", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : transitions personnalisées et relecture";
}

namespace {

constexpr const char *TRANSITION_ID = "amal_transition";
constexpr const char *GRAPHIC_ID = "amal_graphique";
constexpr const char *DOCK_ID = "amal-relecture";
constexpr const char *SAVE_KEY = "amal_relecture";
constexpr const char *CONFIG_SECTION = "AmalRelecture";

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* ------------------------------------------------------------------------ */
/* Outils d'adresse (page des modèles du module amal-graphiques)            */

std::string PercentEncode(const std::string &input, const char *keep)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(input.size() * 3);
	for (unsigned char c : input) {
		const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
					c == '-' || c == '_' || c == '.' || c == '~';
		if (unreserved || (c < 0x80 && keep && std::strchr(keep, (char)c))) {
			out += (char)c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0x0F];
		}
	}
	return out;
}

std::string LocalFileUrl(std::string file)
{
	for (char &c : file) {
		if (c == '\\') {
			c = '/';
		}
	}
	if (!file.empty() && file[0] == '/') {
		file.erase(0, 1);
	}
	return "http://absolute/" + PercentEncode(file, "/:");
}

std::string OverlayBaseUrl()
{
	obs_module_t *graphics = obs_get_module("amal-graphiques");
	char *relative = graphics ? obs_find_module_file(graphics, "overlay/overlay.html") : nullptr;
	if (!relative) {
		blog(LOG_ERROR, "[amal-transitions] Page des modèles introuvable (module amal-graphiques)");
		return std::string();
	}
	char *absolute = os_get_abs_path_ptr(relative);
	std::string file(absolute ? absolute : relative);
	bfree(absolute);
	bfree(relative);
	return LocalFileUrl(file);
}

std::string ColorToHex(long long value)
{
	const uint32_t v = (uint32_t)value;
	char buffer[8];
	snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF);
	return buffer;
}

/* ------------------------------------------------------------------------ */
/* 1. Transition Amal                                                       */

struct AmalTransition {
	obs_source_t *source = nullptr;
	obs_source_t *browser = nullptr; /* animation (page des modèles) */
	obs_source_t *video = nullptr;   /* vidéo de l'utilisateur */
	bool useVideo = false;
	bool transitioning = false;
	uint32_t durationMs = 1400;
	int64_t videoCutMs = 700;
	float cut = 0.5f;
};

void UpdateTransition(void *data, obs_data_t *settings)
{
	AmalTransition *tr = static_cast<AmalTransition *>(data);

	tr->useVideo = strcmp(obs_data_get_string(settings, "mode"), "video") == 0;
	tr->durationMs = (uint32_t)obs_data_get_int(settings, "duree");
	tr->videoCutMs = obs_data_get_int(settings, "point_coupure");

	/* Animation : page des modèles en mode « transition » */
	obs_video_info ovi = {};
	uint32_t width = 1920, height = 1080;
	if (obs_get_video_info(&ovi) && ovi.base_width && ovi.base_height) {
		width = ovi.base_width;
		height = ovi.base_height;
	}

	OBSDataAutoRelease page = obs_data_create();
	obs_data_set_string(page, "t", "transition");
	obs_data_set_string(page, "style", obs_data_get_string(settings, "style"));
	obs_data_set_int(page, "dur", (long long)tr->durationMs);
	obs_data_set_string(page, "fr", obs_data_get_string(settings, "texte"));
	obs_data_set_string(page, "primary", ColorToHex(obs_data_get_int(settings, "couleur_principale")).c_str());
	obs_data_set_string(page, "accent", ColorToHex(obs_data_get_int(settings, "couleur_accent")).c_str());
	const char *logo = obs_data_get_string(settings, "logo");
	if (logo && *logo && os_file_exists(logo)) {
		obs_data_set_string(page, "logo", LocalFileUrl(logo).c_str());
	}
	const std::string base = OverlayBaseUrl();
	const std::string url = base.empty() ? std::string() : base + "#" + PercentEncode(obs_data_get_json(page), nullptr);

	OBSDataAutoRelease browserSettings = obs_data_create();
	obs_data_set_bool(browserSettings, "is_local_file", false);
	obs_data_set_string(browserSettings, "url", url.c_str());
	obs_data_set_int(browserSettings, "width", width);
	obs_data_set_int(browserSettings, "height", height);
	obs_data_set_bool(browserSettings, "shutdown", false);
	obs_data_set_bool(browserSettings, "restart_when_active", false);
	obs_data_set_bool(browserSettings, "reroute_audio", false);
	obs_data_set_string(browserSettings, "css", "");
	if (!tr->browser) {
		tr->browser = obs_source_create_private("browser_source", "amal-transition-animation", browserSettings);
	} else {
		obs_source_update(tr->browser, browserSettings);
	}

	/* Vidéo de l'utilisateur */
	const char *path = obs_data_get_string(settings, "video");
	if (tr->video && tr->transitioning) {
		obs_source_remove_active_child(tr->source, tr->video);
	}
	obs_source_release(tr->video);
	tr->video = nullptr;
	if (tr->useVideo && path && *path) {
		OBSDataAutoRelease media = obs_data_create();
		obs_data_set_string(media, "local_file", path);
		obs_data_set_bool(media, "hw_decode", true);
		obs_data_set_bool(media, "looping", false);
		obs_data_set_bool(media, "is_stinger", true);
		tr->video = obs_source_create_private("ffmpeg_source", "amal-transition-video", media);
		if (tr->video && tr->transitioning) {
			obs_source_add_active_child(tr->source, tr->video);
		}
	}

	obs_transition_enable_fixed(tr->source, true, tr->durationMs);
}

void *CreateTransition(obs_data_t *settings, obs_source_t *source)
{
	AmalTransition *tr = new AmalTransition();
	tr->source = source;
	UpdateTransition(tr, settings);
	return tr;
}

void DestroyTransition(void *data)
{
	AmalTransition *tr = static_cast<AmalTransition *>(data);
	obs_source_release(tr->browser);
	obs_source_release(tr->video);
	delete tr;
}

void TransitionStart(void *data)
{
	AmalTransition *tr = static_cast<AmalTransition *>(data);

	if (tr->useVideo && tr->video) {
		calldata_t cd = {};
		proc_handler_t *ph = obs_source_get_proc_handler(tr->video);
		if (tr->transitioning || obs_source_media_get_state(tr->video) == OBS_MEDIA_STATE_PLAYING) {
			proc_handler_call(ph, "restart", &cd);
			calldata_free(&cd);
			return;
		}
		proc_handler_call(ph, "get_duration", &cd);
		const long long durationNs = calldata_int(&cd, "duration");
		calldata_free(&cd);
		if (durationNs > 0) {
			tr->durationMs = (uint32_t)(durationNs / 1000000LL) + 250;
		}
		tr->cut = tr->durationMs ? (float)tr->videoCutMs / (float)tr->durationMs : 0.5f;
		obs_transition_enable_fixed(tr->source, true, tr->durationMs);
		obs_source_add_active_child(tr->source, tr->video);
	} else {
		tr->cut = 0.5f;
		obs_transition_enable_fixed(tr->source, true, tr->durationMs);
		if (tr->browser) {
			obs_source_add_active_child(tr->source, tr->browser);
			calldata_t cd = {};
			calldata_set_string(&cd, "eventName", "amalTransition");
			calldata_set_string(&cd, "jsonString", "{}");
			proc_handler_call(obs_source_get_proc_handler(tr->browser), "javascript_event", &cd);
			calldata_free(&cd);
		}
	}

	if (tr->cut < 0.001f) {
		tr->cut = 0.001f;
	} else if (tr->cut > 0.999f) {
		tr->cut = 0.999f;
	}
	tr->transitioning = true;
}

void TransitionStop(void *data)
{
	AmalTransition *tr = static_cast<AmalTransition *>(data);
	if (tr->video) {
		obs_source_remove_active_child(tr->source, tr->video);
		calldata_t cd = {};
		proc_handler_call(obs_source_get_proc_handler(tr->video), "preload_first_frame", &cd);
		calldata_free(&cd);
	}
	if (tr->browser && tr->transitioning) {
		obs_source_remove_active_child(tr->source, tr->browser);
	}
	tr->transitioning = false;
}

void RenderTransition(void *data, gs_effect_t *)
{
	AmalTransition *tr = static_cast<AmalTransition *>(data);

	/* Scène d'origine avant le point de coupure, nouvelle scène après */
	const float t = obs_transition_get_time(tr->source);
	const enum obs_transition_target target = t < tr->cut ? OBS_TRANSITION_SOURCE_A : OBS_TRANSITION_SOURCE_B;
	if (!obs_transition_video_render_direct(tr->source, target)) {
		return;
	}

	obs_source_t *overlay = (tr->useVideo && tr->video) ? tr->video : tr->browser;
	if (!overlay || !tr->transitioning) {
		return;
	}

	const uint32_t cx = obs_source_get_width(tr->source);
	const uint32_t cy = obs_source_get_height(tr->source);
	const uint32_t ox = obs_source_get_width(overlay);
	const uint32_t oy = obs_source_get_height(overlay);
	if (!cx || !cy || !ox || !oy) {
		return;
	}

	const bool previous = gs_set_linear_srgb(true);
	gs_matrix_push();
	gs_matrix_scale3f((float)cx / (float)ox, (float)cy / (float)oy, 1.0f);
	obs_source_video_render(overlay);
	gs_matrix_pop();
	gs_set_linear_srgb(previous);
}

float MixA(void *data, float t)
{
	AmalTransition *tr = static_cast<AmalTransition *>(data);
	const float v = t / tr->cut;
	return v > 1.0f ? 0.0f : 1.0f - v;
}

float MixB(void *data, float t)
{
	AmalTransition *tr = static_cast<AmalTransition *>(data);
	const float v = (1.0f - t) / (1.0f - tr->cut);
	return v > 1.0f ? 0.0f : 1.0f - v;
}

bool RenderAudio(void *data, uint64_t *ts_out, obs_source_audio_mix *audio, uint32_t mixers, size_t channels,
		 size_t sample_rate)
{
	AmalTransition *tr = static_cast<AmalTransition *>(data);
	if (!tr) {
		return false;
	}

	/* Son de la vidéo de l'utilisateur, ajouté au mélange (comme « Stinger ») */
	uint64_t ts = 0;
	const bool withVideo = tr->useVideo && tr->video && tr->transitioning;
	if (withVideo && !obs_source_audio_pending(tr->video)) {
		ts = obs_source_get_audio_timestamp(tr->video);
		if (!ts) {
			return false;
		}
	}

	const bool success =
		obs_transition_audio_render(tr->source, ts_out, audio, mixers, channels, sample_rate, MixA, MixB);
	if (!ts) {
		return success;
	}
	if (!*ts_out || ts < *ts_out) {
		*ts_out = ts;
	}

	obs_source_audio_mix child = {};
	obs_source_get_audio_mix(tr->video, &child);
	for (size_t mix = 0; mix < MAX_AUDIO_MIXES; mix++) {
		if ((mixers & (1u << mix)) == 0) {
			continue;
		}
		for (size_t ch = 0; ch < channels; ch++) {
			float *out = audio->output[mix].data[ch];
			const float *in = child.output[mix].data[ch];
			for (size_t i = 0; i < AUDIO_OUTPUT_FRAMES; i++) {
				out[i] += in[i];
			}
		}
	}
	return true;
}

void EnumActive(void *data, obs_source_enum_proc_t callback, void *param)
{
	AmalTransition *tr = static_cast<AmalTransition *>(data);
	if (!tr->transitioning) {
		return;
	}
	if (tr->useVideo && tr->video) {
		callback(tr->source, tr->video, param);
	} else if (tr->browser) {
		callback(tr->source, tr->browser, param);
	}
}

void EnumAll(void *data, obs_source_enum_proc_t callback, void *param)
{
	AmalTransition *tr = static_cast<AmalTransition *>(data);
	if (tr->browser) {
		callback(tr->source, tr->browser, param);
	}
	if (tr->video) {
		callback(tr->source, tr->video, param);
	}
}

enum gs_color_space GetColorSpace(void *data, size_t, const enum gs_color_space *)
{
	return obs_transition_video_get_color_space(static_cast<AmalTransition *>(data)->source);
}

bool ModeModified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool video = strcmp(obs_data_get_string(settings, "mode"), "video") == 0;
	for (const char *name : {"style", "logo", "texte", "duree", "couleur_principale", "couleur_accent"}) {
		obs_property_set_visible(obs_properties_get(props, name), !video);
	}
	obs_property_set_visible(obs_properties_get(props, "video"), video);
	obs_property_set_visible(obs_properties_get(props, "point_coupure"), video);
	return true;
}

obs_properties_t *TransitionProperties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *mode = obs_properties_add_list(props, "mode", obs_module_text("Mode"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(mode, obs_module_text("Mode.Animation"), "animation");
	obs_property_list_add_string(mode, obs_module_text("Mode.Video"), "video");
	obs_property_set_modified_callback(mode, ModeModified);

	obs_property_t *style = obs_properties_add_list(props, "style", obs_module_text("Style"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(style, obs_module_text("Style.Wipe"), "volet");
	obs_property_list_add_string(style, obs_module_text("Style.Curtain"), "rideau");
	obs_property_list_add_string(style, obs_module_text("Style.Circle"), "logo");
	obs_property_list_add_string(style, obs_module_text("Style.Shards"), "eclats");
	obs_property_list_add_string(style, obs_module_text("Style.Ribbons"), "rubans");
	obs_property_list_add_string(style, obs_module_text("Style.Diamond"), "losange");

	obs_properties_add_path(props, "logo", obs_module_text("Logo"), OBS_PATH_FILE,
				"Images (*.png *.jpg *.jpeg *.webp *.svg *.gif)", nullptr);
	obs_properties_add_text(props, "texte", obs_module_text("Text"), OBS_TEXT_DEFAULT);
	obs_property_t *duration =
		obs_properties_add_int_slider(props, "duree", obs_module_text("Duration"), 600, 4000, 100);
	obs_property_int_set_suffix(duration, " ms");
	obs_properties_add_color(props, "couleur_principale", obs_module_text("Color.Primary"));
	obs_properties_add_color(props, "couleur_accent", obs_module_text("Color.Accent"));

	obs_properties_add_path(props, "video", obs_module_text("Video"), OBS_PATH_FILE,
				"Vidéos (*.webm *.mov *.mp4 *.mkv);;Tous les fichiers (*.*)", nullptr);
	obs_property_t *cut =
		obs_properties_add_int(props, "point_coupure", obs_module_text("CutPoint"), 0, 30000, 50);
	obs_property_int_set_suffix(cut, " ms");
	obs_property_set_long_description(cut, obs_module_text("CutPoint.Help"));

	return props;
}

void TransitionDefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "mode", "animation");
	obs_data_set_default_string(settings, "style", "volet");
	obs_data_set_default_int(settings, "duree", 1400);
	obs_data_set_default_int(settings, "point_coupure", 700);
	obs_data_set_default_int(settings, "couleur_principale", 0xFF4F6B0B); /* #0B6B4F */
	obs_data_set_default_int(settings, "couleur_accent", 0xFF37AFD4);     /* #D4AF37 */
}

const char *TransitionName(void *)
{
	return obs_module_text("Transition.Name");
}

/* ------------------------------------------------------------------------ */
/* 2. Panneau « Relecture »                                                 */

class ReplayDock;
QPointer<ReplayDock> dockWidget;
obs_hotkey_id replayHotkey = OBS_INVALID_HOTKEY_ID;

/* Transition Amal de la liste des transitions (créée au besoin) */
obs_source_t *FindAmalTransition()
{
	obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	obs_source_t *found = nullptr;
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *tr = list.sources.array[i];
		const char *id = obs_source_get_unversioned_id(tr);
		if (!found && id && strcmp(id, TRANSITION_ID) == 0) {
			found = obs_source_get_ref(tr);
		}
	}
	obs_frontend_source_list_free(&list);
	return found;
}

void EnsureAmalTransition()
{
	OBSSourceAutoRelease existing = FindAmalTransition();
	if (existing) {
		return;
	}
	QObject *main = static_cast<QObject *>(obs_frontend_get_main_window());
	QMetaObject::invokeMethod(main, "AmalAddTransition", Qt::DirectConnection,
				  Q_ARG(QString, QString::fromUtf8(TRANSITION_ID)), Q_ARG(QString, T("Transition.Default")));
}

class ReplayDock : public QWidget {
public:
	explicit ReplayDock(QWidget *parent) : QWidget(parent)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(8, 8, 8, 8);

		QLabel *help = new QLabel(T("Dock.Help"), this);
		help->setWordWrap(true);
		layout->addWidget(help);

		bufferStatus = new QLabel(this);
		bufferStatus->setWordWrap(true);
		layout->addWidget(bufferStatus);
		bufferButton = new QPushButton(T("Buffer.Start"), this);
		connect(bufferButton, &QPushButton::clicked, this, [this] {
			if (obs_frontend_replay_buffer_active()) {
				obs_frontend_replay_buffer_stop();
			} else {
				obs_frontend_replay_buffer_start();
			}
		});
		layout->addWidget(bufferButton);

		QHBoxLayout *speedRow = new QHBoxLayout;
		speedRow->addWidget(new QLabel(T("Speed"), this));
		speedCombo = new QComboBox(this);
		speedCombo->addItem(T("Speed.Normal"), 100);
		speedCombo->addItem(T("Speed.75"), 75);
		speedCombo->addItem(T("Speed.50"), 50);
		speedCombo->addItem(T("Speed.25"), 25);
		speedCombo->setCurrentIndex(2);
		speedRow->addWidget(speedCombo, 1);
		layout->addLayout(speedRow);

		QHBoxLayout *labelRow = new QHBoxLayout;
		labelRow->addWidget(new QLabel(T("Badge"), this));
		badgeEdit = new QLineEdit(T("Badge.Default"), this);
		labelRow->addWidget(badgeEdit, 1);
		layout->addLayout(labelRow);

		useTransition = new QCheckBox(T("UseTransition"), this);
		useTransition->setChecked(true);
		layout->addWidget(useTransition);

		replayButton = new QPushButton(T("Replay"), this);
		replayButton->setMinimumHeight(48);
		QFont font = replayButton->font();
		font.setBold(true);
		font.setPointSizeF(font.pointSizeF() * 1.3);
		replayButton->setFont(font);
		connect(replayButton, &QPushButton::clicked, this, &ReplayDock::requestReplay);
		layout->addWidget(replayButton);

		backButton = new QPushButton(T("Back"), this);
		connect(backButton, &QPushButton::clicked, this, &ReplayDock::returnToLive);
		layout->addWidget(backButton);

		status = new QLabel(this);
		status->setWordWrap(true);
		layout->addWidget(status);
		layout->addStretch(1);

		refreshBuffer();
	}

	void refreshBuffer()
	{
		const bool active = obs_frontend_replay_buffer_active();
		bufferStatus->setText(active ? T("Buffer.On") : T("Buffer.Off"));
		bufferButton->setText(active ? T("Buffer.Stop") : T("Buffer.Start"));
		replayButton->setEnabled(active && !playing);
		backButton->setEnabled(playing);
	}

	void requestReplay()
	{
		if (!obs_frontend_replay_buffer_active()) {
			status->setText(T("Error.BufferOff"));
			return;
		}
		if (playing || pending) {
			return;
		}
		pending = true;
		status->setText(T("Status.Saving"));
		obs_frontend_replay_buffer_save();
	}

	/* La mémoire de relecture a écrit son fichier : on le diffuse */
	void replaySaved()
	{
		if (!pending) {
			return;
		}
		pending = false;

		char *file = obs_frontend_get_last_replay();
		const std::string path = file ? file : "";
		bfree(file);
		if (path.empty() || !os_file_exists(path.c_str())) {
			status->setText(T("Error.NoFile"));
			return;
		}

		obs_video_info ovi = {};
		if (!obs_get_video_info(&ovi)) {
			return;
		}

		/* Scène de relecture : vidéo plein écran + badge */
		const QString sceneName = T("Scene.Name");
		OBSSourceAutoRelease sceneSource = obs_get_source_by_name(sceneName.toUtf8().constData());
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		OBSSceneAutoRelease created;
		if (!scene) {
			if (sceneSource) {
				status->setText(T("Error.NameTaken").arg(sceneName));
				return;
			}
			created = obs_scene_create(sceneName.toUtf8().constData());
			scene = created;
		}

		OBSDataAutoRelease media = obs_data_create();
		obs_data_set_bool(media, "is_local_file", true);
		obs_data_set_string(media, "local_file", path.c_str());
		obs_data_set_bool(media, "looping", false);
		obs_data_set_bool(media, "restart_on_activate", true);
		obs_data_set_bool(media, "close_when_inactive", true);
		obs_data_set_bool(media, "clear_on_media_end", false);
		obs_data_set_int(media, "speed_percent", speedCombo->currentData().toInt());

		const QString mediaName = T("Source.Video");
		OBSSourceAutoRelease video = obs_get_source_by_name(mediaName.toUtf8().constData());
		if (video) {
			obs_source_update(video, media);
		} else {
			video = obs_source_create("ffmpeg_source", mediaName.toUtf8().constData(), media, nullptr);
		}
		if (!video) {
			status->setText(T("Error.NoFile"));
			return;
		}
		if (!obs_scene_find_source(scene, mediaName.toUtf8().constData())) {
			obs_sceneitem_t *item = obs_scene_add(scene, video);
			vec2 bounds;
			vec2_set(&bounds, (float)ovi.base_width, (float)ovi.base_height);
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
			obs_sceneitem_set_bounds(item, &bounds);
		}

		/* Badge « Ralenti » / « Relecture » (Bandeau TV réduit au badge) */
		const QString badgeName = T("Source.Badge");
		OBSDataAutoRelease badge = obs_data_create();
		obs_data_set_string(badge, "modele", "direct");
		obs_data_set_string(badge, "titre", "");
		obs_data_set_string(badge, "sous_titre", "");
		obs_data_set_string(badge, "format_date", "aucune");
		obs_data_set_bool(badge, "horloge", false);
		obs_data_set_bool(badge, "bande", false);
		obs_data_set_string(badge, "info", "");
		const QString label = badgeEdit->text().trimmed();
		obs_data_set_bool(badge, "badge_direct", !label.isEmpty());
		obs_data_set_string(badge, "libelle", label.toUtf8().constData());
		OBSSourceAutoRelease badgeSource = obs_get_source_by_name(badgeName.toUtf8().constData());
		if (badgeSource) {
			obs_source_update(badgeSource, badge);
		} else {
			badgeSource = obs_source_create(GRAPHIC_ID, badgeName.toUtf8().constData(), badge, nullptr);
		}
		if (badgeSource && !obs_scene_find_source(scene, badgeName.toUtf8().constData())) {
			obs_sceneitem_t *item = obs_scene_add(scene, badgeSource);
			obs_sceneitem_set_locked(item, true);
		}

		/* Fin de la vidéo -> retour automatique */
		if (connectedVideo.Get() != video.Get()) {
			disconnectVideo();
			signal_handler_connect(obs_source_get_signal_handler(video), "media_ended", MediaEnded, this);
			connectedVideo = video.Get();
		}

		/* Mémoriser la scène et la transition d'origine, puis basculer */
		OBSSourceAutoRelease current = obs_frontend_get_current_scene();
		previousScene = OBSGetWeakRef(current.Get());
		OBSSourceAutoRelease currentTransition = obs_frontend_get_current_transition();
		previousTransition = OBSGetWeakRef(currentTransition.Get());

		switchWithTransition(obs_scene_get_source(scene));
		playing = true;
		refreshBuffer();
		status->setText(T("Status.Playing").arg(speedCombo->currentText()));
	}

	void returnToLive()
	{
		if (!playing) {
			return;
		}
		playing = false;
		OBSSource scene = OBSGetStrongRef(previousScene);
		if (scene) {
			switchWithTransition(scene);
		}
		restoreTransitionPending = true;
		refreshBuffer();
		status->setText(T("Status.Back"));
	}

	void transitionStopped()
	{
		if (!restoreTransitionPending) {
			return;
		}
		restoreTransitionPending = false;
		OBSSource transition = OBSGetStrongRef(previousTransition);
		if (transition) {
			obs_frontend_set_current_transition(transition);
		}
	}

	void save(obs_data_t *obj) const
	{
		obs_data_set_int(obj, "vitesse", speedCombo->currentData().toInt());
		obs_data_set_string(obj, "badge", badgeEdit->text().toUtf8().constData());
		obs_data_set_bool(obj, "transition", useTransition->isChecked());
	}

	void load(obs_data_t *obj)
	{
		if (!obj) {
			return;
		}
		const int index = speedCombo->findData((int)obs_data_get_int(obj, "vitesse"));
		if (index >= 0) {
			speedCombo->setCurrentIndex(index);
		}
		if (obs_data_has_user_value(obj, "badge")) {
			badgeEdit->setText(QString::fromUtf8(obs_data_get_string(obj, "badge")));
		}
		if (obs_data_has_user_value(obj, "transition")) {
			useTransition->setChecked(obs_data_get_bool(obj, "transition"));
		}
	}

	~ReplayDock() override { disconnectVideo(); }

private:
	void disconnectVideo()
	{
		if (connectedVideo) {
			signal_handler_disconnect(obs_source_get_signal_handler(connectedVideo), "media_ended", MediaEnded,
						  this);
			connectedVideo = nullptr;
		}
	}

	static void MediaEnded(void *data, calldata_t *)
	{
		ReplayDock *dock = static_cast<ReplayDock *>(data);
		QMetaObject::invokeMethod(dock, [dock] { dock->returnToLive(); }, Qt::QueuedConnection);
	}

	void switchWithTransition(obs_source_t *scene)
	{
		if (useTransition->isChecked()) {
			OBSSourceAutoRelease amal = FindAmalTransition();
			if (amal) {
				obs_frontend_set_current_transition(amal);
			}
		}
		obs_frontend_set_current_scene(scene);
	}

	QLabel *bufferStatus = nullptr;
	QPushButton *bufferButton = nullptr;
	QComboBox *speedCombo = nullptr;
	QLineEdit *badgeEdit = nullptr;
	QCheckBox *useTransition = nullptr;
	QPushButton *replayButton = nullptr;
	QPushButton *backButton = nullptr;
	QLabel *status = nullptr;

	bool pending = false;
	bool playing = false;
	bool restoreTransitionPending = false;
	OBSSource connectedVideo;
	OBSWeakSource previousScene;
	OBSWeakSource previousTransition;
};

void HotkeyReplay(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && dockWidget) {
		QMetaObject::invokeMethod(
			dockWidget,
			[] {
				if (dockWidget) {
					dockWidget->requestReplay();
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
		OBSDataArrayAutoRelease hotkey = obs_hotkey_save(replayHotkey);
		obs_data_set_array(obj, "raccourci", hotkey);
		obs_data_set_obj(saveData, SAVE_KEY, obj);
		return;
	}

	OBSDataAutoRelease obj = obs_data_get_obj(saveData, SAVE_KEY);
	if (obj) {
		OBSDataArrayAutoRelease hotkey = obs_data_get_array(obj, "raccourci");
		obs_hotkey_load(replayHotkey, hotkey);
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
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		ShowDockOnFirstRun();
		EnsureAmalTransition();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		EnsureAmalTransition();
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
		if (dockWidget) {
			dockWidget->replaySaved();
		}
		break;
	case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
		if (dockWidget) {
			dockWidget->transitionStopped();
		}
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		if (dockWidget) {
			dockWidget->refreshBuffer();
		}
		break;
	default:
		break;
	}
}

} // namespace

bool obs_module_load(void)
{
	obs_source_info info = {};
	info.id = TRANSITION_ID;
	info.type = OBS_SOURCE_TYPE_TRANSITION;
	info.get_name = TransitionName;
	info.create = CreateTransition;
	info.destroy = DestroyTransition;
	info.update = UpdateTransition;
	info.get_defaults = TransitionDefaults;
	info.get_properties = TransitionProperties;
	info.video_render = RenderTransition;
	info.audio_render = RenderAudio;
	info.enum_active_sources = EnumActive;
	info.enum_all_sources = EnumAll;
	info.transition_start = TransitionStart;
	info.transition_stop = TransitionStop;
	info.video_get_color_space = GetColorSpace;
	obs_register_source(&info);

	QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	QScrollArea *scroll = new QScrollArea(main);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	dockWidget = new ReplayDock(scroll);
	scroll->setWidget(dockWidget);
	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), scroll)) {
		delete scroll;
	}

	replayHotkey = obs_hotkey_register_frontend("amal_relecture", obs_module_text("Hotkey.Replay"), HotkeyReplay,
						    nullptr);
	obs_frontend_add_save_callback(SaveCallback, nullptr);
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	blog(LOG_INFO, "[amal-transitions] Transition Amal et panneau Relecture chargés");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	obs_frontend_remove_save_callback(SaveCallback, nullptr);
	obs_hotkey_unregister(replayHotkey);
}
