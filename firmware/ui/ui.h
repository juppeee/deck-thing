/**
 * Device interface (LVGL 9). Runs unchanged in the Windows simulator and on the ESP32-S3.
 *
 * The platform calls ui_init() once and then reports knob, connection and new data.
 * Until data arrives from the PC, the interface shows sample tracks.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

/** An artist with Spotify id (for the artist page). */
typedef struct {
    const char * id;
    const char * name;
} ui_artist_ref_t;

/** Playback state as the PC app sends it. */
typedef struct {
    const ui_artist_ref_t * artist_refs; /* only with a Spotify login; else NULL */
    size_t artist_ref_count;
    const char * title;     /* song title */
    const char * artists;   /* „Lena Kessler, Mira Holt, Jonas Feld“ */
    const char * context;   /* context, e.g. "Lieblingssongs"; empty = hide */
    float pos_s;            /* position in seconds */
    float dur_s;            /* length in seconds */
    bool playing;
    uint8_t shuffle;        /* 0 off, 1 shuffle, 2 Smart Shuffle */
    uint8_t repeat;         /* 0 off, 1 playlist, 2 track */
    bool spotify_login;     /* no like button without a login */
    int8_t liked;           /* -1 unknown right now (e.g. just after a track change), 0 no, 1 yes */
    int8_t volume;          /* 0–100, -1 unknown */
    bool muted;
    lv_color_t color;       /* base colour of the cover for the background gradient */
} ui_playback_t;

/** Build the interface. knob may be NULL (no knob connected). */
void ui_init(lv_display_t * display, lv_indev_t * knob);

/** Knob on the real device: detent steps (+ = louder) and press. */
void ui_knob_rotate(int32_t steps);
void ui_knob_press(void);

/** Knob plugged in or out (shows/hides the volume button). */
void ui_set_knob_present(bool present);

/** Link to the PC app established or lost (lost = back to the sample tracks). */
void ui_set_connected(bool connected);

/** New playback state from the PC app. */
void ui_set_playback(const ui_playback_t * pb);

/** Cover as JPEG (240×240). The data is copied. */
void ui_set_cover_jpeg(const uint8_t * data, size_t len);

/** A hint instead of playback, e.g. "Spotify ist nicht geöffnet"; title = NULL hides it. */
void ui_set_status_message(const char * title, const char * text);

/** Short message below title and artists. */
void ui_toast(const char * text);

/** One card of the playlists page. */
typedef struct {
    const char * name;
    const char * sub;       /* e.g. owner of the playlist */
    const char * uri;       /* spotify:playlist:… or "liked" */
    const char * image_id;  /* 16 chars, the picture comes separately via ui_put_image(); empty = no picture */
    bool liked_songs;       /* Liked Songs: its own tile instead of a picture */
} ui_library_item_t;

/** Playlists from the PC app; spotify_login = false shows the hint to connect. */
void ui_set_library(const ui_library_item_t * items, size_t count, bool spotify_login);

/** Album or single on the artist page. */
typedef struct {
    const char * name;
    const char * type;      /* „Album“, „Single“, „Compilation“ */
    const char * year;
    const char * uri;
    const char * image_id;
} ui_album_t;

/** The PC app's answer to an artist request. */
typedef struct {
    const char * id;
    const char * name;
    const char * image_id;
    int8_t following;       /* -1 unknown (permission missing), 0 no, 1 yes */
    const ui_album_t * albums;
    size_t album_count;
} ui_artist_t;

void ui_set_artist(const ui_artist_t * artist);

/** One key of the key page; only the PC app knows its action. */
typedef struct {
    const char * label;
    const char * icon;      /* Lucide name, e.g. "keyboard", "calculator" */
    const char * icon_id;   /* 16 chars: emoji or own picture arrives via ui_put_key_icon(); empty = Lucide */
    lv_color_t color;       /* colour of the Lucide icon and of the pressed key */
    lv_color_t text_color;
    bool icon_fill;         /* picture fills the whole key, label on top at the bottom */
    bool empty;            /* empty slot: stays blank, doesn't react */
} ui_key_t;

/** Key page layout from the PC app. More than 8 keys: vertical = further rows below,
 *  otherwise further pages (8 each) to the right. */
void ui_set_keys(const char * page_name, bool vertical, const ui_key_t * keys, size_t count);

/** Icon picture of a key (RGB565A8: w·h·2 bytes colour, then w·h bytes alpha). The data is copied. */
void ui_put_key_icon(const char * id, uint16_t w, uint16_t h, const uint8_t * data, size_t len);

/** Picture (JPEG) for cards; the id is in the lists, the picture comes separately. The data is copied. */
void ui_put_image(const char * id, const uint8_t * jpeg, size_t len);

/** One channel of the audio page: master, microphone or an app. */
typedef struct {
    const char * id;        /* "out", "mic" or the app id (at most 20 chars) */
    const char * name;
    const char * sub;       /* sub line (device name); empty = none */
    const char * icon_id;   /* program icon, arrives via ui_put_audio_icon(); empty = first letter */
    lv_color_t color;       /* tint of the icon tile */
    int8_t volume;          /* 0–100 */
    bool muted;
    uint8_t level;          /* current level, 0–100 */
} ui_audio_channel_t;

typedef struct {
    const char * id;
    const char * name;
    bool active;
} ui_audio_output_t;

typedef struct {
    bool list_layout;                   /* false = mixer, true = list */
    const ui_audio_channel_t * out;     /* NULL = no output device */
    const ui_audio_channel_t * mic;     /* NULL = no microphone */
    const ui_audio_channel_t * apps;
    size_t app_count;
    const ui_audio_output_t * outputs;  /* for choosing the output device */
    size_t output_count;
} ui_audio_t;

/** Mixer state; only arrives while the audio page is open. */
void ui_set_audio(const ui_audio_t * audio);

/** Program icon for the audio page (RGB565A8 like the keys). The data is copied. */
void ui_put_audio_icon(const char * id, uint16_t w, uint16_t h, const uint8_t * data, size_t len);

/** Which pages sit at the top as tabs (set in the PC app). At least one stays visible. */
void ui_set_pages(bool music, bool playlists, bool keys, bool audio);

/** Where commands go (JSON like {"cmd":"next"}); the platform sends them to the PC app. */
void ui_set_command_handler(void (*handler)(const char * json));
