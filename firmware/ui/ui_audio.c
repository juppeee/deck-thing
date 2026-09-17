/**
 * Audio: a volume mixer like in Windows. Master, microphone and every app with its own audio session,
 * with fader, mute and live level; plus choosing the output device.
 *
 * Two layouts, chosen in the PC app: mixer (vertical faders, swipe sideways)
 * or list (horizontal sliders, scroll down). The PC app only sends data while the page
 * is open – about ten times per second so the levels move.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"

#define MAX_APPS      16
#define MAX_OUTPUTS   8
#define MAX_ICONS     32
#define HOLD_MS       1500  /* after a local change, ignore values from the PC this long */
#define SEND_MS       80    /* batch fader moves */
#define BODY_Y        TOPBAR_H
#define BODY_H        (SCREEN_H - TOPBAR_H)

#define COL_RAISE     lv_color_hex(0x1C1C1C)
#define COL_KNOB_BG   lv_color_hex(0x2E2E2E)
#define COL_METER_BG  lv_color_hex(0x262626)
#define COL_RED       lv_color_hex(0xF3727F)
#define COL_MUTED_BAR lv_color_hex(0x555555)

typedef enum { KIND_OUT, KIND_MIC, KIND_APP } kind_t;

typedef struct {
    kind_t kind;
    char id[24];
    char name[64];
    char sub[80];
    char icon_id[17];
    lv_color_t color;
    int volume;
    bool muted;
    uint8_t level;
    uint32_t touched;
    bool pending_volume;
    /* widgets of the current layout */
    lv_obj_t * track;
    lv_obj_t * fill;
    lv_obj_t * meter;
    lv_obj_t * value;
    lv_obj_t * mute_icon;
    lv_obj_t * mute_btn;
    lv_obj_t * icon_img;
    lv_obj_t * icon_letter;
    lv_obj_t * name_label;
    lv_obj_t * sub_label;
    bool vertical;
} channel_t;

typedef struct {
    char id[24];
    char name[80];
    bool active;
} output_t;

typedef struct {
    char id[17];
    uint8_t * data;
    lv_image_dsc_t dsc;
} icon_t;

static struct {
    lv_obj_t * view;
    lv_obj_t * body;       /* rebuilt for a new channel list or layout */
    lv_obj_t * message;
    lv_obj_t * message_title;
    lv_obj_t * message_text;
    lv_obj_t * output_name; /* mixer: name on the tile */
    lv_obj_t * sheet;
    channel_t out, mic;
    bool has_out, has_mic;
    channel_t apps[MAX_APPS];
    size_t app_count;
    output_t outputs[MAX_OUTPUTS];
    size_t output_count;
    bool list_layout;
    bool loaded;
    bool open;
    char signature[512];
    lv_timer_t * send_timer;
    icon_t icons[MAX_ICONS];
    size_t icon_next;
} au;

/* ---------- Helpers ---------- */

static void copy(char * dst, size_t size, const char * src)
{
    snprintf(dst, size, "%s", src ? src : "");
}

static void show_message(const char * title, const char * text)
{
    lv_label_set_text(au.message_title, title);
    lv_label_set_text(au.message_text, text);
    ui_set_hidden(au.message, false);
    if(au.body) ui_set_hidden(au.body, true);
}

static const lv_image_dsc_t * icon_get(const char * id)
{
    if(id == NULL || id[0] == '\0') return NULL;
    for(size_t i = 0; i < MAX_ICONS; i++) {
        if(au.icons[i].data != NULL && strcmp(au.icons[i].id, id) == 0) return &au.icons[i].dsc;
    }
    return NULL;
}

/* ---------- Showing a channel ---------- */

static void update_channel(channel_t * ch)
{
    if(ch->track == NULL) return;
    int32_t vol = LV_CLAMP(0, ch->volume, 100);
    if(ch->vertical) lv_obj_set_height(ch->fill, lv_pct(vol));
    else lv_obj_set_width(ch->fill, lv_pct(vol));
    lv_obj_set_style_bg_color(ch->fill, ch->muted ? COL_MUTED_BAR : COL_GREEN, 0);
    if(ch->muted) lv_label_set_text(ch->value, "aus");
    else lv_label_set_text_fmt(ch->value, "%d", (int)vol);
    const char * glyph = ch->kind == KIND_MIC ? (ch->muted ? ICON_LC_MIC_OFF : ICON_LC_MIC)
                                              : (ch->muted ? ICON_LC_VOLUME_OFF : ICON_LC_VOLUME);
    lv_label_set_text(ch->mute_icon, glyph);
    lv_obj_set_style_bg_color(ch->mute_btn, ch->muted ? COL_RED : COL_KNOB_BG, 0);
    lv_obj_set_style_text_color(ch->mute_icon, ch->muted ? COL_INK : COL_TEXT, 0);
    if(ch->icon_img) lv_obj_set_style_opa(lv_obj_get_parent(ch->icon_img), ch->muted ? 115 : LV_OPA_COVER, 0);
}

static void update_level(channel_t * ch)
{
    if(ch->meter == NULL) return;
    int32_t level = ch->muted ? 0 : ch->level;
    if(ch->vertical) lv_obj_set_height(ch->meter, lv_pct(level));
    else lv_obj_set_width(ch->meter, lv_pct(level));
}

static void update_icon(channel_t * ch)
{
    if(ch->icon_img == NULL) return;
    const lv_image_dsc_t * dsc = icon_get(ch->icon_id);
    lv_image_set_src(ch->icon_img, dsc);
    ui_set_hidden(ch->icon_img, dsc == NULL);
    ui_set_hidden(ch->icon_letter, dsc != NULL);
}

/* ---------- Controls ---------- */

static void send_cb(lv_timer_t * t)
{
    bool more = false;
    channel_t * all[MAX_APPS + 2];
    size_t n = 0;
    if(au.has_out) all[n++] = &au.out;
    if(au.has_mic) all[n++] = &au.mic;
    for(size_t i = 0; i < au.app_count; i++) all[n++] = &au.apps[i];
    for(size_t i = 0; i < n; i++) {
        if(!all[i]->pending_volume) continue;
        char value[64];
        snprintf(value, sizeof(value), "{\"id\":\"%s\",\"vol\":%d}", all[i]->id, all[i]->volume);
        ui_send_command("audio_volume", value);
        all[i]->pending_volume = false;
        more = true;
    }
    if(!more) lv_timer_pause(t);
}

static void set_volume(channel_t * ch, int volume)
{
    ch->volume = LV_CLAMP(0, volume, 100);
    ch->muted = false;
    ch->touched = lv_tick_get();
    ch->pending_volume = true;
    update_channel(ch);
    update_level(ch);
    if(lv_timer_get_paused(au.send_timer)) {
        lv_timer_reset(au.send_timer);
        lv_timer_resume(au.send_timer);
    }
}

static void on_track(lv_event_t * e)
{
    channel_t * ch = lv_event_get_user_data(e);
    lv_indev_t * indev = lv_indev_active();
    if(indev == NULL || ch->track == NULL) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(ch->track, &a);
    int32_t ratio;
    if(ch->vertical) ratio = (a.y2 - p.y) * 100 / LV_MAX(1, lv_area_get_height(&a));
    else ratio = (p.x - a.x1) * 100 / LV_MAX(1, lv_area_get_width(&a));
    set_volume(ch, (int)ratio);
}

static void on_mute(lv_event_t * e)
{
    channel_t * ch = lv_event_get_user_data(e);
    ch->muted = !ch->muted;
    ch->touched = lv_tick_get();
    update_channel(ch);
    update_level(ch);
    char value[64];
    snprintf(value, sizeof(value), "{\"id\":\"%s\",\"on\":%s}", ch->id, ch->muted ? "true" : "false");
    ui_send_command("audio_mute", value);
}

/* ---------- Choosing the output device ---------- */

/* On the narrow tile the model is what counts: "Kopfhörer (USB-Headset)" → "USB-Headset" */
static void set_output_label(const char * name)
{
    const char * open = strchr(name, '(');
    size_t len = strlen(name);
    if(open != NULL && len > 0 && name[len - 1] == ')' && open + 1 < name + len - 1) {
        char inner[80];
        snprintf(inner, sizeof(inner), "%.*s", (int)(name + len - 1 - (open + 1)), open + 1);
        lv_label_set_text(au.output_name, inner);
    }
    else lv_label_set_text(au.output_name, name);
}

static void close_sheet(void)
{
    if(au.sheet != NULL) {
        lv_obj_delete(au.sheet);
        au.sheet = NULL;
    }
}

static void on_sheet_backdrop(lv_event_t * e)
{
    if(lv_event_get_target(e) == au.sheet) close_sheet();
}

static void on_output(lv_event_t * e)
{
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    if(i < au.output_count) {
        char value[40];
        snprintf(value, sizeof(value), "\"%s\"", au.outputs[i].id);
        ui_send_command("audio_output", value);
        for(size_t k = 0; k < au.output_count; k++) au.outputs[k].active = k == i;
        if(au.output_name) set_output_label(au.outputs[i].name);
    }
    close_sheet();
}

static void open_sheet(lv_event_t * e)
{
    LV_UNUSED(e);
    close_sheet();
    au.sheet = ui_plain_obj(lv_layer_top());
    lv_obj_set_size(au.sheet, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(au.sheet, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(au.sheet, 140, 0);
    lv_obj_add_flag(au.sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(au.sheet, on_sheet_backdrop, LV_EVENT_CLICKED, NULL);

    lv_obj_t * box = ui_plain_obj(au.sheet);
    lv_obj_set_width(box, 540);
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(box, COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 20, 0);
    lv_obj_set_style_pad_all(box, 16, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 2, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE); /* tapping inside the card doesn't close it */

    lv_obj_t * title = ui_text_label(box, &fig_sb_24, COL_TEXT, "Ton ausgeben über");
    lv_obj_set_style_pad_left(title, 8, 0);
    lv_obj_set_style_pad_bottom(title, 10, 0);

    for(size_t i = 0; i < au.output_count; i++) {
        lv_obj_t * opt = lv_button_create(box);
        lv_obj_remove_style_all(opt);
        lv_obj_set_size(opt, lv_pct(100), 60);
        lv_obj_set_style_radius(opt, 12, 0);
        lv_obj_set_style_bg_color(opt, COL_TEXT, 0);
        lv_obj_set_style_bg_opa(opt, au.outputs[i].active ? 18 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(opt, 36, LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(opt, 14, 0);
        lv_obj_set_flex_flow(opt, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(opt, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(opt, 14, 0);
        lv_obj_add_event_cb(opt, on_output, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_text_label(opt, &lc_28, au.outputs[i].active ? COL_GREEN : COL_MUTED, ICON_LC_SPEAKER);
        lv_obj_t * name = ui_text_label(opt, &fig_sb_20, COL_TEXT, au.outputs[i].name);
        lv_obj_set_height(name, lv_font_get_line_height(&fig_sb_20));
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        if(au.outputs[i].active) ui_text_label(opt, &lc_28, COL_GREEN, ICON_LC_CHECK);
    }
}

/* ---------- Building blocks ---------- */

static lv_obj_t * mute_button(lv_obj_t * parent, channel_t * ch, int32_t w, int32_t h)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, COL_KNOB_BG, 0);
    lv_obj_set_style_opa(btn, 190, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, on_mute, LV_EVENT_CLICKED, ch);
    ch->mute_btn = btn;
    ch->mute_icon = ui_text_label(btn, &lc_28, COL_TEXT, ICON_LC_VOLUME);
    lv_obj_center(ch->mute_icon);
    return btn;
}

/* program icon or, while it is missing, the first letter on a tinted tile */
static void app_icon(lv_obj_t * parent, channel_t * ch, int32_t size)
{
    lv_obj_t * box = ui_plain_obj(parent);
    lv_obj_set_size(box, size, size);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_bg_color(box, ch->color, 0);
    lv_obj_set_style_bg_opa(box, 46, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    if(ch->kind == KIND_APP) {
        char letter[5] = { 0 };
        size_t len = 1;
        while(len < 4 && (ch->name[len] & 0xC0) == 0x80) len++; /* whole UTF-8 character */
        memcpy(letter, ch->name, len);
        ch->icon_letter = ui_text_label(box, &fig_sb_24, ch->color, letter);
        lv_obj_center(ch->icon_letter);
        ch->icon_img = lv_image_create(box);
        lv_obj_center(ch->icon_img);
        update_icon(ch);
    }
    else {
        ch->icon_img = NULL;
        ch->icon_letter = NULL;
        lv_obj_center(ui_text_label(box, &lc_28, ch->color, ch->kind == KIND_MIC ? ICON_LC_MIC : ICON_LC_SPEAKER));
    }
}

/* fader: handles dragging itself so the row doesn't scroll along */
static lv_obj_t * track(lv_obj_t * parent, channel_t * ch, bool vertical)
{
    ch->vertical = vertical;
    lv_obj_t * t = ui_plain_obj(parent);
    lv_obj_set_style_bg_color(t, COL_KNOB_BG, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_ext_click_area(t, 12);
    lv_obj_add_event_cb(t, on_track, LV_EVENT_PRESSED, ch);
    lv_obj_add_event_cb(t, on_track, LV_EVENT_PRESSING, ch);
    ch->track = t;
    ch->fill = ui_plain_obj(t);
    lv_obj_set_style_bg_opa(ch->fill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ch->fill, LV_OBJ_FLAG_CLICKABLE);
    if(vertical) {
        lv_obj_set_style_radius(t, 12, 0);
        lv_obj_set_style_radius(ch->fill, 12, 0);
        lv_obj_set_width(ch->fill, lv_pct(100));
        lv_obj_align(ch->fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    else {
        lv_obj_set_style_radius(t, 11, 0);
        lv_obj_set_style_radius(ch->fill, 11, 0);
        lv_obj_set_height(ch->fill, lv_pct(100));
    }
    return t;
}

static lv_obj_t * meter(lv_obj_t * parent, channel_t * ch, bool vertical)
{
    lv_obj_t * m = ui_plain_obj(parent);
    lv_obj_set_style_bg_color(m, COL_METER_BG, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m, vertical ? 3 : 2, 0);
    lv_obj_remove_flag(m, LV_OBJ_FLAG_CLICKABLE);
    ch->meter = ui_plain_obj(m);
    lv_obj_set_style_bg_color(ch->meter, COL_GREEN, 0);
    lv_obj_set_style_bg_opa(ch->meter, vertical ? LV_OPA_COVER : 150, 0);
    lv_obj_set_style_radius(ch->meter, vertical ? 3 : 2, 0);
    if(vertical) {
        lv_obj_set_width(ch->meter, lv_pct(100));
        lv_obj_align(ch->meter, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    else lv_obj_set_height(ch->meter, lv_pct(100));
    return m;
}

/* ---------- Mixer ---------- */

static void mixer_channel(lv_obj_t * parent, channel_t * ch)
{
    bool app = ch->kind == KIND_APP;
    lv_obj_t * col = ui_plain_obj(parent);
    lv_obj_set_size(col, app ? 100 : 96, lv_pct(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_set_style_pad_top(col, app ? 12 : 4, 0);
    lv_obj_set_style_pad_bottom(col, 10, 0);
    lv_obj_set_style_pad_hor(col, 6, 0);
    if(app) {
        lv_obj_set_style_bg_color(col, COL_RAISE, 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(col, 16, 0);
        app_icon(col, ch, 44);
    }
    else {
        ch->icon_img = NULL;
        ch->icon_letter = NULL;
    }

    const lv_font_t * name_font = app ? &fig_sb_20 : &fig_md_17;
    ch->name_label = ui_text_label(col, name_font, app ? COL_TEXT : COL_MUTED, ch->name);
    /* fixed height = one line; only then LVGL shortens with "…" instead of wrapping */
    lv_obj_set_size(ch->name_label, lv_pct(100), lv_font_get_line_height(name_font));
    lv_obj_set_style_text_align(ch->name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ch->name_label, LV_LABEL_LONG_MODE_DOTS);
    ch->sub_label = NULL;

    lv_obj_t * fader = ui_plain_obj(col);
    lv_obj_set_width(fader, lv_pct(100));
    lv_obj_set_flex_grow(fader, 1);
    lv_obj_set_flex_flow(fader, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fader, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(fader, 6, 0);
    lv_obj_t * t = track(fader, ch, true);
    lv_obj_set_size(t, 40, lv_pct(100));
    lv_obj_t * m = meter(fader, ch, true);
    lv_obj_set_size(m, 6, lv_pct(100));

    ch->value = ui_text_label(col, &fig_sb_20, COL_TEXT, "");
    mute_button(col, ch, 56, 40);
    update_channel(ch);
    update_level(ch);
}

static void build_mixer(void)
{
    lv_obj_t * masters = ui_plain_obj(au.body);
    lv_obj_set_pos(masters, 20, 8);
    lv_obj_set_size(masters, 216, BODY_H - 24);

    lv_obj_t * pick = lv_button_create(masters);
    lv_obj_remove_style_all(pick);
    lv_obj_set_size(pick, 216, 52);
    lv_obj_set_style_radius(pick, 12, 0);
    lv_obj_set_style_bg_color(pick, COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(pick, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(pick, 190, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(pick, 12, 0);
    lv_obj_set_flex_flow(pick, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pick, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pick, 10, 0);
    lv_obj_add_event_cb(pick, open_sheet, LV_EVENT_CLICKED, NULL);
    ui_text_label(pick, &lc_28, COL_GREEN, ICON_LC_SPEAKER);
    const char * active = "Ausgabe";
    for(size_t i = 0; i < au.output_count; i++) if(au.outputs[i].active) active = au.outputs[i].name;
    au.output_name = ui_text_label(pick, &fig_sb_20, COL_TEXT, "");
    set_output_label(active);
    lv_obj_set_height(au.output_name, lv_font_get_line_height(&fig_sb_20));
    lv_obj_set_flex_grow(au.output_name, 1);
    lv_label_set_long_mode(au.output_name, LV_LABEL_LONG_MODE_DOTS);
    ui_text_label(pick, &lc_28, COL_MUTED, ICON_LC_CHEVRON_DOWN);

    lv_obj_t * row = ui_plain_obj(masters);
    lv_obj_set_pos(row, 0, 64);
    lv_obj_set_size(row, 216, BODY_H - 24 - 64);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    if(au.has_out) mixer_channel(row, &au.out);
    if(au.has_mic) mixer_channel(row, &au.mic);

    lv_obj_t * divider = ui_plain_obj(au.body);
    lv_obj_set_pos(divider, 248, 14);
    lv_obj_set_size(divider, 1, BODY_H - 34);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    lv_obj_t * rail = ui_plain_obj(au.body);
    lv_obj_set_pos(rail, 250, 0);
    lv_obj_set_size(rail, SCREEN_W - 250, BODY_H);
    lv_obj_add_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(rail, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(rail, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_left(rail, 14, 0);
    lv_obj_set_style_pad_right(rail, 20, 0);
    lv_obj_set_style_pad_top(rail, 8, 0);
    lv_obj_set_style_pad_bottom(rail, 16, 0);
    lv_obj_set_style_pad_column(rail, 10, 0);
    for(size_t i = 0; i < au.app_count; i++) mixer_channel(rail, &au.apps[i]);
    if(au.app_count == 0) {
        lv_obj_t * hint = ui_text_label(rail, &fig_md_22, COL_MUTED, "Gerade spielt keine App Ton ab.");
        lv_obj_set_style_pad_top(hint, 160, 0);
        lv_obj_set_style_pad_left(hint, 40, 0);
    }
}

/* ---------- List ---------- */

static void list_row(lv_obj_t * parent, channel_t * ch, bool head)
{
    lv_obj_t * row = ui_plain_obj(parent);
    lv_obj_set_size(row, lv_pct(100), 76);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_set_style_bg_color(row, head ? COL_SURFACE : COL_RAISE, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, 0);

    app_icon(row, ch, 48);

    lv_obj_t * info = ui_plain_obj(row);
    lv_obj_set_size(info, 150, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    ch->name_label = ui_text_label(info, &fig_sb_20, COL_TEXT, ch->name);
    lv_obj_set_size(ch->name_label, lv_pct(100), lv_font_get_line_height(&fig_sb_20));
    lv_label_set_long_mode(ch->name_label, LV_LABEL_LONG_MODE_DOTS);
    ch->sub_label = ui_text_label(info, &fig_md_17, COL_MUTED, ch->sub);
    lv_obj_set_size(ch->sub_label, lv_pct(100), lv_font_get_line_height(&fig_md_17));
    lv_label_set_long_mode(ch->sub_label, LV_LABEL_LONG_MODE_DOTS);
    ui_set_hidden(ch->sub_label, ch->sub[0] == '\0');

    lv_obj_t * slider = ui_plain_obj(row);
    lv_obj_set_height(slider, 48);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_flex_flow(slider, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(slider, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(slider, 6, 0);
    lv_obj_t * t = track(slider, ch, false);
    lv_obj_set_size(t, lv_pct(100), 22);
    lv_obj_t * m = meter(slider, ch, false);
    lv_obj_set_size(m, lv_pct(100), 4);

    ch->value = ui_text_label(row, &fig_sb_20, COL_TEXT, "");
    lv_obj_set_width(ch->value, 44);
    lv_obj_set_style_text_align(ch->value, LV_TEXT_ALIGN_RIGHT, 0);
    mute_button(row, ch, 52, 48);

    if(head && au.output_count > 1) {
        lv_obj_t * pick = lv_button_create(row);
        lv_obj_remove_style_all(pick);
        lv_obj_set_size(pick, 52, 48);
        lv_obj_set_style_radius(pick, 12, 0);
        lv_obj_set_style_bg_color(pick, COL_KNOB_BG, 0);
        lv_obj_set_style_bg_opa(pick, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(pick, 190, LV_STATE_PRESSED);
        lv_obj_add_event_cb(pick, open_sheet, LV_EVENT_CLICKED, NULL);
        lv_obj_center(ui_text_label(pick, &lc_28, COL_TEXT, ICON_LC_ARROWS));
    }
    update_channel(ch);
    update_level(ch);
}

static void build_list(void)
{
    au.output_name = NULL;
    lv_obj_add_flag(au.body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(au.body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(au.body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(au.body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(au.body, 20, 0);
    lv_obj_set_style_pad_top(au.body, 4, 0);
    lv_obj_set_style_pad_bottom(au.body, 16, 0);
    lv_obj_set_style_pad_row(au.body, 8, 0);
    if(au.has_out) list_row(au.body, &au.out, true);
    if(au.has_mic) list_row(au.body, &au.mic, false);
    for(size_t i = 0; i < au.app_count; i++) list_row(au.body, &au.apps[i], false);
}

static void clear_widgets(channel_t * ch)
{
    ch->track = ch->fill = ch->meter = ch->value = ch->mute_icon = ch->mute_btn = NULL;
    ch->icon_img = ch->icon_letter = ch->name_label = ch->sub_label = NULL;
}

static void rebuild(void)
{
    close_sheet();
    int32_t scroll_x = 0, scroll_y = 0;
    if(au.body != NULL) {
        scroll_y = lv_obj_get_scroll_y(au.body);
        lv_obj_delete(au.body);
    }
    clear_widgets(&au.out);
    clear_widgets(&au.mic);
    for(size_t i = 0; i < au.app_count; i++) clear_widgets(&au.apps[i]);

    au.body = ui_plain_obj(au.view);
    lv_obj_set_pos(au.body, 0, BODY_Y);
    lv_obj_set_size(au.body, SCREEN_W, BODY_H);
    if(au.list_layout) {
        build_list();
        lv_obj_scroll_to_y(au.body, scroll_y, LV_ANIM_OFF);
    }
    else build_mixer();
    LV_UNUSED(scroll_x);
    ui_set_hidden(au.message, true);
}

/* ---------- Interface ---------- */

void ui_audio_build(lv_obj_t * view)
{
    au.view = view;
    lv_obj_set_style_bg_color(view, COL_INK, 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);

    au.message = ui_plain_obj(view);
    lv_obj_set_size(au.message, SCREEN_W, SCREEN_H - TOPBAR_H);
    lv_obj_set_pos(au.message, 0, TOPBAR_H);
    lv_obj_set_style_pad_hor(au.message, 96, 0);
    lv_obj_set_flex_flow(au.message, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(au.message, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(au.message, 12, 0);
    ui_text_label(au.message, &mi_64, COL_GREEN, ICON_TUNE);
    au.message_title = ui_text_label(au.message, &fig_b_40, COL_TEXT, "");
    au.message_text = ui_text_label(au.message, &fig_md_22, COL_MUTED, "");
    lv_obj_set_width(au.message_text, 560);
    lv_label_set_long_mode(au.message_text, LV_LABEL_LONG_MODE_WRAP);

    au.send_timer = lv_timer_create(send_cb, SEND_MS, NULL);
    lv_timer_pause(au.send_timer);
    show_message("Keine Verbindung zum PC", "Der Mixer erscheint, sobald das Gerät mit der PC-App verbunden ist.");
}

void ui_audio_on_show(void)
{
    if(!ui_is_connected()) {
        show_message("Keine Verbindung zum PC", "Der Mixer erscheint, sobald das Gerät mit der PC-App verbunden ist.");
        return;
    }
    au.open = true;
    if(!au.loaded) show_message("Audio wird geladen …", "");
    ui_send_command("audio", "true");
}

void ui_audio_on_hide(void)
{
    close_sheet();
    if(au.open) ui_send_command("audio", "false");
    au.open = false;
}

void ui_audio_on_disconnect(void)
{
    close_sheet();
    au.loaded = false;
    au.open = false;
    au.signature[0] = '\0';
    if(au.body != NULL) {
        lv_obj_delete(au.body);
        au.body = NULL;
    }
    show_message("Keine Verbindung zum PC", "Der Mixer erscheint, sobald das Gerät mit der PC-App verbunden ist.");
}

/* take the PC's values unless the channel was just changed on the device */
static void take(channel_t * dst, const ui_audio_channel_t * src, kind_t kind, bool same)
{
    bool held = same && lv_tick_elaps(dst->touched) < HOLD_MS;
    if(!same) {
        memset(dst, 0, sizeof(*dst));
        dst->touched = lv_tick_get() - HOLD_MS - 1;
    }
    dst->kind = kind;
    copy(dst->id, sizeof(dst->id), src->id);
    copy(dst->name, sizeof(dst->name), src->name);
    copy(dst->sub, sizeof(dst->sub), src->sub);
    copy(dst->icon_id, sizeof(dst->icon_id), src->icon_id);
    dst->color = src->color;
    dst->level = src->level;
    if(!held && !dst->pending_volume) {
        dst->volume = src->volume;
        dst->muted = src->muted;
    }
}

void ui_set_audio(const ui_audio_t * audio)
{
    if(audio == NULL) return;
    au.loaded = true;

    /* ids of all channels + layout: rebuild only on a change, otherwise update the values */
    char sig[sizeof(au.signature)];
    int n = snprintf(sig, sizeof(sig), "%d|%s|%s|%s", audio->list_layout,
                     audio->out ? audio->out->id : "-", audio->out ? audio->out->sub : "",
                     audio->mic ? audio->mic->id : "-");
    for(size_t i = 0; i < audio->app_count && i < MAX_APPS && n > 0 && n < (int)sizeof(sig); i++) {
        n += snprintf(sig + n, sizeof(sig) - (size_t)n, "|%s", audio->apps[i].id);
    }
    for(size_t i = 0; i < audio->output_count && i < MAX_OUTPUTS && n > 0 && n < (int)sizeof(sig); i++) {
        n += snprintf(sig + n, sizeof(sig) - (size_t)n, "|%s%d", audio->outputs[i].id, audio->outputs[i].active);
    }
    bool same = strcmp(sig, au.signature) == 0 && au.body != NULL;

    au.list_layout = audio->list_layout;
    au.has_out = audio->out != NULL;
    au.has_mic = audio->mic != NULL;
    if(au.has_out) take(&au.out, audio->out, KIND_OUT, same);
    if(au.has_mic) take(&au.mic, audio->mic, KIND_MIC, same);
    size_t count = LV_MIN(audio->app_count, MAX_APPS);
    for(size_t i = 0; i < count; i++) {
        /* the same app in the same spot keeps its interaction state */
        take(&au.apps[i], &audio->apps[i], KIND_APP, same);
    }
    au.app_count = count;
    au.output_count = LV_MIN(audio->output_count, MAX_OUTPUTS);
    for(size_t i = 0; i < au.output_count; i++) {
        copy(au.outputs[i].id, sizeof(au.outputs[i].id), audio->outputs[i].id);
        copy(au.outputs[i].name, sizeof(au.outputs[i].name), audio->outputs[i].name);
        au.outputs[i].active = audio->outputs[i].active;
    }

    if(!same) {
        memcpy(au.signature, sig, sizeof(sig));
        rebuild();
        return;
    }
    channel_t * all[MAX_APPS + 2];
    size_t k = 0;
    if(au.has_out) all[k++] = &au.out;
    if(au.has_mic) all[k++] = &au.mic;
    for(size_t i = 0; i < au.app_count; i++) all[k++] = &au.apps[i];
    for(size_t i = 0; i < k; i++) {
        update_channel(all[i]);
        update_level(all[i]);
    }
}

void ui_put_audio_icon(const char * id, uint16_t w, uint16_t h, const uint8_t * data, size_t len)
{
    if(id == NULL || len != (size_t)w * h * 3 || icon_get(id) != NULL) return;
    icon_t * slot = &au.icons[au.icon_next];
    au.icon_next = (au.icon_next + 1) % MAX_ICONS;
    if(slot->data != NULL) {
        /* detach an evicted icon from the channels first */
        channel_t * all[MAX_APPS];
        for(size_t i = 0; i < au.app_count; i++) all[i] = &au.apps[i];
        for(size_t i = 0; i < au.app_count; i++) {
            if(all[i]->icon_img && strcmp(all[i]->icon_id, slot->id) == 0) lv_image_set_src(all[i]->icon_img, NULL);
        }
        lv_image_cache_drop(&slot->dsc);
        free(slot->data);
        slot->data = NULL;
    }
    uint8_t * copy_data = malloc(len);
    if(copy_data == NULL) return;
    memcpy(copy_data, data, len);
    copy(slot->id, sizeof(slot->id), id);
    slot->data = copy_data;
    memset(&slot->dsc, 0, sizeof(slot->dsc));
    slot->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    slot->dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    slot->dsc.header.w = w;
    slot->dsc.header.h = h;
    slot->dsc.header.stride = w * 2;
    slot->dsc.data = copy_data;
    slot->dsc.data_size = (uint32_t)len;
    for(size_t i = 0; i < au.app_count; i++) update_icon(&au.apps[i]);
}

/* knob: turn = master volume, press = mute master */
void ui_audio_knob_rotate(int32_t steps)
{
    if(!au.has_out || au.out.track == NULL) return;
    set_volume(&au.out, au.out.volume + (int)steps * 2);
}

void ui_audio_knob_press(void)
{
    if(!au.has_out || au.out.mute_btn == NULL) return;
    lv_obj_send_event(au.out.mute_btn, LV_EVENT_CLICKED, NULL);
}
