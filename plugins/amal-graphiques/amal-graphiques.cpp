/******************************************************************************
    Amal Studio - source « Amal Graphique » (habillages islamiques)
    Copyright (C) 2026 Amal Ben Aly

    La source affiche un modèle graphique (verset, hadith, invocation, rappel,
    intervenant, titre de conférence, bandeau défilant). Le dessin est confié
    à une source navigateur interne (Chromium), qui gère parfaitement l'arabe :
    écriture de droite à gauche, ligatures et signes diacritiques.

    Aucun texte religieux n'est inventé ici : la seule valeur par défaut est la
    Basmala (Al-Fâtiha, 1:1, texte Tanzil). Tout le reste est saisi par
    l'utilisateur ou viendra de la bibliothèque vérifiée.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include <obs-module.h>
#include <util/bmem.h>
#include <util/dstr.h>

#include <cstdint>
#include <cstdio>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("amal-graphiques", "fr-FR")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Amal Studio : habillages graphiques islamiques";
}

namespace {

constexpr const char *SOURCE_ID = "amal_graphique";
constexpr const char *BROWSER_ID = "browser_source";

/* Basmala, texte uthmani du projet Tanzil (Al-Fâtiha, 1:1) */
constexpr const char *BASMALA = "\xD8\xA8\xD9\x90\xD8\xB3\xD9\x92\xD9\x85\xD9\x90 \xD9\xB1\xD9\x84\xD9\x84\xD9\x91\xD9\x8E"
				"\xD9\x87\xD9\x90 \xD9\xB1\xD9\x84\xD8\xB1\xD9\x91\xD9\x8E\xD8\xAD\xD9\x92\xD9\x85\xD9\x8E"
				"\xD9\xB0\xD9\x86\xD9\x90 \xD9\xB1\xD9\x84\xD8\xB1\xD9\x91\xD9\x8E\xD8\xAD\xD9\x90\xD9\x8A"
				"\xD9\x85\xD9\x90";

struct AmalGraphic {
	obs_source_t *source = nullptr;
	obs_source_t *browser = nullptr;
	uint32_t width = 1920;
	uint32_t height = 1080;
	unsigned replay = 0;
};

/* Encode une chaîne UTF-8 pour une adresse (RFC 3986), en gardant les caractères sûrs. */
std::string PercentEncode(const std::string &input, const char *keep)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(input.size() * 3);
	for (unsigned char c : input) {
		const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
					c == '-' || c == '_' || c == '.' || c == '~';
		if (unreserved || (c < 0x80 && keep && std::string(keep).find((char)c) != std::string::npos)) {
			out += (char)c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0x0F];
		}
	}
	return out;
}

/* Couleur OBS (0xAABBGGRR) vers « #RRGGBB » */
std::string ColorToHex(long long value)
{
	const uint32_t v = (uint32_t)value;
	char buffer[8];
	snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF);
	return buffer;
}

/* Adresse de la page des modèles, servie par le gestionnaire de fichiers locaux du navigateur */
std::string OverlayBaseUrl()
{
	char *path = obs_module_file("overlay/overlay.html");
	if (!path) {
		blog(LOG_ERROR, "[amal-graphiques] Fichier overlay/overlay.html introuvable");
		return std::string();
	}

	std::string file(path);
	bfree(path);
	for (char &c : file) {
		if (c == '\\') {
			c = '/';
		}
	}

	/* Chemin Windows « C:/… » ou chemin absolu Unix « /… » */
	if (!file.empty() && file[0] == '/') {
		file.erase(0, 1);
	}
	return "http://absolute/" + PercentEncode(file, "/:");
}

std::string BuildUrl(AmalGraphic *g, obs_data_t *settings)
{
	const std::string base = OverlayBaseUrl();
	if (base.empty()) {
		return std::string();
	}

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "t", obs_data_get_string(settings, "modele"));
	obs_data_set_string(data, "label", obs_data_get_string(settings, "libelle"));
	obs_data_set_string(data, "ar", obs_data_get_string(settings, "texte_arabe"));
	obs_data_set_string(data, "fr", obs_data_get_string(settings, "texte"));
	obs_data_set_string(data, "ref", obs_data_get_string(settings, "reference"));
	obs_data_set_string(data, "title", obs_data_get_string(settings, "titre"));
	obs_data_set_string(data, "subtitle", obs_data_get_string(settings, "sous_titre"));
	obs_data_set_string(data, "pos", obs_data_get_string(settings, "position"));
	obs_data_set_string(data, "anim", obs_data_get_string(settings, "animation"));
	obs_data_set_int(data, "dur", obs_data_get_int(settings, "duree"));
	obs_data_set_int(data, "speed", obs_data_get_int(settings, "vitesse"));
	obs_data_set_double(data, "scale", (double)obs_data_get_int(settings, "taille") / 100.0);
	obs_data_set_string(data, "primary", ColorToHex(obs_data_get_int(settings, "couleur_principale")).c_str());
	obs_data_set_string(data, "accent", ColorToHex(obs_data_get_int(settings, "couleur_accent")).c_str());
	obs_data_set_string(data, "text", ColorToHex(obs_data_get_int(settings, "couleur_texte")).c_str());
	/* Change l'adresse pour rejouer l'animation à la demande */
	obs_data_set_int(data, "n", g->replay);

	const std::string json = obs_data_get_json(data);
	obs_data_release(data);

	return base + "#" + PercentEncode(json, nullptr);
}

void UpdateBrowser(AmalGraphic *g, obs_data_t *settings)
{
	if (!g->browser) {
		return;
	}

	obs_video_info ovi = {};
	if (obs_get_video_info(&ovi) && ovi.base_width && ovi.base_height) {
		g->width = ovi.base_width;
		g->height = ovi.base_height;
	}

	obs_data_t *browserSettings = obs_data_create();
	obs_data_set_bool(browserSettings, "is_local_file", false);
	obs_data_set_string(browserSettings, "url", BuildUrl(g, settings).c_str());
	/* La page est conçue en 1920 x 1080 et s'adapte à la taille de l'image (720p, 1440p…) */
	obs_data_set_int(browserSettings, "width", g->width);
	obs_data_set_int(browserSettings, "height", g->height);
	obs_data_set_bool(browserSettings, "fps_custom", false);
	obs_data_set_bool(browserSettings, "shutdown", false);
	obs_data_set_bool(browserSettings, "restart_when_active", true);
	obs_data_set_bool(browserSettings, "reroute_audio", false);
	obs_data_set_string(browserSettings, "css", "");
	obs_source_update(g->browser, browserSettings);
	obs_data_release(browserSettings);
}

/* ------------------------------------------------------------------------ */
/* Propriétés                                                               */

bool TemplateModified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const std::string t = obs_data_get_string(settings, "modele");
	const bool panel = t == "verset" || t == "hadith" || t == "invocation" || t == "rappel";
	const bool speaker = t == "intervenant";
	const bool title = t == "titre";
	const bool ticker = t == "bandeau";

	obs_property_set_visible(obs_properties_get(props, "libelle"), panel || ticker);
	obs_property_set_visible(obs_properties_get(props, "texte_arabe"), panel || speaker || title);
	obs_property_set_visible(obs_properties_get(props, "texte"), panel || ticker);
	obs_property_set_visible(obs_properties_get(props, "reference"), panel || title);
	obs_property_set_visible(obs_properties_get(props, "titre"), speaker || title);
	obs_property_set_visible(obs_properties_get(props, "sous_titre"), speaker || title);
	obs_property_set_visible(obs_properties_get(props, "vitesse"), ticker);
	obs_property_set_visible(obs_properties_get(props, "position"), !speaker);
	return true;
}

bool ReplayClicked(obs_properties_t *, obs_property_t *, void *data)
{
	AmalGraphic *g = static_cast<AmalGraphic *>(data);
	g->replay++;
	obs_data_t *settings = obs_source_get_settings(g->source);
	UpdateBrowser(g, settings);
	obs_data_release(settings);
	return false;
}

obs_properties_t *GetProperties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *list = obs_properties_add_list(props, "modele", obs_module_text("Template"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(list, obs_module_text("Template.Verse"), "verset");
	obs_property_list_add_string(list, obs_module_text("Template.Hadith"), "hadith");
	obs_property_list_add_string(list, obs_module_text("Template.Invocation"), "invocation");
	obs_property_list_add_string(list, obs_module_text("Template.Reminder"), "rappel");
	obs_property_list_add_string(list, obs_module_text("Template.Speaker"), "intervenant");
	obs_property_list_add_string(list, obs_module_text("Template.Title"), "titre");
	obs_property_list_add_string(list, obs_module_text("Template.Ticker"), "bandeau");
	obs_property_set_modified_callback(list, TemplateModified);

	obs_properties_add_text(props, "libelle", obs_module_text("Label"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "texte_arabe", obs_module_text("ArabicText"), OBS_TEXT_MULTILINE);
	obs_properties_add_text(props, "texte", obs_module_text("Text"), OBS_TEXT_MULTILINE);
	obs_properties_add_text(props, "reference", obs_module_text("Reference"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "titre", obs_module_text("TitleField"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "sous_titre", obs_module_text("Subtitle"), OBS_TEXT_DEFAULT);

	obs_property_t *pos = obs_properties_add_list(props, "position", obs_module_text("Position"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(pos, obs_module_text("Position.Bottom"), "bas");
	obs_property_list_add_string(pos, obs_module_text("Position.Top"), "haut");
	obs_property_list_add_string(pos, obs_module_text("Position.Center"), "centre");

	obs_property_t *anim = obs_properties_add_list(props, "animation", obs_module_text("Animation"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(anim, obs_module_text("Animation.Slide"), "glisser");
	obs_property_list_add_string(anim, obs_module_text("Animation.Fade"), "fondu");
	obs_property_list_add_string(anim, obs_module_text("Animation.None"), "aucune");

	obs_property_t *dur = obs_properties_add_int(props, "duree", obs_module_text("Duration"), 0, 3600, 1);
	obs_property_int_set_suffix(dur, " s");
	obs_property_set_long_description(dur, obs_module_text("Duration.Help"));

	obs_property_t *speed = obs_properties_add_int_slider(props, "vitesse", obs_module_text("Speed"), 40, 600, 10);
	obs_property_int_set_suffix(speed, " px/s");

	obs_property_t *size = obs_properties_add_int_slider(props, "taille", obs_module_text("Size"), 30, 200, 5);
	obs_property_int_set_suffix(size, " %");

	obs_properties_add_color(props, "couleur_principale", obs_module_text("Color.Primary"));
	obs_properties_add_color(props, "couleur_accent", obs_module_text("Color.Accent"));
	obs_properties_add_color(props, "couleur_texte", obs_module_text("Color.Text"));

	obs_properties_add_button2(props, "rejouer", obs_module_text("Replay"), ReplayClicked, data);

	return props;
}

void GetDefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "modele", "verset");
	obs_data_set_default_string(settings, "texte_arabe", BASMALA);
	obs_data_set_default_string(settings, "reference", "Al-F\xC3\xA2tiha (1:1)");
	obs_data_set_default_string(settings, "position", "bas");
	obs_data_set_default_string(settings, "animation", "glisser");
	obs_data_set_default_int(settings, "duree", 0);
	obs_data_set_default_int(settings, "vitesse", 140);
	obs_data_set_default_int(settings, "taille", 100);
	obs_data_set_default_int(settings, "couleur_principale", 0xFF4F6B0B); /* #0B6B4F */
	obs_data_set_default_int(settings, "couleur_accent", 0xFF37AFD4);     /* #D4AF37 */
	obs_data_set_default_int(settings, "couleur_texte", 0xFFFFFFFF);      /* #FFFFFF */
}

/* ------------------------------------------------------------------------ */
/* Cycle de vie de la source                                                */

void *Create(obs_data_t *settings, obs_source_t *source)
{
	AmalGraphic *g = new AmalGraphic();
	g->source = source;

	g->browser = obs_source_create_private(BROWSER_ID, "amal-graphique-rendu", nullptr);
	if (!g->browser) {
		blog(LOG_ERROR, "[amal-graphiques] Source navigateur indisponible : l'habillage ne peut pas s'afficher");
	}

	UpdateBrowser(g, settings);
	return g;
}

void Destroy(void *data)
{
	AmalGraphic *g = static_cast<AmalGraphic *>(data);
	if (g->browser) {
		obs_source_release(g->browser);
	}
	delete g;
}

void Update(void *data, obs_data_t *settings)
{
	UpdateBrowser(static_cast<AmalGraphic *>(data), settings);
}

void Render(void *data, gs_effect_t *)
{
	AmalGraphic *g = static_cast<AmalGraphic *>(data);
	if (g->browser) {
		obs_source_video_render(g->browser);
	}
}

uint32_t GetWidth(void *data)
{
	AmalGraphic *g = static_cast<AmalGraphic *>(data);
	return g->browser ? obs_source_get_width(g->browser) : 0;
}

uint32_t GetHeight(void *data)
{
	AmalGraphic *g = static_cast<AmalGraphic *>(data);
	return g->browser ? obs_source_get_height(g->browser) : 0;
}

/* Déclare la source navigateur comme « enfant » : le moteur l'affiche et l'active avec la source. */
void EnumSources(void *data, obs_source_enum_proc_t callback, void *param)
{
	AmalGraphic *g = static_cast<AmalGraphic *>(data);
	if (g->browser) {
		callback(g->source, g->browser, param);
	}
}

const char *GetName(void *)
{
	return obs_module_text("SourceName");
}

} // namespace

bool obs_module_load(void)
{
	obs_source_info info = {};
	info.id = SOURCE_ID;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	info.get_name = GetName;
	info.create = Create;
	info.destroy = Destroy;
	info.update = Update;
	info.get_defaults = GetDefaults;
	info.get_properties = GetProperties;
	info.video_render = Render;
	info.get_width = GetWidth;
	info.get_height = GetHeight;
	info.enum_active_sources = EnumSources;
	info.enum_all_sources = EnumSources;

	obs_register_source(&info);
	blog(LOG_INFO, "[amal-graphiques] Source « Amal Graphique » disponible");
	return true;
}
