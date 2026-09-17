/**
 * Shared helpers of the interface modules (ui.c, ui_library.c, ui_images.c, …).
 * Not part of the platform interface – that is ui.h.
 */
#pragma once

#include "theme.h"
#include "ui.h"
/* lv_image_cache_drop(): not reachable through lvgl.h; MSVC overlooks that, GCC on the ESP32 doesn't */
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/draw/lv_image_decoder_private.h"

/* The first views have a tab in the top bar, the artist page doesn't */
enum { VIEW_NOW, VIEW_LIBRARY, VIEW_DECK, VIEW_AUDIO, TAB_COUNT, VIEW_ARTIST = TAB_COUNT, VIEW_COUNT };

/* Building blocks */
lv_obj_t * ui_plain_obj(lv_obj_t * parent);
lv_obj_t * ui_text_label(lv_obj_t * parent, const lv_font_t * font, lv_color_t color, const char * text);
void ui_set_hidden(lv_obj_t * obj, bool hidden);

/* State and commands */
void ui_show_view(int view);
bool ui_is_connected(void);
/** Command to the PC app while a connection exists (value_json may be NULL). */
void ui_send_command(const char * cmd, const char * value_json);

/* Pictures */
bool ui_jpeg_size(const uint8_t * data, size_t len, uint16_t * w, uint16_t * h);
const lv_image_dsc_t * ui_image_get(const char * id);
/** Decode a JPEG once to RGB565; free with ui_image_free(). NULL on error. */
lv_draw_buf_t * ui_jpeg_decode(const uint8_t * jpeg, size_t len);
void ui_image_free(lv_draw_buf_t * buf);

/* Playlists */
void ui_library_build(lv_obj_t * view);
void ui_library_on_show(void);
void ui_library_on_disconnect(void);
void ui_library_image_arrived(const char * id);
void ui_library_image_dropped(const char * id);
/* Knob on the playlists page: turn = choose a card, press = play */
void ui_library_knob_rotate(int32_t steps);
void ui_library_knob_press(void);

/* Key page */
void ui_deck_build(lv_obj_t * view);
void ui_deck_on_show(void);
void ui_deck_on_disconnect(void);
void ui_deck_knob_rotate(int32_t steps);
void ui_deck_knob_press(void);

/* Start animation (sits on top of everything and cleans up after itself) */
void ui_splash_start(void);
/** Loading screen while no PC app is connected (animated logo); fades out on connect. */
void ui_waiting_show(bool show);

/* Audio */
void ui_audio_build(lv_obj_t * view);
void ui_audio_on_show(void);
void ui_audio_on_hide(void);
void ui_audio_on_disconnect(void);
void ui_audio_knob_rotate(int32_t steps);
void ui_audio_knob_press(void);

/* Artist page */
void ui_artist_build(lv_obj_t * view);
void ui_artist_open(const char * id, const char * name);
void ui_artist_image_arrived(const char * id);
void ui_artist_image_dropped(const char * id);
void ui_artist_knob_rotate(int32_t steps);
void ui_artist_knob_press(void);
