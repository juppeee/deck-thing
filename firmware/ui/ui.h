/**
 * Geräte-Oberfläche (LVGL 9). Läuft unverändert im Windows-Simulator und auf dem ESP32-S3.
 *
 * Die Plattform ruft ui_init() einmal auf und meldet danach Knauf, Verbindung und neue Daten.
 * Solange keine Daten vom PC kommen, zeigt die Oberfläche Beispieltitel.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

/** Ein Interpret mit Spotify-ID (für die Künstlerseite). */
typedef struct {
    const char * id;
    const char * name;
} ui_artist_ref_t;

/** Zustand der Wiedergabe, wie ihn die PC-App schickt. */
typedef struct {
    const ui_artist_ref_t * artist_refs; /* nur mit Spotify-Login; sonst NULL */
    size_t artist_ref_count;
    const char * title;     /* Songtitel */
    const char * artists;   /* „Lena Kessler, Mira Holt, Jonas Feld“ */
    const char * context;   /* Herkunft, z. B. „Lieblingssongs“; leer = ausblenden */
    float pos_s;            /* Position in Sekunden */
    float dur_s;            /* Länge in Sekunden */
    bool playing;
    uint8_t shuffle;        /* 0 aus, 1 Zufall, 2 Smart Shuffle */
    uint8_t repeat;         /* 0 aus, 1 Playlist, 2 Titel */
    bool spotify_login;     /* ohne Login gibt es keinen Like-Button */
    int8_t liked;           /* -1 gerade unbekannt (z. B. kurz nach Songwechsel), 0 nein, 1 ja */
    int8_t volume;          /* 0–100, -1 unbekannt */
    bool muted;
    lv_color_t color;       /* Grundfarbe des Covers für den Hintergrundverlauf */
} ui_playback_t;

/** Oberfläche aufbauen. knob darf NULL sein (kein Knauf angeschlossen). */
void ui_init(lv_display_t * display, lv_indev_t * knob);

/** Knauf vom echten Gerät: Rastschritte (+ = lauter) und Drücken. */
void ui_knob_rotate(int32_t steps);
void ui_knob_press(void);

/** Knauf an- oder abgesteckt (blendet die Lautstärketaste ein/aus). */
void ui_set_knob_present(bool present);

/** Verbindung zur PC-App hergestellt oder verloren (verloren = zurück zu den Beispieltiteln). */
void ui_set_connected(bool connected);

/** Neuer Wiedergabestand von der PC-App. */
void ui_set_playback(const ui_playback_t * pb);

/** Cover als JPEG (240×240). Die Daten werden kopiert. */
void ui_set_cover_jpeg(const uint8_t * data, size_t len);

/** Hinweis statt Wiedergabe, z. B. „Spotify ist nicht geöffnet“; title = NULL blendet ihn aus. */
void ui_set_status_message(const char * title, const char * text);

/** Kurze Meldung unter Titel und Interpreten. */
void ui_toast(const char * text);

/** Eine Karte der Bibliothek. */
typedef struct {
    const char * name;
    const char * sub;       /* z. B. Besitzer der Playlist */
    const char * uri;       /* spotify:playlist:… oder "liked" */
    const char * image_id;  /* 16 Zeichen, Bild kommt getrennt über ui_put_image(); leer = kein Bild */
    bool liked_songs;       /* Lieblingssongs: eigene Kachel statt Bild */
} ui_library_item_t;

/** Playlists von der PC-App; spotify_login = false zeigt den Hinweis zum Verbinden. */
void ui_set_library(const ui_library_item_t * items, size_t count, bool spotify_login);

/** Album oder Single auf der Künstlerseite. */
typedef struct {
    const char * name;
    const char * type;      /* „Album“, „Single“, „Compilation“ */
    const char * year;
    const char * uri;
    const char * image_id;
} ui_album_t;

/** Antwort der PC-App auf eine Künstleranfrage. */
typedef struct {
    const char * id;
    const char * name;
    const char * image_id;
    int8_t following;       /* -1 unbekannt (Berechtigung fehlt), 0 nein, 1 ja */
    const ui_album_t * albums;
    size_t album_count;
} ui_artist_t;

void ui_set_artist(const ui_artist_t * artist);

/** Eine Taste der Tastenseite; die Aktion kennt nur die PC-App. */
typedef struct {
    const char * label;
    const char * icon;      /* Lucide-Name, z. B. "keyboard", "calculator" */
    const char * icon_id;   /* 16 Zeichen: Emoji oder eigenes Bild kommt über ui_put_key_icon(); leer = Lucide */
    lv_color_t color;       /* Farbe des Lucide-Symbols und der gedrückten Taste */
    lv_color_t text_color;
    bool icon_fill;         /* Bild füllt die ganze Taste, Beschriftung unten darüber */
    bool empty;            /* leeres Feld: bleibt frei, reagiert nicht */
} ui_key_t;

/** Belegung der Tastenseite von der PC-App. Mehr als 8 Tasten: vertical = weitere Reihen nach unten,
 *  sonst weitere Seiten (je 8) nach rechts. */
void ui_set_keys(const char * page_name, bool vertical, const ui_key_t * keys, size_t count);

/** Symbolbild einer Taste (RGB565A8: w·h·2 Byte Farbe, dann w·h Byte Alpha). Daten werden kopiert. */
void ui_put_key_icon(const char * id, uint16_t w, uint16_t h, const uint8_t * data, size_t len);

/** Bild (JPEG) für Karten; die Kennung steht in den Listen, das Bild kommt getrennt. Daten werden kopiert. */
void ui_put_image(const char * id, const uint8_t * jpeg, size_t len);

/** Ein Kanal der Audio-Seite: Gesamt, Mikrofon oder eine App. */
typedef struct {
    const char * id;        /* "out", "mic" oder Kennung der App (höchstens 20 Zeichen) */
    const char * name;
    const char * sub;       /* Unterzeile (Gerätename); leer = keine */
    const char * icon_id;   /* Programmsymbol, kommt über ui_put_audio_icon(); leer = Anfangsbuchstabe */
    lv_color_t color;       /* Tönung der Symbolkachel */
    int8_t volume;          /* 0–100 */
    bool muted;
    uint8_t level;          /* Pegel gerade eben, 0–100 */
} ui_audio_channel_t;

typedef struct {
    const char * id;
    const char * name;
    bool active;
} ui_audio_output_t;

typedef struct {
    bool list_layout;                   /* false = Mischpult, true = Liste */
    const ui_audio_channel_t * out;     /* NULL = kein Ausgabegerät */
    const ui_audio_channel_t * mic;     /* NULL = kein Mikrofon */
    const ui_audio_channel_t * apps;
    size_t app_count;
    const ui_audio_output_t * outputs;  /* zur Auswahl des Ausgabegeräts */
    size_t output_count;
} ui_audio_t;

/** Stand des Mixers; kommt nur, solange die Audio-Seite offen ist. */
void ui_set_audio(const ui_audio_t * audio);

/** Programmsymbol für die Audio-Seite (RGB565A8 wie bei den Tasten). Daten werden kopiert. */
void ui_put_audio_icon(const char * id, uint16_t w, uint16_t h, const uint8_t * data, size_t len);

/** Welche Seiten oben als Reiter stehen (in der PC-App einstellbar). Mindestens eine bleibt sichtbar. */
void ui_set_pages(bool music, bool playlists, bool keys, bool audio);

/** Wohin Befehle gehen (JSON wie {"cmd":"next"}); die Plattform schickt sie an die PC-App. */
void ui_set_command_handler(void (*handler)(const char * json));
