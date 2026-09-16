/**
 * Gemeinsame Hilfen der Oberflächen-Module (ui.c, ui_library.c, ui_images.c).
 * Nicht Teil der Plattform-Schnittstelle – die steht in ui.h.
 */
#pragma once

#include "theme.h"
#include "ui.h"
/* lv_image_cache_drop(): nicht über lvgl.h erreichbar; MSVC übersieht das, GCC auf dem ESP32 nicht */
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/draw/lv_image_decoder_private.h"

/* Die ersten Ansichten haben einen Reiter in der Kopfleiste, die Künstlerseite nicht */
enum { VIEW_NOW, VIEW_LIBRARY, VIEW_DECK, VIEW_AUDIO, TAB_COUNT, VIEW_ARTIST = TAB_COUNT, VIEW_COUNT };

/* Bausteine */
lv_obj_t * ui_plain_obj(lv_obj_t * parent);
lv_obj_t * ui_text_label(lv_obj_t * parent, const lv_font_t * font, lv_color_t color, const char * text);
void ui_set_hidden(lv_obj_t * obj, bool hidden);

/* Zustand und Befehle */
void ui_show_view(int view);
bool ui_is_connected(void);
/** Befehl an die PC-App, sobald eine Verbindung besteht (value_json darf NULL sein). */
void ui_send_command(const char * cmd, const char * value_json);

/* Bilder */
bool ui_jpeg_size(const uint8_t * data, size_t len, uint16_t * w, uint16_t * h);
const lv_image_dsc_t * ui_image_get(const char * id);
/** JPEG einmalig nach RGB565 dekodieren; Freigabe mit ui_image_free(). NULL bei Fehler. */
lv_draw_buf_t * ui_jpeg_decode(const uint8_t * jpeg, size_t len);
void ui_image_free(lv_draw_buf_t * buf);

/* Bibliothek */
void ui_library_build(lv_obj_t * view);
void ui_library_on_show(void);
void ui_library_on_disconnect(void);
void ui_library_image_arrived(const char * id);
void ui_library_image_dropped(const char * id);
/* Knauf in der Bibliothek: drehen = Karte wählen, drücken = abspielen */
void ui_library_knob_rotate(int32_t steps);
void ui_library_knob_press(void);

/* Tastenseite */
void ui_deck_build(lv_obj_t * view);
void ui_deck_on_show(void);
void ui_deck_on_disconnect(void);
void ui_deck_knob_rotate(int32_t steps);
void ui_deck_knob_press(void);

/* Startanimation (liegt über allem und räumt sich selbst weg) */
void ui_splash_start(void);

/* Audio */
void ui_audio_build(lv_obj_t * view);
void ui_audio_on_show(void);
void ui_audio_on_hide(void);
void ui_audio_on_disconnect(void);
void ui_audio_knob_rotate(int32_t steps);
void ui_audio_knob_press(void);

/* Künstlerseite */
void ui_artist_build(lv_obj_t * view);
void ui_artist_open(const char * id, const char * name);
void ui_artist_image_arrived(const char * id);
void ui_artist_image_dropped(const char * id);
void ui_artist_knob_rotate(int32_t steps);
void ui_artist_knob_press(void);
