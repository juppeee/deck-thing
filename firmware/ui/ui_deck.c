/**
 * Key page (Stream Deck mode): freely assignable keys whose actions the PC app runs
 * (key combination, program, script, link, media). The layout comes from the PC app.
 *
 * 4 × 2 keys are visible. Depending on the setting, more follow as whole pages to the right
 * (swipe, snaps page by page) or as rows below (snaps row by row).
 *
 * The icon is either a Lucide glyph from the built-in font or a picture the PC app sends
 * ready-made (emoji or own picture, RGB565 with alpha channel).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"

#define MAX_KEYS  48
#define COLUMNS   4
#define PAGE_KEYS 8
#define GRID_X    32
#define GRID_Y    120
#define KEY_GAP   16
#define KEY_W     ((SCREEN_W - 2 * GRID_X - 3 * KEY_GAP) / 4)
#define KEY_H     150
#define PAGE_W    (COLUMNS * (KEY_W + KEY_GAP)) /* one page to the right */
#define ROW_H     (KEY_H + KEY_GAP)             /* one row down */
#define ICON_SIZE 56
#define DOT_SIZE  8

typedef struct {
    char label[48];
    char icon[24];
    char icon_id[17];       /* empty = Lucide glyph, else a picture from the PC app */
    lv_color_t color;
    lv_color_t text_color;
    bool icon_fill;
    bool empty;
    lv_obj_t * button;
    lv_obj_t * icon_label;  /* Lucide */
    lv_obj_t * image;       /* emoji or own picture */
    lv_obj_t * text;
    lv_image_dsc_t dsc;
    uint8_t * pixels;
} deck_key_t;

static struct {
    lv_obj_t * title;
    lv_obj_t * grid;
    lv_obj_t * dots;
    lv_obj_t * message;
    lv_obj_t * message_title;
    lv_obj_t * message_text;
    deck_key_t keys[MAX_KEYS];
    size_t count;
    bool vertical;
    bool loaded;
    bool requested;
    int focus;
    bool focus_visible;
} dk;

/* icon names from the layout (Lucide) → glyphs in lc_48 */
static const struct {
    const char * name;
    const char * glyph;
} ICONS[] = {
    { "keyboard", "\xEE\x8A\x84" },     { "mic-off", "\xEE\x84\x99" },     { "mic", "\xEE\x84\x98" },
    { "scan", "\xEE\x89\x97" },         { "camera", "\xEE\x81\xA4" },      { "calculator", "\xEE\x86\xBC" },
    { "folder", "\xEE\x83\x97" },       { "globe", "\xEE\x83\xA8" },       { "activity", "\xEE\x80\xB8" },
    { "monitor", "\xEE\x84\x9D" },      { "app-window", "\xEE\x90\xA6" },  { "lock", "\xEE\x84\x8B" },
    { "power", "\xEE\x85\x80" },        { "settings", "\xEE\x85\x94" },    { "zap", "\xEE\x86\xB4" },
    { "command", "\xEE\x82\x9A" },      { "terminal", "\xEE\x86\x81" },    { "music", "\xEE\x84\xA2" },
    { "volume-2", "\xEE\x86\xAB" },     { "volume-x", "\xEE\x86\xAC" },    { "play", "\xEE\x84\xBC" },
    { "skip-forward", "\xEE\x85\xA0" }, { "plus", "\xEE\x84\xBD" },        { "message-circle", "\xEE\x84\x96" },
    { "gamepad-2", "\xEE\x83\x9F" },    { "video", "\xEE\x86\xA5" },       { "house", "\xEE\x83\xB5" },
    { "star", "\xEE\x85\xB6" },
};

static const char * glyph_for(const char * name)
{
    for(size_t i = 0; i < sizeof(ICONS) / sizeof(ICONS[0]); i++) {
        if(strcmp(ICONS[i].name, name) == 0) return ICONS[i].glyph;
    }
    return ICONS[0].glyph; /* unknown: keyboard */
}

static void show_message(const char * title, const char * text)
{
    lv_label_set_text(dk.message_title, title);
    lv_label_set_text(dk.message_text, text);
    ui_set_hidden(dk.message, false);
    ui_set_hidden(dk.grid, true);
    ui_set_hidden(dk.dots, true);
    ui_set_hidden(dk.title, true);
}

/* free picture buffers only once no object points at them (after lv_obj_clean) */
static void free_pixels(deck_key_t * key)
{
    if(key->pixels == NULL) return;
    lv_image_cache_drop(&key->dsc);
    free(key->pixels);
    key->pixels = NULL;
}

static void clear_keys(void)
{
    lv_obj_clean(dk.grid);
    lv_obj_clean(dk.dots);
    for(size_t i = 0; i < dk.count; i++) free_pixels(&dk.keys[i]);
    dk.count = 0;
    dk.focus = 0;
    dk.focus_visible = false;
}

/* ---------- Paging ---------- */

static int32_t scroll_pitch(void)
{
    return dk.vertical ? ROW_H : PAGE_W;
}

static int32_t scroll_pos(void)
{
    return dk.vertical ? lv_obj_get_scroll_y(dk.grid) : lv_obj_get_scroll_x(dk.grid);
}

/* number of snap stops: pages to the right, or top row 0 … rows−2 downwards */
static int stop_count(void)
{
    if(dk.vertical) {
        int rows = (int)((dk.count + COLUMNS - 1) / COLUMNS);
        return rows > 2 ? rows - 1 : 1;
    }
    return (int)((dk.count + PAGE_KEYS - 1) / PAGE_KEYS);
}

static int current_stop(void)
{
    int stop = (int)((scroll_pos() + scroll_pitch() / 2) / scroll_pitch());
    return LV_CLAMP(0, stop, stop_count() - 1);
}

static void scroll_to_stop(int stop, lv_anim_enable_t anim)
{
    int32_t target = LV_CLAMP(0, stop, stop_count() - 1) * scroll_pitch();
    if(target == scroll_pos()) return;
    if(dk.vertical) lv_obj_scroll_to_y(dk.grid, target, anim);
    else lv_obj_scroll_to_x(dk.grid, target, anim);
}

static void update_dots(void)
{
    int active = current_stop();
    uint32_t n = lv_obj_get_child_count(dk.dots);
    for(uint32_t i = 0; i < n; i++) {
        lv_obj_set_style_bg_color(lv_obj_get_child(dk.dots, (int32_t)i), (int)i == active ? COL_TEXT : COL_TRACK, 0);
    }
}

static void build_dots(void)
{
    int stops = stop_count();
    lv_obj_set_flex_flow(dk.dots, dk.vertical ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    if(dk.vertical) lv_obj_align(dk.dots, LV_ALIGN_TOP_RIGHT, -8, GRID_Y + KEY_H - DOT_SIZE * stops);
    else lv_obj_align(dk.dots, LV_ALIGN_BOTTOM_MID, 0, -14);
    for(int i = 0; stops > 1 && i < stops; i++) {
        lv_obj_t * dot = ui_plain_obj(dk.dots);
        lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    }
    ui_set_hidden(dk.dots, stops <= 1);
    update_dots();
}

/* after a swipe, snap to the nearest whole page or row */
static void on_scroll_end(lv_event_t * e)
{
    LV_UNUSED(e);
    if(dk.count == 0) return;
    int stop = current_stop();
    if(stop * scroll_pitch() != scroll_pos()) scroll_to_stop(stop, LV_ANIM_ON);
    else update_dots();
}

static void make_visible(int index)
{
    if(dk.vertical) {
        int row = index / COLUMNS;
        int top = current_stop();
        if(row < top) top = row;
        else if(row > top + 1) top = row - 1;
        scroll_to_stop(top, LV_ANIM_ON);
    }
    else {
        scroll_to_stop(index / PAGE_KEYS, LV_ANIM_ON);
    }
}

/* ---------- Pressing ---------- */

static void set_key_lit(deck_key_t * key, bool lit)
{
    if(key->empty) return;
    /* pressed: the key lights up in its colour, icon and text turn dark (pictures stay as they are) */
    lv_obj_set_style_bg_color(key->button, lit ? key->color : COL_SURFACE, 0);
    if(key->icon_label) lv_obj_set_style_text_color(key->icon_label, lit ? COL_INK : key->color, 0);
    if(key->icon_fill && key->image) {
        /* whole-key picture: half transparent, the key colour shines through; the text stays light */
        lv_obj_set_style_image_opa(key->image, lit ? LV_OPA_50 : LV_OPA_COVER, 0);
        return;
    }
    lv_obj_set_style_text_color(key->text, lit ? COL_INK : key->text_color, 0);
}

static void trigger(size_t index)
{
    char value[32];
    snprintf(value, sizeof(value), "{\"page\":0,\"index\":%u}", (unsigned)index);
    ui_send_command("key_press", value);
}

static void on_key_event(lv_event_t * e)
{
    deck_key_t * key = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_PRESSED) set_key_lit(key, true);
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) set_key_lit(key, false);
    else if(code == LV_EVENT_CLICKED) trigger((size_t)(key - dk.keys));
}

static void unlight_cb(lv_timer_t * t)
{
    set_key_lit(lv_timer_get_user_data(t), false);
}

/* ---------- Knob ---------- */

static void update_focus(void)
{
    for(size_t i = 0; i < dk.count; i++) {
        bool on = dk.focus_visible && (int)i == dk.focus;
        lv_obj_set_style_outline_color(dk.keys[i].button, COL_TEXT, 0);
        lv_obj_set_style_outline_pad(dk.keys[i].button, 4, 0);
        lv_obj_set_style_outline_width(dk.keys[i].button, on ? 4 : 0, 0);
    }
}

static int first_active_from(int start)
{
    for(int n = 0; n < (int)dk.count; n++) {
        int i = (start + n) % (int)dk.count;
        if(!dk.keys[i].empty) return i;
    }
    return -1;
}

void ui_deck_knob_rotate(int32_t steps)
{
    if(dk.count == 0 || lv_obj_has_flag(dk.grid, LV_OBJ_FLAG_HIDDEN)) return;
    if(!dk.focus_visible) {
        /* the first turn only shows the frame, on the first assigned key of the visible page */
        int start = current_stop() * (dk.vertical ? COLUMNS : PAGE_KEYS);
        int first = first_active_from(start);
        if(first < 0) return;
        dk.focus = first;
        dk.focus_visible = true;
    }
    else {
        /* skip empty slots; stop at the edge */
        int dir = steps > 0 ? 1 : -1;
        for(int32_t n = LV_ABS(steps); n > 0; n--) {
            int next = dk.focus + dir;
            while(next >= 0 && next < (int)dk.count && dk.keys[next].empty) next += dir;
            if(next < 0 || next >= (int)dk.count) break;
            dk.focus = next;
        }
    }
    update_focus();
    make_visible(dk.focus);
}

void ui_deck_knob_press(void)
{
    if(dk.count == 0 || !dk.focus_visible || lv_obj_has_flag(dk.grid, LV_OBJ_FLAG_HIDDEN)) return;
    deck_key_t * key = &dk.keys[dk.focus];
    if(key->empty) return;
    set_key_lit(key, true);
    lv_timer_t * t = lv_timer_create(unlight_cb, 160, key); /* light up briefly like a tap */
    lv_timer_set_repeat_count(t, 1);
    trigger((size_t)dk.focus);
}

/* ---------- Layout ---------- */

void ui_deck_build(lv_obj_t * view)
{
    lv_obj_set_style_bg_color(view, COL_INK, 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);

    dk.title = ui_text_label(view, &fig_sb_28, COL_TEXT, "");
    lv_obj_set_pos(dk.title, GRID_X, 76);

    /* 8 px padding all round: the knob frame (4 px gap + 4 px width) sticks out over the key
       and would be clipped at the grid edge otherwise – the keys themselves stay in place.
       Exactly 4 × 2 keys are visible; anything beyond that scrolls. */
    dk.grid = ui_plain_obj(view);
    lv_obj_set_pos(dk.grid, GRID_X - 8, GRID_Y - 8);
    lv_obj_set_size(dk.grid, SCREEN_W - 2 * GRID_X + 16, 2 * KEY_H + KEY_GAP + 16);
    lv_obj_set_style_pad_all(dk.grid, 8, 0);
    lv_obj_add_flag(dk.grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(dk.grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(dk.grid, on_scroll_end, LV_EVENT_SCROLL_END, NULL);

    dk.dots = ui_plain_obj(view);
    lv_obj_set_size(dk.dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(dk.dots, 8, 0);
    lv_obj_set_style_pad_column(dk.dots, 8, 0);

    dk.message = ui_plain_obj(view);
    lv_obj_set_size(dk.message, SCREEN_W, SCREEN_H - TOPBAR_H);
    lv_obj_set_pos(dk.message, 0, TOPBAR_H);
    lv_obj_set_style_pad_hor(dk.message, 96, 0);
    lv_obj_set_flex_flow(dk.message, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dk.message, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(dk.message, 12, 0);
    ui_text_label(dk.message, &mi_64, COL_GREEN, ICON_GRID);
    dk.message_title = ui_text_label(dk.message, &fig_b_40, COL_TEXT, "");
    dk.message_text = ui_text_label(dk.message, &fig_md_22, COL_MUTED, "");
    lv_obj_set_width(dk.message_text, 520);
    lv_label_set_long_mode(dk.message_text, LV_LABEL_LONG_MODE_WRAP);

    show_message("Keine Verbindung zum PC", "Die Tasten erscheinen, sobald das Gerät mit der PC-App verbunden ist.");
}

static void build_key(deck_key_t * key, size_t index)
{
    /* pages to the right: page by page, 4 × 2 each; downwards: row by row */
    int32_t x, y;
    if(dk.vertical) {
        x = (int32_t)(index % COLUMNS) * (KEY_W + KEY_GAP);
        y = (int32_t)(index / COLUMNS) * ROW_H;
    }
    else {
        size_t in_page = index % PAGE_KEYS;
        x = (int32_t)(index / PAGE_KEYS) * PAGE_W + (int32_t)(in_page % COLUMNS) * (KEY_W + KEY_GAP);
        y = (int32_t)(in_page / COLUMNS) * ROW_H;
    }

    key->icon_label = NULL;
    key->image = NULL;
    key->text = NULL;
    if(key->empty) {
        /* empty slot: only a subtle outline so the positions stay */
        lv_obj_t * slot = ui_plain_obj(dk.grid);
        lv_obj_remove_flag(slot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(slot, KEY_W, KEY_H);
        lv_obj_set_pos(slot, x, y);
        lv_obj_set_style_radius(slot, 16, 0);
        lv_obj_set_style_border_width(slot, 2, 0);
        lv_obj_set_style_border_color(slot, lv_color_hex(0x2A2A2A), 0);
        key->button = slot;
        return;
    }
    lv_obj_t * btn = lv_button_create(dk.grid);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, KEY_W, KEY_H);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 12, 0);
    lv_obj_add_event_cb(btn, on_key_event, LV_EVENT_PRESSED, key);
    lv_obj_add_event_cb(btn, on_key_event, LV_EVENT_RELEASED, key);
    lv_obj_add_event_cb(btn, on_key_event, LV_EVENT_PRESS_LOST, key);
    lv_obj_add_event_cb(btn, on_key_event, LV_EVENT_CLICKED, key);
    key->button = btn;

    if(key->icon_id[0] && key->icon_fill) {
        /* picture as background of the whole key (corners rounded by the PC), label on top at the bottom */
        key->image = lv_image_create(btn);
        lv_obj_remove_flag(key->image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(key->image, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(key->image, KEY_W, KEY_H);
        lv_obj_set_pos(key->image, 0, 0);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_bottom(btn, 12, 0);
    }
    else if(key->icon_id[0]) {
        /* keep the space now; the picture follows right after the layout */
        key->image = lv_image_create(btn);
        lv_obj_remove_flag(key->image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(key->image, ICON_SIZE, ICON_SIZE);
    }
    else {
        key->icon_label = ui_text_label(btn, &lc_48, key->color, glyph_for(key->icon));
        /* as tall as a picture icon so all labels line up */
        lv_obj_set_height(key->icon_label, ICON_SIZE);
        lv_obj_set_style_pad_top(key->icon_label, (ICON_SIZE - lv_font_get_line_height(&lc_48)) / 2, 0);
    }
    key->text = ui_text_label(btn, &fig_md_22, key->text_color, key->label);
    lv_obj_set_size(key->text, KEY_W - 16, lv_font_get_line_height(&fig_md_22) + 4);
    lv_obj_set_style_text_align(key->text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(key->text, LV_LABEL_LONG_MODE_DOTS);
    set_key_lit(key, false);
}

/* ---------- Data ---------- */

void ui_set_keys(const char * page_name, bool vertical, const ui_key_t * keys, size_t count)
{
    dk.loaded = true;
    clear_keys();

    size_t used = 0;
    for(size_t i = 0; i < count; i++) used += keys[i].empty ? 0 : 1;
    if(used == 0) {
        show_message("Keine Tasten belegt", "Die Belegung lässt sich in der PC-App einstellen.");
        return;
    }
    bool mode_changed = dk.vertical != vertical;
    dk.vertical = vertical;
    lv_obj_set_scroll_dir(dk.grid, vertical ? LV_DIR_VER : LV_DIR_HOR);
    for(size_t i = 0; i < count && dk.count < MAX_KEYS; i++) {
        deck_key_t * key = &dk.keys[dk.count];
        snprintf(key->label, sizeof(key->label), "%s", keys[i].label ? keys[i].label : "");
        snprintf(key->icon, sizeof(key->icon), "%s", keys[i].icon ? keys[i].icon : "keyboard");
        snprintf(key->icon_id, sizeof(key->icon_id), "%s", keys[i].icon_id ? keys[i].icon_id : "");
        key->color = keys[i].color;
        key->text_color = keys[i].text_color;
        key->icon_fill = keys[i].icon_fill;
        key->empty = keys[i].empty;
        key->pixels = NULL;
        build_key(key, dk.count);
        dk.count++;
    }
    lv_obj_update_layout(dk.grid);
    /* start at the front for a new direction, else stay where you were (a layout can arrive after every save) */
    if(mode_changed) lv_obj_scroll_to(dk.grid, 0, 0, LV_ANIM_OFF);
    else scroll_to_stop(current_stop(), LV_ANIM_OFF);
    lv_label_set_text(dk.title, page_name ? page_name : "");
    ui_set_hidden(dk.message, true);
    ui_set_hidden(dk.grid, false);
    ui_set_hidden(dk.title, false);
    build_dots();
}

void ui_put_key_icon(const char * id, uint16_t w, uint16_t h, const uint8_t * data, size_t len)
{
    if(len != (size_t)w * h * 3) return; /* RGB565 (2 bytes) + alpha (1 byte) per pixel */
    for(size_t i = 0; i < dk.count; i++) {
        deck_key_t * key = &dk.keys[i];
        if(key->empty || key->image == NULL || strcmp(key->icon_id, id) != 0) continue;
        uint8_t * copy = malloc(len);
        if(copy == NULL) return;
        memcpy(copy, data, len);
        lv_image_set_src(key->image, NULL);
        free_pixels(key);
        key->pixels = copy;
        memset(&key->dsc, 0, sizeof(key->dsc));
        key->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        key->dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
        key->dsc.header.w = w;
        key->dsc.header.h = h;
        key->dsc.header.stride = w * 2;
        key->dsc.data = copy;
        key->dsc.data_size = (uint32_t)len;
        lv_image_set_src(key->image, &key->dsc);
    }
}

void ui_deck_on_show(void)
{
    if(!ui_is_connected()) {
        show_message("Keine Verbindung zum PC", "Die Tasten erscheinen, sobald das Gerät mit der PC-App verbunden ist.");
        return;
    }
    if(dk.requested) return; /* fetch the layout once per connection; the PC app pushes changes by itself */
    if(!dk.loaded) show_message("Tasten werden geladen …", "");
    dk.requested = true;
    ui_send_command("keys", NULL);
}

void ui_deck_on_disconnect(void)
{
    dk.loaded = false;
    dk.requested = false;
    clear_keys();
    show_message("Keine Verbindung zum PC", "Die Tasten erscheinen, sobald das Gerät mit der PC-App verbunden ist.");
}
