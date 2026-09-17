/**
 * Device interface: top bar and the music view.
 * Display 800×480; colours and fonts in theme.h.
 *
 * Playlists, keys, audio and artist page live in their own files.
 */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "theme.h"
#include "ui_internal.h"

/* shared building blocks are called ui_… outside, short here */
#define plain_obj   ui_plain_obj
#define text_label  ui_text_label
#define set_hidden  ui_set_hidden
#define jpeg_size   ui_jpeg_size

/* ---------- Dimensions ---------- */
#define COVER_X          32
#define COVER_Y          72
#define COVER_SIZE       240
#define META_X           304
#define META_Y           70
#define META_W           (SCREEN_W - META_X - 32)
#define PROGRESS_X       32
#define PROGRESS_Y       326
#define PROGRESS_W       (SCREEN_W - 2 * PROGRESS_X)
#define PROGRESS_H       32
#define CONTROLS_Y       380
#define VOLPANEL_Y       236

/* ---------- Timing ---------- */
#define TICK_MS              16
#define SCROLL_EVERY_MS      20000 /* cut-off lines scroll through once every 20 s */
#define SCROLL_AFTER_TRACK   4000  /* and once shortly after a track change */
#define VOLPANEL_HIDE_MS     3000
#define TOAST_MS             1600
#define VOLUME_SEND_MS       80    /* batch knob commands */
/* after local input the display wins for a moment; the PC reports the new state late */
#define HOLD_MODES_MS        2000
#define HOLD_LIKE_MS         3000
#define HOLD_VOLUME_MS       2500 /* Spotify reports the new volume noticeably late */
#define HOLD_PLAY_MS         1500

/* ---------- Wave ---------- */
#define WAVE_STEP        2.0f
#define WAVE_LEN         36.0f
#define WAVE_AMP         5.0f
#define WAVE_EDGE        12.0f
#define WAVE_MAX_POINTS  ((int)(PROGRESS_W / WAVE_STEP) + 3)

#define PI_F 3.14159265f
#define TEXT_MAX 160


/* ---------- Marquee for cut-off lines ---------- */
typedef struct {
    lv_obj_t * clip;
    lv_obj_t * label;
    const lv_font_t * font;
    int32_t width;
    int32_t dist;
    uint32_t start;
    bool running;
} scroller_t;

/* ---------- State ---------- */
static struct {
    char title[TEXT_MAX];
    char artists[TEXT_MAX];
    char context[TEXT_MAX];
    float pos_s;
    uint32_t pos_tick; /* moment at which pos_s was valid */
    float dur_s;
    bool playing;
    uint8_t shuffle;
    uint8_t repeat;
    int8_t liked;
    bool spotify_login;
    int8_t volume;
    bool muted;
    lv_color_t color;
    bool knob_present;
    bool connected;
    bool from_pc;      /* false = sample data */
    int demo_index;
    int view;
    uint32_t touched_modes, touched_like, touched_volume, touched_play, touched_seek;
    bool seeking;      /* finger is on the progress bar */
    float seek_pos;
    struct { char id[40]; char name[96]; } artist_refs[6]; /* for the artist page */
    char source[40];
    bool spotify;
    struct { char id[20]; char name[40]; bool playing; } sources[8];
    size_t source_count;
    bool can_seek, can_shuffle, can_repeat, can_prev, can_next, has_volume;
    bool chooser_sources; /* the chooser lists sources, not artists */
    size_t artist_ref_count;
} st;

/* ---------- Objects ---------- */
static struct {
    lv_obj_t * bg_canvas;
    uint16_t * bg_pixels; /* 800×480 RGB565, in PSRAM on the device */
    lv_obj_t * views[VIEW_COUNT];
    lv_obj_t * tabs[TAB_COUNT];
    lv_obj_t * knob_icon;
    lv_obj_t * conn_icon;
    lv_obj_t * time_label;

    lv_obj_t * cover;
    lv_obj_t * cover_icon;
    lv_obj_t * cover_img;
    lv_draw_buf_t * cover_buf;
    scroller_t context;
    scroller_t title;
    scroller_t artists;

    lv_obj_t * progress;
    lv_obj_t * seek_dot;
    lv_obj_t * wave;
    lv_point_precise_t wave_points[WAVE_MAX_POINTS];
    float wave_last_end;

    lv_obj_t * btn_volume;
    lv_obj_t * btn_shuffle;
    lv_obj_t * shuffle_sparkle; /* small sparkle top right = Smart Shuffle */
    lv_obj_t * btn_prev;
    lv_obj_t * btn_play;
    lv_obj_t * btn_next;
    lv_obj_t * btn_repeat;
    lv_obj_t * btn_like;

    lv_obj_t * volpanel;
    lv_obj_t * vol_icon;
    lv_obj_t * vol_slider;
    lv_obj_t * vol_value;
    lv_timer_t * volpanel_timer;
    lv_timer_t * volume_send_timer;

    lv_obj_t * status;
    lv_obj_t * status_icon;
    lv_obj_t * status_title;

    lv_obj_t * chooser;
    lv_obj_t * chooser_list;
    lv_obj_t * source_pill;
    lv_obj_t * source_label;
    lv_obj_t * source_chevron;
    lv_obj_t * status_text;

    lv_obj_t * toast;
    lv_timer_t * toast_timer;
} ui;

static uint32_t next_scroll_tick;
static lv_indev_read_cb_t knob_original_read;
static void (*command_handler)(const char * json);

static void show_offline(void);

/* ---------- Sample data while the PC app sends nothing ---------- */
static const struct {
    const char * title;
    const char * artists;
    const char * context;
    float dur_s;
    uint32_t color;
} DEMO[] = {
    { "Nachtfahrt über die A7", "Lena Kessler", "Rastplatz Süd", 238, 0x2E4A7A },
    { "Kupferdraht und Regen, der nicht aufhört zu fallen", "Die Werkstattband, Mira Holt, Jonas Feld", "Lieblingssongs", 312, 0x7A2E3A },
    { "Grünphase", "Morgen & Mira", "Ampelsinfonie", 187, 0x1F6B5C },
};
#define DEMO_COUNT ((int)(sizeof(DEMO) / sizeof(DEMO[0])))

/* =====================================================================
 *  Helpers
 * ===================================================================== */

lv_obj_t * plain_obj(lv_obj_t * parent)
{
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t * text_label(lv_obj_t * parent, const lv_font_t * font, lv_color_t color, const char * text)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

/* Round icon button; the icon is child 0, an active dot (if wanted) child 1 */
static lv_obj_t * icon_button(lv_obj_t * parent, const char * icon, const lv_font_t * font,
                              int32_t w, int32_t h, lv_event_cb_t cb, bool with_dot)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, COL_TEXT, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(btn, 36, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label = text_label(btn, font, COL_TEXT, icon);
    lv_obj_center(label);

    if(with_dot) {
        lv_obj_t * dot = plain_obj(btn);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, COL_GREEN, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
    return btn;
}

static void button_set_icon(lv_obj_t * btn, const char * icon, lv_color_t color)
{
    lv_obj_t * label = lv_obj_get_child(btn, 0);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_color(label, color, 0);
}

static void button_set_dot(lv_obj_t * btn, bool on)
{
    lv_obj_t * dot = lv_obj_get_child(btn, 1);
    if(dot == NULL) return;
    if(on) lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
}

void set_hidden(lv_obj_t * obj, bool hidden)
{
    if(hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void copy_text(char * dst, const char * src)
{
    snprintf(dst, TEXT_MAX, "%s", src ? src : "");
}

static float ease_in_out(float t)
{
    return 0.5f - 0.5f * cosf(PI_F * t);
}

/* Command to the PC app (only while real data is shown) */
static void send_cmd(const char * cmd, const char * value_json)
{
    if(!st.from_pc || command_handler == NULL) return;
    char buf[160];
    if(value_json != NULL) snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\",\"value\":%s}", cmd, value_json);
    else snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\"}", cmd);
    command_handler(buf);
}

/* =====================================================================
 *  Marquee
 * ===================================================================== */

static void scroller_init(scroller_t * s, lv_obj_t * parent, const lv_font_t * font, lv_color_t color, int32_t width)
{
    s->font = font;
    s->width = width;
    s->running = false;
    s->clip = plain_obj(parent);
    /* a little room at the bottom, or the box cuts off descenders (g, y, p) */
    lv_obj_set_size(s->clip, width, lv_font_get_line_height(font) + 4);
    s->label = text_label(s->clip, font, color, "");
    lv_label_set_long_mode(s->label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s->label, width);
}

static void scroller_stop(scroller_t * s)
{
    s->running = false;
    lv_obj_set_style_translate_x(s->label, 0, 0);
    lv_label_set_long_mode(s->label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s->label, s->width);
}

static void scroller_set_text(scroller_t * s, const char * text)
{
    if(strcmp(lv_label_get_text(s->label), text) == 0) return;
    scroller_stop(s);
    lv_label_set_text(s->label, text);
}

static void scroller_start(scroller_t * s)
{
    if(s->running || lv_obj_has_flag(s->clip, LV_OBJ_FLAG_HIDDEN)) return;
    lv_point_t size;
    lv_text_get_size(&size, lv_label_get_text(s->label), s->font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if(size.x <= s->width + 2) return;
    s->dist = size.x - s->width;
    s->start = lv_tick_get();
    s->running = true;
    lv_label_set_long_mode(s->label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(s->label, size.x);
}

/* hold briefly, ease to the end, hold, ease back, hold, then "…" again */
static void scroller_tick(scroller_t * s)
{
    if(!s->running) return;
    const uint32_t hold = 1200;
    const uint32_t travel = LV_MAX(1400, (uint32_t)s->dist * 1000 / 60); /* 60 px per second */
    const uint32_t t = lv_tick_elaps(s->start);
    float x;
    if(t < hold) x = 0;
    else if(t < hold + travel) x = -ease_in_out((float)(t - hold) / travel) * s->dist;
    else if(t < 2 * hold + travel) x = (float)-s->dist;
    else if(t < 2 * hold + 2 * travel) x = -s->dist + ease_in_out((float)(t - 2 * hold - travel) / travel) * s->dist;
    else if(t < 3 * hold + 2 * travel) x = 0;
    else {
        scroller_stop(s);
        return;
    }
    lv_obj_set_style_translate_x(s->label, (int32_t)lroundf(x), 0);
}

/* =====================================================================
 *  Messages and volume bar (they share the same spot)
 * ===================================================================== */

static void toast_hide_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
}

void ui_toast(const char * text)
{
    lv_label_set_text(ui.toast, text);
    lv_obj_add_flag(ui.volpanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(ui.toast_timer);
    lv_timer_resume(ui.toast_timer);
}

static void volpanel_hide_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    lv_obj_add_flag(ui.volpanel, LV_OBJ_FLAG_HIDDEN);
}

static void volpanel_update(void)
{
    int value = st.volume < 0 ? 0 : st.volume;
    lv_slider_set_value(ui.vol_slider, st.muted ? 0 : value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui.vol_slider, st.muted ? COL_MUTED : COL_GREEN, LV_PART_INDICATOR);
    if(st.muted) lv_label_set_text(ui.vol_value, "aus");
    else lv_label_set_text_fmt(ui.vol_value, "%d", value);
    lv_label_set_text(ui.vol_icon, st.muted ? ICON_LC_VOLUME_OFF : ICON_LC_VOLUME);
    button_set_icon(ui.btn_volume, st.muted ? ICON_LC_VOLUME_OFF : ICON_LC_VOLUME, COL_TEXT);
}

static void volpanel_show(void)
{
    if(st.view != VIEW_NOW) return;
    lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.volpanel, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(ui.volpanel_timer);
    lv_timer_resume(ui.volpanel_timer);
}

/* fast turning makes many steps; only send the latest value every 80 ms */
static void volume_send_cb(lv_timer_t * t)
{
    char value[8];
    snprintf(value, sizeof(value), "%d", st.volume);
    send_cmd("volume", value);
    lv_timer_pause(t);
}

static void volume_changed(void)
{
    st.touched_volume = lv_tick_get();
    volpanel_update();
    volpanel_show();
    if(lv_timer_get_paused(ui.volume_send_timer)) {
        lv_timer_reset(ui.volume_send_timer);
        lv_timer_resume(ui.volume_send_timer);
    }
}

/* =====================================================================
 *  Updating the display
 * ===================================================================== */

/* Horizontal gradient with 4×4 Bayer dithering. At 16-bit colour the in-between steps are missing
   and the gradient breaks into visible bands; LVGL 9 no longer dithers gradients itself. */
static void render_background(lv_color_t left, lv_color_t right)
{
    static const uint8_t BAYER[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
    };
    if(ui.bg_pixels == NULL) return;
    for(int32_t x = 0; x < SCREEN_W; x++) {
        float t = (float)x / (SCREEN_W - 1);
        float r = (left.red + (right.red - left.red) * t) * 31.0f / 255.0f;
        float g = (left.green + (right.green - left.green) * t) * 63.0f / 255.0f;
        float b = (left.blue + (right.blue - left.blue) * t) * 31.0f / 255.0f;
        for(int32_t y = 0; y < SCREEN_H; y++) {
            float d = (BAYER[y & 3][x & 3] + 0.5f) / 16.0f;
            uint16_t r5 = (uint16_t)LV_MIN(31, (int)(r + d));
            uint16_t g6 = (uint16_t)LV_MIN(63, (int)(g + d));
            uint16_t b5 = (uint16_t)LV_MIN(31, (int)(b + d));
            ui.bg_pixels[y * SCREEN_W + x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }
    lv_obj_invalidate(ui.bg_canvas);
}

static void update_background(void)
{
    static lv_color_t last;
    static bool drawn;
    if(drawn && lv_color_eq(last, st.color)) return; /* recompute only for a new colour */
    last = st.color;
    drawn = true;
    /* tinted on the left, almost black on the right (like the draft) */
    render_background(lv_color_mix(st.color, COL_INK, 140), lv_color_mix(st.color, COL_INK, 38));
    lv_obj_set_style_bg_color(ui.cover, lv_color_mix(st.color, COL_TEXT, 200), 0);
}

static void update_buttons(void)
{
    button_set_icon(ui.btn_play, st.playing ? ICON_PAUSE : ICON_PLAY, COL_INK);

    /* Smart Shuffle like in Spotify: the same shuffle icon with a small sparkle */
    button_set_icon(ui.btn_shuffle, ICON_LC_SHUFFLE, st.shuffle ? COL_GREEN : COL_MUTED);
    button_set_dot(ui.btn_shuffle, st.shuffle != 0);
    set_hidden(ui.shuffle_sparkle, st.shuffle != 2);

    button_set_icon(ui.btn_repeat, st.repeat == 2 ? ICON_LC_REPEAT_ONE : ICON_LC_REPEAT, st.repeat ? COL_GREEN : COL_MUTED);
    button_set_dot(ui.btn_repeat, st.repeat != 0);

    /* Like keeps its place while logged in – otherwise the whole row shifts on a track change;
       while the state is briefly unknown it is only dimmed */
    set_hidden(ui.btn_like, st.from_pc && !(st.spotify && st.spotify_login));
    /* other players: only what the source supports */
    set_hidden(ui.btn_shuffle, st.from_pc && !st.can_shuffle);
    set_hidden(ui.btn_repeat, st.from_pc && !st.can_repeat);
    lv_obj_set_style_opa(ui.btn_prev, !st.from_pc || st.can_prev ? LV_OPA_COVER : 90, 0);
    lv_obj_set_style_opa(ui.btn_next, !st.from_pc || st.can_next ? LV_OPA_COVER : 90, 0);
    set_hidden(ui.progress, st.from_pc && st.dur_s <= 0);

    bool show_source = st.from_pc && st.source[0] != '\0';
    set_hidden(ui.source_pill, !show_source);
    if(show_source) {
        lv_label_set_text(ui.source_label, st.source);
        set_hidden(ui.source_chevron, st.source_count < 2);
    }
    /* not liked: thin plus in a circle (Lucide); liked: filled green check (Material), like Spotify */
    lv_obj_set_style_text_font(lv_obj_get_child(ui.btn_like, 0), st.liked > 0 ? &mi_44 : &lc_36, 0);
    button_set_icon(ui.btn_like, st.liked > 0 ? ICON_CHECK_CIRCLE : ICON_LC_CIRCLE_PLUS, st.liked > 0 ? COL_GREEN : COL_MUTED);
    lv_obj_set_style_text_opa(lv_obj_get_child(ui.btn_like, 0), st.liked < 0 ? 90 : LV_OPA_COVER, 0);

    /* with a knob, no second volume button */
    set_hidden(ui.btn_volume, st.knob_present || (st.from_pc && !st.has_volume));
    lv_label_set_text(ui.conn_icon, st.connected ? ICON_USB : ICON_LINK_OFF);
    lv_obj_set_style_text_color(ui.conn_icon, st.connected ? COL_TEXT : COL_MUTED, 0);
}

static void update_texts(void)
{
    bool has_context = st.context[0] != '\0';
    set_hidden(ui.context.clip, !has_context);
    /* without a context line the block moves down a little */
    lv_obj_set_y(lv_obj_get_parent(ui.title.clip), has_context ? META_Y : META_Y + 30);
    scroller_set_text(&ui.context, st.context);
    scroller_set_text(&ui.title, st.title);
    scroller_set_text(&ui.artists, st.artists);
}

static float current_pos(void)
{
    if(st.seeking) return st.seek_pos; /* while dragging the wave follows the finger */
    float pos = st.pos_s;
    if(st.playing) pos += lv_tick_elaps(st.pos_tick) / 1000.0f;
    if(st.dur_s > 0 && pos > st.dur_s) pos = st.dur_s;
    return pos;
}

/* played part as a fixed wave over the straight grey line; grows with the position only */
static void update_wave(void)
{
    float end = st.dur_s > 0 ? current_pos() / st.dur_s * PROGRESS_W : 0;
    if(fabsf(end - ui.wave_last_end) < 0.05f) return;
    ui.wave_last_end = end;

    if(end < 1.0f) {
        lv_obj_add_flag(ui.wave, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const float mid = PROGRESS_H / 2.0f;
    int n = 0;
    for(float x = 0; x < end && n < WAVE_MAX_POINTS - 1; x += WAVE_STEP) {
        float taper = LV_MIN(1.0f, LV_MIN(x / WAVE_EDGE, (end - x) / WAVE_EDGE));
        ui.wave_points[n].x = x;
        ui.wave_points[n].y = mid + sinf(x / WAVE_LEN * 2 * PI_F) * WAVE_AMP * taper;
        n++;
    }
    ui.wave_points[n].x = end;
    ui.wave_points[n].y = mid;
    n++;
    lv_line_set_points_mutable(ui.wave, ui.wave_points, n);
    lv_obj_remove_flag(ui.wave, LV_OBJ_FLAG_HIDDEN);
}

static void update_clock(void)
{
    time_t now = time(NULL);
    struct tm * local = localtime(&now);
    if(local == NULL) return;
    lv_label_set_text_fmt(ui.time_label, "%02d:%02d", local->tm_hour, local->tm_min);
}

static void schedule_scroll(uint32_t in_ms)
{
    next_scroll_tick = lv_tick_get() + in_ms;
}

static void update_all(void)
{
    update_background();
    update_texts();
    update_buttons();
    volpanel_update();
    ui.wave_last_end = -1;
    update_wave();
}

/* =====================================================================
 *  Sample mode
 * ===================================================================== */

static void demo_load(int index)
{
    st.demo_index = (index + DEMO_COUNT) % DEMO_COUNT;
    copy_text(st.title, DEMO[st.demo_index].title);
    copy_text(st.artists, DEMO[st.demo_index].artists);
    copy_text(st.context, DEMO[st.demo_index].context);
    st.dur_s = DEMO[st.demo_index].dur_s;
    st.pos_s = 0;
    st.pos_tick = lv_tick_get();
    st.color = lv_color_hex(DEMO[st.demo_index].color);
    st.liked = 0;
    update_all();
    schedule_scroll(SCROLL_AFTER_TRACK);
}

/* =====================================================================
 *  Controls
 * ===================================================================== */

static void set_playing(bool playing)
{
    st.pos_s = current_pos();
    st.pos_tick = lv_tick_get();
    st.playing = playing;
    update_buttons();
}

static void toggle_play(void)
{
    set_playing(!st.playing);
    st.touched_play = lv_tick_get();
    send_cmd("play_pause", NULL);
}

static void on_play(lv_event_t * e)
{
    LV_UNUSED(e);
    toggle_play();
}

static void on_next(lv_event_t * e)
{
    LV_UNUSED(e);
    if(st.from_pc) send_cmd("next", NULL);
    else demo_load(st.demo_index + 1);
}

static void on_prev(lv_event_t * e)
{
    LV_UNUSED(e);
    if(st.from_pc) {
        send_cmd("prev", NULL);
        return;
    }
    if(current_pos() > 3) {
        st.pos_s = 0;
        st.pos_tick = lv_tick_get();
        ui.wave_last_end = -1;
    }
    else demo_load(st.demo_index - 1);
}

static void on_shuffle(lv_event_t * e)
{
    LV_UNUSED(e);
    /* Smart Shuffle can only be switched on in Spotify; the button toggles off ↔ on */
    st.shuffle = st.shuffle ? 0 : 1;
    st.touched_modes = lv_tick_get();
    update_buttons();
    ui_toast(st.shuffle ? "Zufallswiedergabe an" : "Zufallswiedergabe aus");
    send_cmd("shuffle", st.shuffle ? "true" : "false");
}

static void on_repeat(lv_event_t * e)
{
    LV_UNUSED(e);
    static const char * const TEXT[] = { "Wiederholen aus", "Playlist wiederholen", "Titel wiederholen" };
    static const char * const VALUE[] = { "0", "1", "2" };
    st.repeat = (uint8_t)((st.repeat + 1) % 3);
    st.touched_modes = lv_tick_get();
    update_buttons();
    ui_toast(TEXT[st.repeat]);
    send_cmd("repeat", VALUE[st.repeat]);
}

static void on_like(lv_event_t * e)
{
    LV_UNUSED(e);
    if(st.liked < 0) return;
    st.liked = st.liked ? 0 : 1;
    st.touched_like = lv_tick_get();
    update_buttons();
    ui_toast(st.liked ? "Zu Lieblingssongs hinzugefügt" : "Aus Lieblingssongs entfernt");
    send_cmd("like", st.liked ? "true" : "false");
}

/* seeking: tap or drag, jump to the spot on release */
static void on_progress(lv_event_t * e)
{
    if(st.from_pc && !st.can_seek) return; /* the source can't seek */
    lv_event_code_t code = lv_event_get_code(e);
    if(st.dur_s <= 0) return;

    if(code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        lv_area_t area;
        lv_obj_get_coords(ui.progress, &area);
        float frac = (float)(point.x - area.x1) / (float)lv_area_get_width(&area);
        frac = LV_CLAMP(0.0f, frac, 1.0f);
        st.seeking = true;
        st.seek_pos = frac * st.dur_s;
        lv_obj_set_x(ui.seek_dot, (int32_t)(frac * PROGRESS_W) - 11);
        lv_obj_remove_flag(ui.seek_dot, LV_OBJ_FLAG_HIDDEN);
        ui.wave_last_end = -1;
        update_wave();
    }
    else if((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && st.seeking) {
        st.seeking = false;
        st.pos_s = st.seek_pos;
        st.pos_tick = lv_tick_get();
        st.touched_seek = lv_tick_get();
        lv_obj_add_flag(ui.seek_dot, LV_OBJ_FLAG_HIDDEN);
        ui.wave_last_end = -1;
        update_wave();
        char value[16];
        snprintf(value, sizeof(value), "%.1f", st.pos_s);
        send_cmd("seek", value);
    }
}

static void on_volume_button(lv_event_t * e)
{
    LV_UNUSED(e);
    volpanel_update();
    volpanel_show();
}

static void on_mute(lv_event_t * e)
{
    LV_UNUSED(e);
    st.muted = !st.muted;
    st.touched_volume = lv_tick_get();
    volpanel_update();
    volpanel_show();
    send_cmd("mute", st.muted ? "true" : "false");
}

static void on_slider(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    st.volume = (int8_t)lv_slider_get_value(slider);
    st.muted = false;
    volume_changed();
}

static void show_view(int view)
{
    if(st.view == VIEW_AUDIO && view != VIEW_AUDIO) ui_audio_on_hide();
    st.view = view;
    for(int i = 0; i < VIEW_COUNT; i++) set_hidden(ui.views[i], i != view);
    lv_obj_add_flag(ui.chooser, LV_OBJ_FLAG_HIDDEN);
    for(int i = 0; i < TAB_COUNT; i++) {
        bool active = i == view;
        lv_obj_set_style_bg_opa(ui.tabs[i], active ? 26 : LV_OPA_TRANSP, 0);
        for(uint32_t c = 0; c < lv_obj_get_child_count(ui.tabs[i]); c++) {
            lv_obj_set_style_text_color(lv_obj_get_child(ui.tabs[i], c), active ? COL_TEXT : COL_MUTED, 0);
        }
    }
    if(view != VIEW_NOW) lv_obj_add_flag(ui.volpanel, LV_OBJ_FLAG_HIDDEN);
    if(view == VIEW_LIBRARY) ui_library_on_show();
    if(view == VIEW_DECK) ui_deck_on_show();
    if(view == VIEW_AUDIO) ui_audio_on_show();
}

void ui_set_pages(bool music, bool playlists, bool keys, bool audio)
{
    bool visible[TAB_COUNT] = { music, playlists, keys, audio };
    int first = -1;
    for(int i = 0; i < TAB_COUNT; i++) if(visible[i] && first < 0) first = i;
    if(first < 0) {
        first = VIEW_NOW; /* never hide everything */
        visible[VIEW_NOW] = true;
    }
    for(int i = 0; i < TAB_COUNT; i++) set_hidden(ui.tabs[i], !visible[i]);
    /* open page hidden: go to the first visible one (the artist page belongs to music) */
    int current = st.view == VIEW_ARTIST ? VIEW_NOW : st.view;
    if(current < TAB_COUNT && !visible[current]) show_view(first);
}

static void on_tab(lv_event_t * e)
{
    show_view((int)(intptr_t)lv_event_get_user_data(e));
}

/* ---------- Artists → artist page ---------- */

static void on_chooser_item(lv_event_t * e)
{
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    lv_obj_add_flag(ui.chooser, LV_OBJ_FLAG_HIDDEN);
    if(st.chooser_sources) {
        if(i < st.source_count) {
            char value[32];
            snprintf(value, sizeof(value), "\"%s\"", st.sources[i].id);
            send_cmd("source", value);
            copy_text(st.source, st.sources[i].name); /* show the choice at once; the PC confirms */
            update_buttons();
        }
        return;
    }
    if(i < st.artist_ref_count) ui_artist_open(st.artist_refs[i].id, st.artist_refs[i].name);
}

static void on_chooser_backdrop(lv_event_t * e)
{
    if(lv_event_get_target(e) == ui.chooser) lv_obj_add_flag(ui.chooser, LV_OBJ_FLAG_HIDDEN);
}

static void on_artists(lv_event_t * e)
{
    LV_UNUSED(e);
    if(!st.connected || !st.spotify || !st.spotify_login || st.artist_ref_count == 0) return;
    if(st.artist_ref_count == 1) {
        ui_artist_open(st.artist_refs[0].id, st.artist_refs[0].name);
        return;
    }
    st.chooser_sources = false;
    lv_obj_clean(ui.chooser_list);
    text_label(ui.chooser_list, &fig_sb_20, COL_MUTED, "Interpreten");
    for(size_t i = 0; i < st.artist_ref_count; i++) {
        lv_obj_t * item = lv_button_create(ui.chooser_list);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_PCT(100), 64);
        lv_obj_set_style_radius(item, 12, 0);
        lv_obj_set_style_pad_hor(item, 14, 0);
        lv_obj_set_style_bg_color(item, COL_TEXT, 0);
        lv_obj_set_style_bg_opa(item, 26, LV_STATE_PRESSED);
        lv_obj_add_event_cb(item, on_chooser_item, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t * label = text_label(item, &fig_sb_28, COL_TEXT, st.artist_refs[i].name);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    }
    lv_obj_remove_flag(ui.chooser, LV_OBJ_FLAG_HIDDEN);
}

/* Source pill on the cover: pick which player the page shows */
static void on_source(lv_event_t * e)
{
    LV_UNUSED(e);
    if(!st.connected || st.source_count < 2) return;
    st.chooser_sources = true;
    lv_obj_clean(ui.chooser_list);
    text_label(ui.chooser_list, &fig_sb_20, COL_MUTED, "Quelle");
    for(size_t i = 0; i < st.source_count; i++) {
        lv_obj_t * item = lv_button_create(ui.chooser_list);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_PCT(100), 64);
        lv_obj_set_style_radius(item, 12, 0);
        lv_obj_set_style_pad_hor(item, 14, 0);
        lv_obj_set_style_bg_color(item, COL_TEXT, 0);
        lv_obj_set_style_bg_opa(item, strcmp(st.sources[i].name, st.source) == 0 ? 18 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(item, 36, LV_STATE_PRESSED);
        lv_obj_add_event_cb(item, on_chooser_item, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t * label = text_label(item, &fig_sb_28, COL_TEXT, st.sources[i].name);
        lv_obj_set_width(label, 300);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        if(st.sources[i].playing) {
            lv_obj_t * note = text_label(item, &mi_28, COL_GREEN, ICON_VOLUME);
            lv_obj_align(note, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
    lv_obj_remove_flag(ui.chooser, LV_OBJ_FLAG_HIDDEN);
}

void ui_show_view(int view)
{
    show_view(view);
}

bool ui_is_connected(void)
{
    return st.connected;
}

void ui_send_command(const char * cmd, const char * value_json)
{
    if(!st.connected || command_handler == NULL) return;
    char buf[200];
    if(value_json != NULL) snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\",\"value\":%s}", cmd, value_json);
    else snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\"}", cmd);
    command_handler(buf);
}

void ui_knob_rotate(int32_t steps)
{
    /* on the playlists page the knob moves through the cards, otherwise it is the volume */
    if(st.view == VIEW_LIBRARY) {
        ui_library_knob_rotate(steps);
        return;
    }
    if(st.view == VIEW_ARTIST) {
        ui_artist_knob_rotate(steps); /* on the artist page through the albums */
        return;
    }
    if(st.view == VIEW_DECK) {
        ui_deck_knob_rotate(steps); /* on the key page choose a key */
        return;
    }
    if(st.view == VIEW_AUDIO) {
        ui_audio_knob_rotate(steps); /* on the audio page the master volume */
        return;
    }
    if(st.view != VIEW_NOW) show_view(VIEW_NOW);
    int value = (st.volume < 0 ? 50 : st.volume) + steps * 2;
    st.volume = (int8_t)LV_CLAMP(0, value, 100);
    st.muted = false;
    volume_changed();
}

void ui_knob_press(void)
{
    if(st.view == VIEW_LIBRARY) {
        ui_library_knob_press(); /* play the marked playlist */
        return;
    }
    if(st.view == VIEW_ARTIST) {
        ui_artist_knob_press(); /* play the marked album */
        return;
    }
    if(st.view == VIEW_DECK) {
        ui_deck_knob_press(); /* trigger the marked key */
        return;
    }
    if(st.view == VIEW_AUDIO) {
        ui_audio_knob_press(); /* mute master */
        return;
    }
    if(st.view != VIEW_NOW) show_view(VIEW_NOW);
    toggle_play();
}

void ui_set_knob_present(bool present)
{
    st.knob_present = present;
    update_buttons();
}

/* Simulator: treat the Windows driver's mouse wheel and middle button as the knob
   instead of triggering LVGL's focus navigation. The real device calls ui_knob_* directly. */
static void knob_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    static bool was_pressed;
    knob_original_read(indev, data);
    if(data->enc_diff != 0) ui_knob_rotate(-data->enc_diff); /* wheel up = louder */
    bool pressed = data->state == LV_INDEV_STATE_PRESSED;
    if(was_pressed && !pressed) ui_knob_press();
    was_pressed = pressed;
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_RELEASED;
}

/* =====================================================================
 *  Tick
 * ===================================================================== */

static void tick_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    static uint32_t last_clock;

    if(!st.from_pc && st.dur_s > 0 && current_pos() >= st.dur_s) demo_load(st.demo_index + 1);

    update_wave();
    scroller_tick(&ui.context);
    scroller_tick(&ui.title);
    scroller_tick(&ui.artists);

    if(lv_tick_get() >= next_scroll_tick) {
        scroller_start(&ui.context);
        scroller_start(&ui.title);
        scroller_start(&ui.artists);
        schedule_scroll(SCROLL_EVERY_MS);
    }
    if(lv_tick_elaps(last_clock) >= 1000) {
        last_clock = lv_tick_get();
        update_clock();
    }
}

/* =====================================================================
 *  Layout
 * ===================================================================== */

static void build_topbar(lv_obj_t * screen)
{
    static const struct { const char * icon; const char * text; } TABS[TAB_COUNT] = {
        { ICON_MUSIC, "Medien" }, { ICON_LIBRARY, "Playlists" }, { ICON_GRID, "Tasten" }, { ICON_TUNE, "Audio" },
    };

    lv_obj_t * bar = plain_obj(screen);
    lv_obj_set_size(bar, SCREEN_W, TOPBAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t * tabs = plain_obj(bar);
    lv_obj_set_size(tabs, LV_SIZE_CONTENT, 48);
    lv_obj_align(tabs, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs, 4, 0);

    for(int i = 0; i < TAB_COUNT; i++) {
        lv_obj_t * tab = lv_button_create(tabs);
        lv_obj_remove_style_all(tab);
        lv_obj_set_height(tab, 48);
        lv_obj_set_width(tab, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(tab, 18, 0);
        lv_obj_set_style_radius(tab, 12, 0);
        lv_obj_set_style_bg_color(tab, COL_TEXT, 0);
        lv_obj_set_style_bg_opa(tab, 40, LV_STATE_PRESSED);
        lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(tab, 10, 0);
        text_label(tab, &mi_28, COL_MUTED, TABS[i].icon);
        text_label(tab, &fig_sb_24, COL_MUTED, TABS[i].text);
        lv_obj_add_event_cb(tab, on_tab, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui.tabs[i] = tab;
    }

    lv_obj_t * right = plain_obj(bar);
    lv_obj_set_size(right, LV_SIZE_CONTENT, 48);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -24, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 18, 0);
    ui.conn_icon = text_label(right, &mi_28, COL_MUTED, ICON_LINK_OFF);
    ui.time_label = text_label(right, &fig_sb_24, COL_TEXT, "--:--");
}

static void build_now_view(lv_obj_t * view)
{
    /* background as a self-drawn image (dithered), behind everything else */
    ui.bg_pixels = lv_malloc((size_t)SCREEN_W * SCREEN_H * sizeof(uint16_t));
    ui.bg_canvas = lv_canvas_create(view);
    if(ui.bg_pixels != NULL) {
        lv_canvas_set_buffer(ui.bg_canvas, ui.bg_pixels, SCREEN_W, SCREEN_H, LV_COLOR_FORMAT_RGB565);
    }
    lv_obj_set_pos(ui.bg_canvas, 0, 0);
    lv_obj_remove_flag(ui.bg_canvas, LV_OBJ_FLAG_CLICKABLE);

    /* cover: placeholder with a note icon until a picture arrives from the PC app */
    ui.cover = plain_obj(view);
    lv_obj_set_size(ui.cover, COVER_SIZE, COVER_SIZE);
    lv_obj_set_pos(ui.cover, COVER_X, COVER_Y);
    lv_obj_set_style_radius(ui.cover, 10, 0);
    lv_obj_set_style_clip_corner(ui.cover, true, 0);
    lv_obj_set_style_bg_opa(ui.cover, LV_OPA_COVER, 0);
    ui.cover_icon = text_label(ui.cover, &mi_64, COL_TEXT, ICON_MUSIC);
    lv_obj_set_style_text_opa(ui.cover_icon, 150, 0);
    lv_obj_center(ui.cover_icon);
    ui.cover_img = lv_image_create(ui.cover);
    lv_obj_set_size(ui.cover_img, COVER_SIZE, COVER_SIZE);
    lv_obj_add_flag(ui.cover_img, LV_OBJ_FLAG_HIDDEN);

    /* source pill in the cover's bottom-left corner: where the media comes from, tap to switch */
    ui.source_pill = lv_button_create(view);
    lv_obj_remove_style_all(ui.source_pill);
    lv_obj_set_size(ui.source_pill, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_max_width(ui.source_pill, COVER_SIZE - 20, 0);
    lv_obj_set_pos(ui.source_pill, COVER_X + 10, COVER_Y + COVER_SIZE - 46);
    lv_obj_set_style_radius(ui.source_pill, 18, 0);
    lv_obj_set_style_bg_color(ui.source_pill, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ui.source_pill, 180, 0);
    lv_obj_set_style_bg_opa(ui.source_pill, 230, LV_STATE_PRESSED);
    lv_obj_set_style_pad_left(ui.source_pill, 14, 0);
    lv_obj_set_style_pad_right(ui.source_pill, 12, 0);
    lv_obj_set_flex_flow(ui.source_pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.source_pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui.source_pill, 6, 0);
    lv_obj_set_ext_click_area(ui.source_pill, 8);
    lv_obj_add_event_cb(ui.source_pill, on_source, LV_EVENT_CLICKED, NULL);
    ui.source_label = text_label(ui.source_pill, &fig_sb_20, COL_TEXT, "");
    lv_label_set_long_mode(ui.source_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(ui.source_label, lv_font_get_line_height(&fig_sb_20));
    lv_obj_set_style_max_width(ui.source_label, COVER_SIZE - 70, 0);
    ui.source_chevron = text_label(ui.source_pill, &lc_16, COL_TEXT, ICON_LC_CHEVRON_DOWN);
    lv_obj_add_flag(ui.source_pill, LV_OBJ_FLAG_HIDDEN);

    /* context, title, artists */
    lv_obj_t * meta = plain_obj(view);
    lv_obj_set_size(meta, META_W, 200);
    lv_obj_set_pos(meta, META_X, META_Y);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(meta, 2, 0);
    scroller_init(&ui.context, meta, &fig_md_22, COL_MUTED, META_W);
    scroller_init(&ui.title, meta, &fig_xb_42, COL_TEXT, META_W);
    scroller_init(&ui.artists, meta, &fig_md_30, COL_MUTED, META_W);

    /* progress: grey line across the full width, the white wave on top */
    lv_obj_t * progress = plain_obj(view);
    ui.progress = progress;
    lv_obj_set_size(progress, PROGRESS_W, PROGRESS_H);
    lv_obj_set_pos(progress, PROGRESS_X, PROGRESS_Y);
    lv_obj_add_flag(progress, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    /* seeking: the whole bar reacts, with some margin so it is easy to hit */
    lv_obj_add_flag(progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(progress, 28);
    lv_obj_add_event_cb(progress, on_progress, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(progress, on_progress, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(progress, on_progress, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(progress, on_progress, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t * track = plain_obj(progress);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE); /* otherwise the grey line swallows the tap for seeking */
    lv_obj_set_size(track, PROGRESS_W, 6);
    lv_obj_align(track, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(track, 3, 0);
    lv_obj_set_style_bg_color(track, COL_TRACK, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);

    ui.wave = lv_line_create(progress);
    lv_obj_set_size(ui.wave, PROGRESS_W, PROGRESS_H);
    lv_obj_set_pos(ui.wave, 0, 0);
    lv_obj_set_style_line_width(ui.wave, 6, 0);
    lv_obj_set_style_line_color(ui.wave, COL_TEXT, 0);
    lv_obj_set_style_line_rounded(ui.wave, true, 0);
    lv_obj_remove_flag(ui.wave, LV_OBJ_FLAG_CLICKABLE);
    ui.wave_last_end = -1;

    /* dot at the finger position, only visible while dragging */
    ui.seek_dot = plain_obj(progress);
    lv_obj_remove_flag(ui.seek_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui.seek_dot, 22, 22);
    lv_obj_set_y(ui.seek_dot, (PROGRESS_H - 22) / 2);
    lv_obj_set_style_radius(ui.seek_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.seek_dot, COL_TEXT, 0);
    lv_obj_set_style_bg_opa(ui.seek_dot, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui.seek_dot, LV_OBJ_FLAG_HIDDEN);

    /* button row: (volume without knob) shuffle, previous, play, next, repeat, like */
    lv_obj_t * row = plain_obj(view);
    lv_obj_set_size(row, SCREEN_W - 32, 88);
    lv_obj_set_pos(row, 16, CONTROLS_Y);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui.btn_volume = icon_button(row, ICON_LC_VOLUME, &lc_36, 104, 88, on_volume_button, false);
    ui.btn_shuffle = icon_button(row, ICON_LC_SHUFFLE, &lc_36, 104, 88, on_shuffle, true);
    ui.shuffle_sparkle = text_label(ui.btn_shuffle, &lc_16, COL_GREEN, ICON_LC_SPARKLE); /* child 2 */
    lv_obj_align(ui.shuffle_sparkle, LV_ALIGN_CENTER, 20, -18);
    lv_obj_add_flag(ui.shuffle_sparkle, LV_OBJ_FLAG_HIDDEN);
    ui.btn_prev = icon_button(row, ICON_PREV, &mi_44, 104, 88, on_prev, false);

    ui.btn_play = icon_button(row, ICON_PAUSE, &mi_44, 88, 88, on_play, false);
    lv_obj_set_style_radius(ui.btn_play, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ui.btn_play, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ui.btn_play, lv_color_hex(0xD9D9D9), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ui.btn_play, LV_OPA_COVER, LV_STATE_PRESSED);

    ui.btn_next = icon_button(row, ICON_NEXT, &mi_44, 104, 88, on_next, false);
    ui.btn_repeat = icon_button(row, ICON_LC_REPEAT, &lc_36, 104, 88, on_repeat, true);
    ui.btn_like = icon_button(row, ICON_LC_CIRCLE_PLUS, &lc_36, 104, 88, on_like, false);

    /* volume bar in the free area below title and artists */
    ui.volpanel = plain_obj(view);
    lv_obj_set_size(ui.volpanel, META_W, 64);
    lv_obj_set_pos(ui.volpanel, META_X, VOLPANEL_Y);
    lv_obj_set_style_radius(ui.volpanel, 16, 0);
    lv_obj_set_style_bg_color(ui.volpanel, COL_INK, 0);
    lv_obj_set_style_bg_opa(ui.volpanel, 217, 0);
    lv_obj_set_style_pad_left(ui.volpanel, 4, 0);
    lv_obj_set_style_pad_right(ui.volpanel, 18, 0);
    lv_obj_set_flex_flow(ui.volpanel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.volpanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui.volpanel, 14, 0);

    lv_obj_t * mute = icon_button(ui.volpanel, ICON_LC_VOLUME, &lc_28, 56, 56, on_mute, false);
    ui.vol_icon = lv_obj_get_child(mute, 0);

    ui.vol_slider = lv_slider_create(ui.volpanel);
    lv_obj_set_flex_grow(ui.vol_slider, 1);
    lv_obj_set_height(ui.vol_slider, 10);
    lv_slider_set_range(ui.vol_slider, 0, 100);
    lv_obj_set_style_bg_color(ui.vol_slider, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui.vol_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.vol_slider, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui.vol_slider, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(ui.vol_slider, 10, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(ui.vol_slider, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(ui.vol_slider, on_slider, LV_EVENT_VALUE_CHANGED, NULL);

    ui.vol_value = text_label(ui.volpanel, &fig_sb_28, COL_TEXT, "0");
    lv_obj_set_width(ui.vol_value, 52);
    lv_obj_set_style_text_align(ui.vol_value, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_add_flag(ui.volpanel, LV_OBJ_FLAG_HIDDEN);
    ui.volpanel_timer = lv_timer_create(volpanel_hide_cb, VOLPANEL_HIDE_MS, NULL);
    lv_timer_pause(ui.volpanel_timer);
    ui.volume_send_timer = lv_timer_create(volume_send_cb, VOLUME_SEND_MS, NULL);
    lv_timer_pause(ui.volume_send_timer);

    /* hint instead of playback (e.g. Spotify closed) */
    ui.status = plain_obj(view);
    lv_obj_set_size(ui.status, SCREEN_W, SCREEN_H - TOPBAR_H);
    lv_obj_set_pos(ui.status, 0, TOPBAR_H);
    lv_obj_set_style_bg_color(ui.status, COL_INK, 0);
    lv_obj_set_style_bg_opa(ui.status, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(ui.status, 96, 0);
    lv_obj_set_flex_flow(ui.status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui.status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(ui.status, 12, 0);
    ui.status_icon = text_label(ui.status, &mi_64, COL_GREEN, ICON_MUSIC);
    ui.status_title = text_label(ui.status, &fig_b_40, COL_TEXT, "");
    ui.status_text = text_label(ui.status, &fig_md_22, COL_MUTED, "");
    lv_obj_set_width(ui.status_text, 480);
    lv_label_set_long_mode(ui.status_text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_add_flag(ui.status, LV_OBJ_FLAG_HIDDEN);

    /* tapping the artists opens the artist page (needs a Spotify login) */
    lv_obj_add_flag(ui.artists.clip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.artists.clip, on_artists, LV_EVENT_CLICKED, NULL);

    /* chooser when the song has several artists */
    ui.chooser = plain_obj(view);
    lv_obj_set_size(ui.chooser, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(ui.chooser, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui.chooser, 153, 0);
    lv_obj_add_event_cb(ui.chooser, on_chooser_backdrop, LV_EVENT_CLICKED, NULL);
    ui.chooser_list = plain_obj(ui.chooser);
    lv_obj_set_width(ui.chooser_list, 440);
    lv_obj_set_height(ui.chooser_list, LV_SIZE_CONTENT);
    lv_obj_center(ui.chooser_list);
    lv_obj_set_style_bg_color(ui.chooser_list, COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui.chooser_list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.chooser_list, 18, 0);
    lv_obj_set_style_pad_all(ui.chooser_list, 16, 0);
    lv_obj_set_flex_flow(ui.chooser_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(ui.chooser, LV_OBJ_FLAG_HIDDEN);
}

static void build_placeholder_view(lv_obj_t * view, const char * text)
{
    lv_obj_set_style_bg_color(view, COL_INK, 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);
    lv_obj_t * label = text_label(view, &fig_md_22, COL_MUTED, text);
    lv_obj_center(label);
}

void ui_init(lv_display_t * display, lv_indev_t * knob)
{
    LV_UNUSED(display);
    memset(&st, 0, sizeof(st));
    st.volume = 62;
    st.playing = true;
    st.knob_present = knob != NULL;

    lv_obj_t * screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, COL_INK, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    for(int i = 0; i < VIEW_COUNT; i++) {
        ui.views[i] = plain_obj(screen);
        lv_obj_set_size(ui.views[i], SCREEN_W, SCREEN_H);
    }
    build_now_view(ui.views[VIEW_NOW]);
    ui_library_build(ui.views[VIEW_LIBRARY]);
    ui_deck_build(ui.views[VIEW_DECK]);
    ui_audio_build(ui.views[VIEW_AUDIO]);
    ui_artist_build(ui.views[VIEW_ARTIST]);
    build_topbar(screen);

    /* messages: in the same spot as the volume bar */
    ui.toast = text_label(lv_layer_top(), &fig_sb_24, COL_INK, "");
    lv_obj_set_style_bg_color(ui.toast, COL_TEXT, 0);
    lv_obj_set_style_bg_opa(ui.toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.toast, 12, 0);
    lv_obj_set_style_pad_hor(ui.toast, 22, 0);
    lv_obj_set_style_pad_ver(ui.toast, 14, 0);
    lv_obj_set_pos(ui.toast, META_X, VOLPANEL_Y + 6);
    lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
    ui.toast_timer = lv_timer_create(toast_hide_cb, TOAST_MS, NULL);
    lv_timer_pause(ui.toast_timer);

    if(knob != NULL) {
        knob_original_read = lv_indev_get_read_cb(knob);
        lv_indev_set_read_cb(knob, knob_read);
    }

    show_view(VIEW_NOW);
    ui_waiting_show(true); /* until the PC app says hello; the start animation plays on top of it */
    ui_splash_start();
    demo_load(0);
    show_offline(); /* until the PC app says hello */
    update_clock();
    lv_timer_create(tick_cb, TICK_MS, NULL);
}

/* =====================================================================
 *  Data from the PC app
 * ===================================================================== */

static void show_offline(void)
{
    ui_set_status_message("Keine Verbindung zum PC",
                          "Prüfe das USB-Kabel oder ob die App am PC läuft. Gesucht wird über USB und Bluetooth.");
    lv_label_set_text(ui.status_icon, ICON_LINK_OFF);
}

void ui_set_command_handler(void (*handler)(const char * json))
{
    command_handler = handler;
}

void ui_set_connected(bool connected)
{
    st.connected = connected;
    ui_waiting_show(!connected);
    if(!connected) {
        /* don't leave a stale cover; show the hint instead of playback */
        st.from_pc = false;
        st.artist_ref_count = 0;
        lv_obj_add_flag(ui.cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui.cover_icon, LV_OBJ_FLAG_HIDDEN);
        demo_load(0);
        ui_library_on_disconnect();
        ui_deck_on_disconnect();
        ui_audio_on_disconnect();
        if(st.view == VIEW_ARTIST) show_view(VIEW_NOW);
        show_offline();
    }
    else {
        ui_set_status_message(NULL, NULL); /* the first state from the PC sets the right hint right away */
        if(st.view == VIEW_DECK) ui_deck_on_show(); /* key page was open when the connection came */
        if(st.view == VIEW_AUDIO) ui_audio_on_show();
    }
    if(connected && st.view == VIEW_LIBRARY) {
        ui_library_on_show(); /* playlists page was open when the connection came */
    }
    update_buttons();
}

void ui_set_status_message(const char * title, const char * text)
{
    if(title == NULL) {
        lv_obj_add_flag(ui.status, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(ui.status_icon, ICON_MUSIC);
    lv_label_set_text(ui.status_title, title);
    lv_label_set_text(ui.status_text, text ? text : "");
    lv_obj_remove_flag(ui.status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.volpanel, LV_OBJ_FLAG_HIDDEN);
}

/* Read width and height from the JPEG's SOF segment. For in-memory pictures LVGL's TJPGD takes
   them from the image descriptor instead of the file – with 0 it draws nothing. */
bool jpeg_size(const uint8_t * data, size_t len, uint16_t * w, uint16_t * h)
{
    size_t i = 2; /* after FF D8 */
    while(i + 9 < len) {
        if(data[i] != 0xFF) return false;
        uint8_t marker = data[i + 1];
        uint16_t seg_len = (uint16_t)((data[i + 2] << 8) | data[i + 3]);
        if(marker >= 0xC0 && marker <= 0xC3) { /* SOF0–SOF3 */
            *h = (uint16_t)((data[i + 5] << 8) | data[i + 6]);
            *w = (uint16_t)((data[i + 7] << 8) | data[i + 8]);
            return true;
        }
        i += 2 + seg_len;
    }
    return false;
}

void ui_set_cover_jpeg(const uint8_t * data, size_t len)
{
    lv_draw_buf_t * buf = ui_jpeg_decode(data, len);
    if(buf == NULL) return;

    /* detach the old picture from the display before freeing it */
    lv_image_set_src(ui.cover_img, NULL);
    ui_image_free(ui.cover_buf);
    ui.cover_buf = buf;

    lv_image_set_src(ui.cover_img, buf);
    lv_obj_remove_flag(ui.cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.cover_icon, LV_OBJ_FLAG_HIDDEN);
}

void ui_set_playback(const ui_playback_t * pb)
{
    bool track_changed = !st.from_pc ||
                         strcmp(st.title, pb->title ? pb->title : "") != 0 ||
                         strcmp(st.artists, pb->artists ? pb->artists : "") != 0;
    st.from_pc = true;
    copy_text(st.title, pb->title);
    copy_text(st.artists, pb->artists);
    copy_text(st.context, pb->context);

    /* only adjust the position when it is noticeably off (seek, track change) – otherwise the wave stutters */
    bool seek_hold = st.seeking || lv_tick_elaps(st.touched_seek) < 1500; /* don't jump back after seeking */
    if(track_changed || (!seek_hold && fabsf(pb->pos_s - current_pos()) > 1.0f)) {
        st.pos_s = pb->pos_s;
        st.pos_tick = lv_tick_get();
    }
    st.dur_s = pb->dur_s;

    if(lv_tick_elaps(st.touched_play) > HOLD_PLAY_MS && st.playing != pb->playing) set_playing(pb->playing);
    if(lv_tick_elaps(st.touched_modes) > HOLD_MODES_MS) {
        st.shuffle = pb->shuffle;
        st.repeat = pb->repeat;
    }
    st.spotify_login = pb->spotify_login;
    copy_text(st.source, pb->source);
    st.spotify = pb->spotify;
    st.can_seek = pb->can_seek;
    st.can_shuffle = pb->can_shuffle;
    st.can_repeat = pb->can_repeat;
    st.can_prev = pb->can_prev;
    st.can_next = pb->can_next;
    st.has_volume = pb->has_volume;
    st.source_count = 0;
    for(size_t i = 0; pb->sources && i < pb->source_count && i < 8; i++) {
        snprintf(st.sources[i].id, sizeof(st.sources[i].id), "%s", pb->sources[i].id ? pb->sources[i].id : "");
        snprintf(st.sources[i].name, sizeof(st.sources[i].name), "%s", pb->sources[i].name ? pb->sources[i].name : "");
        st.sources[i].playing = pb->sources[i].playing;
        st.source_count++;
    }
    st.artist_ref_count = 0;
    for(size_t i = 0; pb->artist_refs && i < pb->artist_ref_count && i < 6; i++) {
        snprintf(st.artist_refs[i].id, sizeof(st.artist_refs[i].id), "%s", pb->artist_refs[i].id ? pb->artist_refs[i].id : "");
        snprintf(st.artist_refs[i].name, sizeof(st.artist_refs[i].name), "%s", pb->artist_refs[i].name ? pb->artist_refs[i].name : "");
        st.artist_ref_count++;
    }
    if(pb->liked < 0 || lv_tick_elaps(st.touched_like) > HOLD_LIKE_MS) st.liked = pb->liked;
    if(pb->volume >= 0 && lv_tick_elaps(st.touched_volume) > HOLD_VOLUME_MS) {
        st.volume = pb->volume;
        st.muted = pb->muted;
    }
    st.color = pb->color;

    update_all();
    if(track_changed) schedule_scroll(SCROLL_AFTER_TRACK);
}
