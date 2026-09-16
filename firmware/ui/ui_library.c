/**
 * Playlists: waagerecht wischbare Karten mit den Playlists aus Spotify.
 * Lieblingssongs zuerst (lila Verlauf mit Herz, wie in Spotify); antippen spielt die Playlist.
 * Braucht die Verbindung zur PC-App und dort den Spotify-Login.
 */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define CARD_W            264
#define RAIL_Y            92
#define RAIL_H            (SCREEN_H - RAIL_Y)
#define MAX_CARDS         24
#define RELOAD_AFTER_MS   60000

typedef struct {
    char name[96];
    char sub[96];
    char uri[96];
    char image_id[17];
    bool liked_songs;
    lv_obj_t * button;
    lv_obj_t * art;
    lv_obj_t * image;
    lv_obj_t * placeholder;
} card_t;

static struct {
    lv_obj_t * rail;
    lv_obj_t * message;
    lv_obj_t * message_title;
    lv_obj_t * message_text;
    card_t cards[MAX_CARDS];
    size_t count;
    int focus;          /* vom Knauf gewählte Karte */
    bool focus_visible; /* Rahmen erst zeigen, wenn der Knauf benutzt wird */
    bool loaded;
    bool requested;
    uint32_t requested_tick;
} lib;

static void show_message(const char * title, const char * text)
{
    lv_label_set_text(lib.message_title, title);
    lv_label_set_text(lib.message_text, text);
    ui_set_hidden(lib.message, false);
    ui_set_hidden(lib.rail, true);
}

static void show_cards(void)
{
    ui_set_hidden(lib.message, true);
    ui_set_hidden(lib.rail, false);
}

void ui_library_build(lv_obj_t * view)
{
    lv_obj_set_style_bg_color(view, COL_INK, 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);

    /* Kartenreihe: LVGLs eigenes Scrollen, rastet am Kartenanfang ein */
    lib.rail = ui_plain_obj(view);
    lv_obj_set_size(lib.rail, SCREEN_W, RAIL_H);
    lv_obj_set_pos(lib.rail, 0, RAIL_Y);
    lv_obj_add_flag(lib.rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(lib.rail, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(lib.rail, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(lib.rail, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(lib.rail, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_left(lib.rail, 32, 0);
    lv_obj_set_style_pad_right(lib.rail, 32, 0);
    lv_obj_set_style_pad_top(lib.rail, 10, 0); /* Platz für den Knauf-Rahmen über der Karte */
    lv_obj_set_style_pad_column(lib.rail, 24, 0);

    /* Hinweis statt Karten (keine Verbindung, lädt, kein Login) */
    lib.message = ui_plain_obj(view);
    lv_obj_set_size(lib.message, SCREEN_W, SCREEN_H - TOPBAR_H);
    lv_obj_set_pos(lib.message, 0, TOPBAR_H);
    lv_obj_set_style_pad_hor(lib.message, 96, 0);
    lv_obj_set_flex_flow(lib.message, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lib.message, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(lib.message, 12, 0);
    ui_text_label(lib.message, &mi_64, COL_GREEN, ICON_LIBRARY);
    lib.message_title = ui_text_label(lib.message, &fig_b_40, COL_TEXT, "");
    lib.message_text = ui_text_label(lib.message, &fig_md_22, COL_MUTED, "");
    lv_obj_set_width(lib.message_text, 520);
    lv_label_set_long_mode(lib.message_text, LV_LABEL_LONG_MODE_WRAP);

    show_message("Keine Verbindung zum PC", "Die Playlists erscheinen, sobald das Gerät mit der PC-App verbunden ist.");
}

static void select_card(const card_t * card)
{
    char value[128];
    snprintf(value, sizeof(value), "\"%s\"", card->uri);
    ui_send_command("play_context", value);
    ui_show_view(VIEW_NOW);
}

static void on_card(lv_event_t * e)
{
    select_card(lv_event_get_user_data(e));
}

static void update_focus(void)
{
    for(size_t i = 0; i < lib.count; i++) {
        bool on = lib.focus_visible && (int)i == lib.focus;
        lv_obj_set_style_outline_color(lib.cards[i].art, COL_TEXT, 0);
        lv_obj_set_style_outline_pad(lib.cards[i].art, 4, 0);
        lv_obj_set_style_outline_width(lib.cards[i].art, on ? 4 : 0, 0);
    }
}

void ui_library_knob_rotate(int32_t steps)
{
    if(lib.count == 0 || lv_obj_has_flag(lib.rail, LV_OBJ_FLAG_HIDDEN)) return;
    /* erster Dreh zeigt nur, wo man steht; danach springt der Rahmen von Karte zu Karte */
    if(lib.focus_visible) lib.focus = LV_CLAMP(0, lib.focus + steps, (int)lib.count - 1);
    lib.focus_visible = true;
    update_focus();
    lv_obj_scroll_to_view(lib.cards[lib.focus].button, LV_ANIM_ON);
}

void ui_library_knob_press(void)
{
    if(lib.count == 0 || !lib.focus_visible || lv_obj_has_flag(lib.rail, LV_OBJ_FLAG_HIDDEN)) return;
    select_card(&lib.cards[lib.focus]);
}

static void set_card_image(card_t * card)
{
    if(card->image == NULL) return;
    const lv_image_dsc_t * dsc = ui_image_get(card->image_id);
    lv_image_set_src(card->image, dsc);
    ui_set_hidden(card->image, dsc == NULL);
    ui_set_hidden(card->placeholder, dsc != NULL);
}

static void build_card(card_t * card)
{
    lv_obj_t * btn = lv_button_create(lib.rail);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, CARD_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(btn, 4, 0);
    lv_obj_set_style_opa(btn, 190, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, on_card, LV_EVENT_CLICKED, card);
    card->button = btn;

    lv_obj_t * art = ui_plain_obj(btn);
    card->art = art;
    lv_obj_remove_flag(art, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(art, CARD_W, CARD_W);
    /* Ecken rundet das Bild selbst ab; clip_corner kostet beim Wischen pro Karte eine Zwischenebene */
    lv_obj_set_style_radius(art, 10, 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_bottom(art, 10, 0);

    if(card->liked_songs) {
        /* wie in Spotify: Verlauf Lila → Hellblau mit weißem Herz */
        lv_obj_set_style_bg_color(art, lv_color_hex(0x450AF5), 0);
        lv_obj_set_style_bg_grad_color(art, lv_color_hex(0x8E8EE5), 0);
        lv_obj_set_style_bg_grad_dir(art, LV_GRAD_DIR_VER, 0);
        lv_obj_center(ui_text_label(art, &mi_64, COL_TEXT, ICON_HEART));
        card->image = NULL;
        card->placeholder = NULL;
    }
    else {
        lv_obj_set_style_bg_color(art, COL_SURFACE, 0);
        card->placeholder = ui_text_label(art, &mi_64, COL_MUTED, ICON_MUSIC);
        lv_obj_center(card->placeholder);
        card->image = lv_image_create(art);
        lv_obj_set_size(card->image, CARD_W, CARD_W);
        lv_obj_set_style_radius(card->image, 10, 0);
        set_card_image(card);
    }

    /* feste Höhe = eine Zeile; nur dann kürzt LVGL mit „…“ statt umzubrechen */
    lv_obj_t * name = ui_text_label(btn, &fig_sb_28, COL_TEXT, card->name);
    lv_obj_set_size(name, CARD_W, lv_font_get_line_height(&fig_sb_28) + 4);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t * sub = ui_text_label(btn, &fig_md_22, COL_MUTED, card->sub);
    lv_obj_set_size(sub, CARD_W, lv_font_get_line_height(&fig_md_22) + 4);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
}

void ui_set_library(const ui_library_item_t * items, size_t count, bool spotify_login)
{
    lib.loaded = true;
    lv_obj_clean(lib.rail);
    lib.count = 0;
    lib.focus = 0;
    lib.focus_visible = false;

    if(!spotify_login) {
        show_message("Mit Spotify verbinden", "Für deine Playlists die PC-App einmal mit deinem Spotify-Konto verbinden.");
        return;
    }
    if(count == 0) {
        show_message("Keine Playlists gefunden", "In deiner Spotify-Bibliothek sind keine Playlists, oder Spotify hat nicht geantwortet.");
        return;
    }

    for(size_t i = 0; i < count && lib.count < MAX_CARDS; i++) {
        card_t * card = &lib.cards[lib.count++];
        snprintf(card->name, sizeof(card->name), "%s", items[i].name ? items[i].name : "");
        snprintf(card->sub, sizeof(card->sub), "%s", items[i].sub ? items[i].sub : "");
        snprintf(card->uri, sizeof(card->uri), "%s", items[i].uri ? items[i].uri : "");
        snprintf(card->image_id, sizeof(card->image_id), "%s", items[i].image_id ? items[i].image_id : "");
        card->liked_songs = items[i].liked_songs;
        build_card(card);
    }
    show_cards();
    lv_obj_scroll_to_x(lib.rail, 0, LV_ANIM_OFF);
}

void ui_library_on_show(void)
{
    if(!ui_is_connected()) {
        show_message("Keine Verbindung zum PC", "Die Playlists erscheinen, sobald das Gerät mit der PC-App verbunden ist.");
        return;
    }
    if(lib.requested && lib.loaded && lv_tick_elaps(lib.requested_tick) < RELOAD_AFTER_MS) return;
    if(!lib.loaded) show_message("Playlists werden geladen …", "");
    lib.requested = true;
    lib.requested_tick = lv_tick_get();
    ui_send_command("library", NULL);
}

void ui_library_on_disconnect(void)
{
    lib.loaded = false;
    lib.requested = false;
    lv_obj_clean(lib.rail);
    lib.count = 0;
    show_message("Keine Verbindung zum PC", "Die Playlists erscheinen, sobald das Gerät mit der PC-App verbunden ist.");
}

void ui_library_image_arrived(const char * id)
{
    for(size_t i = 0; i < lib.count; i++) {
        if(lib.cards[i].image != NULL && strcmp(lib.cards[i].image_id, id) == 0) set_card_image(&lib.cards[i]);
    }
}

void ui_library_image_dropped(const char * id)
{
    for(size_t i = 0; i < lib.count; i++) {
        card_t * card = &lib.cards[i];
        if(card->image != NULL && strcmp(card->image_id, id) == 0) {
            lv_image_set_src(card->image, NULL);
            ui_set_hidden(card->image, true);
            ui_set_hidden(card->placeholder, false);
        }
    }
}
