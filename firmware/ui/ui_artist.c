/**
 * Künstlerseite: rundes Foto, Name, Abspielen und Folgen, darunter Alben und Singles zum Wischen.
 * Geöffnet über die Interpreten-Zeile der Wiedergabe. Daten und Bilder liefert die PC-App
 * (Spotify-API mit Login); Top-Songs und Follower gibt es seit Februar 2026 nicht mehr.
 */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define PHOTO_SIZE   120
#define ALBUM_W      160
#define MAX_ALBUMS   20
#define INFO_X       232
#define RAIL_Y       250

typedef struct {
    char name[96];
    char sub[48];
    char uri[96];
    char image_id[17];
    lv_obj_t * button;
    lv_obj_t * art;
    lv_obj_t * image;
    lv_obj_t * placeholder;
} album_card_t;

static struct {
    lv_obj_t * photo;
    lv_obj_t * photo_image;
    lv_obj_t * photo_icon;
    lv_obj_t * name;
    lv_obj_t * follow;
    lv_obj_t * follow_label;
    lv_obj_t * rail;
    lv_obj_t * message;

    char id[40];
    char image_id[17];
    int8_t following; /* -1 unbekannt (Berechtigung fehlt), 0 nein, 1 ja */
    album_card_t albums[MAX_ALBUMS];
    size_t count;
    int focus;
    bool focus_visible;
} ar;

/* ---------- Bausteine ---------- */

static lv_obj_t * pill_button(lv_obj_t * parent, bool primary, lv_event_cb_t cb)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_height(btn, 52);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(btn, 22, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, primary ? COL_GREEN : COL_INK, 0);
    lv_obj_set_style_bg_opa(btn, primary ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if(!primary) {
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x727272), 0);
    }
    lv_obj_set_style_opa(btn, 200, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, 8, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static void set_album_image(album_card_t * card)
{
    const lv_image_dsc_t * dsc = ui_image_get(card->image_id);
    lv_image_set_src(card->image, dsc);
    ui_set_hidden(card->image, dsc == NULL);
    ui_set_hidden(card->placeholder, dsc != NULL);
}

static void set_photo(void)
{
    const lv_image_dsc_t * dsc = ui_image_get(ar.image_id);
    lv_image_set_src(ar.photo_image, dsc);
    ui_set_hidden(ar.photo_image, dsc == NULL);
    ui_set_hidden(ar.photo_icon, dsc != NULL);
}

static void update_follow(void)
{
    lv_label_set_text(ar.follow_label, ar.following > 0 ? "Folge ich" : "Folgen");
    lv_obj_set_style_border_color(ar.follow, ar.following > 0 ? COL_TEXT : lv_color_hex(0x727272), 0);
}

static void show_message(const char * text)
{
    lv_label_set_text(ar.message, text);
    ui_set_hidden(ar.message, false);
}

/* ---------- Bedienung ---------- */

static void play_uri(const char * uri)
{
    char value[128];
    snprintf(value, sizeof(value), "\"%s\"", uri);
    ui_send_command("play_context", value);
    ui_show_view(VIEW_NOW);
}

static void on_back(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_show_view(VIEW_NOW);
}

static void on_play(lv_event_t * e)
{
    LV_UNUSED(e);
    char uri[64];
    snprintf(uri, sizeof(uri), "spotify:artist:%s", ar.id);
    play_uri(uri);
}

static void on_follow(lv_event_t * e)
{
    LV_UNUSED(e);
    if(ar.following < 0) {
        ui_toast("Zum Folgen die PC-App neu mit Spotify verbinden");
        return;
    }
    ar.following = ar.following ? 0 : 1;
    update_follow();
    char value[80];
    snprintf(value, sizeof(value), "{\"id\":\"%s\",\"on\":%s}", ar.id, ar.following ? "true" : "false");
    ui_send_command("follow", value);
}

static void on_album(lv_event_t * e)
{
    album_card_t * card = lv_event_get_user_data(e);
    play_uri(card->uri);
}

static void update_focus(void)
{
    for(size_t i = 0; i < ar.count; i++) {
        bool on = ar.focus_visible && (int)i == ar.focus;
        lv_obj_set_style_outline_color(ar.albums[i].art, COL_TEXT, 0);
        lv_obj_set_style_outline_pad(ar.albums[i].art, 4, 0);
        lv_obj_set_style_outline_width(ar.albums[i].art, on ? 4 : 0, 0);
    }
}

void ui_artist_knob_rotate(int32_t steps)
{
    if(ar.count == 0) return;
    if(ar.focus_visible) ar.focus = LV_CLAMP(0, ar.focus + steps, (int)ar.count - 1);
    ar.focus_visible = true;
    update_focus();
    lv_obj_scroll_to_view(ar.albums[ar.focus].button, LV_ANIM_ON);
}

void ui_artist_knob_press(void)
{
    if(ar.count == 0 || !ar.focus_visible) return;
    play_uri(ar.albums[ar.focus].uri);
}

/* ---------- Aufbau ---------- */

void ui_artist_build(lv_obj_t * view)
{
    lv_obj_set_style_bg_color(view, COL_INK, 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);

    lv_obj_t * back = lv_button_create(view);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, 56, 56);
    lv_obj_set_pos(back, 16, 112);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back, COL_TEXT, 0);
    lv_obj_set_style_bg_opa(back, 20, 0);
    lv_obj_set_style_bg_opa(back, 46, LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_center(ui_text_label(back, &mi_28, COL_TEXT, ICON_BACK));

    ar.photo = ui_plain_obj(view);
    lv_obj_set_size(ar.photo, PHOTO_SIZE, PHOTO_SIZE);
    lv_obj_set_pos(ar.photo, 88, 80);
    lv_obj_set_style_radius(ar.photo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(ar.photo, true, 0);
    lv_obj_set_style_bg_color(ar.photo, COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(ar.photo, LV_OPA_COVER, 0);
    ar.photo_icon = ui_text_label(ar.photo, &mi_64, COL_MUTED, ICON_MUSIC);
    lv_obj_center(ar.photo_icon);
    ar.photo_image = lv_image_create(ar.photo);
    lv_obj_set_size(ar.photo_image, PHOTO_SIZE, PHOTO_SIZE);
    lv_image_set_inner_align(ar.photo_image, LV_IMAGE_ALIGN_CENTER);
    ui_set_hidden(ar.photo_image, true);

    ar.name = ui_text_label(view, &fig_xb_42, COL_TEXT, "");
    lv_obj_set_pos(ar.name, INFO_X, 80);
    lv_obj_set_size(ar.name, SCREEN_W - INFO_X - 32, lv_font_get_line_height(&fig_xb_42) + 6);
    lv_label_set_long_mode(ar.name, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t * actions = ui_plain_obj(view);
    lv_obj_set_pos(actions, INFO_X, 144);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, 52);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 12, 0);

    lv_obj_t * play = pill_button(actions, true, on_play);
    ui_text_label(play, &mi_28, COL_INK, ICON_PLAY);
    ui_text_label(play, &fig_sb_20, COL_INK, "Abspielen");

    ar.follow = pill_button(actions, false, on_follow);
    ar.follow_label = ui_text_label(ar.follow, &fig_sb_20, COL_TEXT, "Folgen");

    lv_obj_t * heading = ui_text_label(view, &fig_sb_24, COL_TEXT, "Alben & Singles");
    lv_obj_set_pos(heading, 32, 214);

    ar.rail = ui_plain_obj(view);
    lv_obj_set_size(ar.rail, SCREEN_W, SCREEN_H - RAIL_Y);
    lv_obj_set_pos(ar.rail, 0, RAIL_Y);
    lv_obj_add_flag(ar.rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ar.rail, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(ar.rail, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(ar.rail, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(ar.rail, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_left(ar.rail, 32, 0);
    lv_obj_set_style_pad_right(ar.rail, 32, 0);
    lv_obj_set_style_pad_top(ar.rail, 10, 0);
    lv_obj_set_style_pad_column(ar.rail, 18, 0);

    ar.message = ui_text_label(view, &fig_md_22, COL_MUTED, "");
    lv_obj_set_pos(ar.message, 32, RAIL_Y + 12);
}

static void build_album(album_card_t * card)
{
    lv_obj_t * btn = lv_button_create(ar.rail);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, ALBUM_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_opa(btn, 190, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, on_album, LV_EVENT_CLICKED, card);
    card->button = btn;

    card->art = ui_plain_obj(btn);
    lv_obj_remove_flag(card->art, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(card->art, ALBUM_W, ALBUM_W);
    lv_obj_set_style_radius(card->art, 8, 0);
    lv_obj_set_style_bg_color(card->art, COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(card->art, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_bottom(card->art, 8, 0);
    card->placeholder = ui_text_label(card->art, &mi_44, COL_MUTED, ICON_MUSIC);
    lv_obj_center(card->placeholder);
    card->image = lv_image_create(card->art);
    lv_obj_set_size(card->image, ALBUM_W, ALBUM_W);
    lv_obj_set_style_radius(card->image, 8, 0);
    set_album_image(card);

    lv_obj_t * name = ui_text_label(btn, &fig_sb_20, COL_TEXT, card->name);
    lv_obj_set_size(name, ALBUM_W, lv_font_get_line_height(&fig_sb_20) + 4);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t * sub = ui_text_label(btn, &fig_md_17, COL_MUTED, card->sub);
    lv_obj_set_size(sub, ALBUM_W, lv_font_get_line_height(&fig_md_17) + 4);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
}

/* ---------- Daten ---------- */

void ui_artist_open(const char * id, const char * name)
{
    snprintf(ar.id, sizeof(ar.id), "%s", id);
    ar.image_id[0] = '\0';
    ar.following = -1;
    ar.focus = 0;
    ar.focus_visible = false;
    lv_obj_clean(ar.rail);
    ar.count = 0;

    lv_label_set_text(ar.name, name);
    set_photo();
    update_follow();
    show_message("Künstler wird geladen …");
    ui_show_view(VIEW_ARTIST);

    char value[48];
    snprintf(value, sizeof(value), "\"%s\"", id);
    ui_send_command("artist", value);
}

void ui_set_artist(const ui_artist_t * artist)
{
    if(artist == NULL || strcmp(artist->id ? artist->id : "", ar.id) != 0) return; /* Antwort auf eine ältere Anfrage */

    if(artist->name && artist->name[0]) lv_label_set_text(ar.name, artist->name);
    snprintf(ar.image_id, sizeof(ar.image_id), "%s", artist->image_id ? artist->image_id : "");
    set_photo();
    ar.following = artist->following;
    update_follow();

    lv_obj_clean(ar.rail);
    ar.count = 0;
    for(size_t i = 0; i < artist->album_count && ar.count < MAX_ALBUMS; i++) {
        const ui_album_t * al = &artist->albums[i];
        album_card_t * card = &ar.albums[ar.count++];
        snprintf(card->name, sizeof(card->name), "%s", al->name ? al->name : "");
        if(al->year && al->year[0]) snprintf(card->sub, sizeof(card->sub), "%s · %s", al->type ? al->type : "Album", al->year);
        else snprintf(card->sub, sizeof(card->sub), "%s", al->type ? al->type : "Album");
        snprintf(card->uri, sizeof(card->uri), "%s", al->uri ? al->uri : "");
        snprintf(card->image_id, sizeof(card->image_id), "%s", al->image_id ? al->image_id : "");
        build_album(card);
    }
    if(ar.count == 0) show_message("Keine Alben gefunden");
    else ui_set_hidden(ar.message, true);
}

void ui_artist_image_arrived(const char * id)
{
    if(strcmp(ar.image_id, id) == 0) set_photo();
    for(size_t i = 0; i < ar.count; i++) {
        if(strcmp(ar.albums[i].image_id, id) == 0) set_album_image(&ar.albums[i]);
    }
}

void ui_artist_image_dropped(const char * id)
{
    if(strcmp(ar.image_id, id) == 0) {
        lv_image_set_src(ar.photo_image, NULL);
        ui_set_hidden(ar.photo_image, true);
        ui_set_hidden(ar.photo_icon, false);
    }
    for(size_t i = 0; i < ar.count; i++) {
        album_card_t * card = &ar.albums[i];
        if(strcmp(card->image_id, id) == 0) {
            lv_image_set_src(card->image, NULL);
            ui_set_hidden(card->image, true);
            ui_set_hidden(card->placeholder, false);
        }
    }
}
