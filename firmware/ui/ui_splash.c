/**
 * Startanimation: das Logo blitzt auf, springt auf seine Größe, die Balken wippen und das
 * Drehrad dreht sich – danach blendet es aus und die Oberfläche darunter erscheint.
 * Gleicher Ablauf wie die Animation im Logo-Entwurf (animiert.html) und in der PC-App.
 *
 * Die Einzelteile (Grundform, drei Balken, Drehrad) erzeugt companion/tools/make_logo.py
 * aus assets/deck_thing.svg.
 */
#include "images/logo_images.h"
#include "ui_internal.h"

#define POP_MS       700
#define HOLD_MS      1500   /* so lange steht das Logo nach dem Aufspringen */
#define FADE_MS      350
#define SPIN_MS      6000   /* eine Umdrehung des Drehrads */

static lv_obj_t * backdrop;

static void set_opa(void * obj, int32_t v)
{
    lv_obj_set_style_opa(obj, (lv_opa_t)v, 0);
}

static void set_scale(void * obj, int32_t v)
{
    lv_obj_set_style_transform_scale(obj, v, 0);
}

static void set_flash_size(void * obj, int32_t v)
{
    lv_obj_set_size(obj, v, v);
    lv_obj_center(obj);
}

static void set_bar(void * img, int32_t v)
{
    lv_image_set_scale_y(img, (uint32_t)v);
}

static void set_rotation(void * img, int32_t v)
{
    lv_image_set_rotation(img, v);
}

static void anim(lv_obj_t * obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delay,
                 lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

/* Balken wippen endlos zwischen „klein“ und voller Höhe, jeder in eigenem Takt */
static void bounce(lv_obj_t * img, int32_t low_256, uint32_t half_ms, uint32_t delay)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, set_bar);
    lv_anim_set_values(&a, low_256, 256);
    lv_anim_set_duration(&a, half_ms);
    lv_anim_set_reverse_duration(&a, half_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static lv_obj_t * part(lv_obj_t * parent, const lv_image_dsc_t * src, int32_t x, int32_t y)
{
    lv_obj_t * img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_set_pos(img, x, y);
    return img;
}

static void finished(lv_anim_t * a)
{
    LV_UNUSED(a);
    /* löscht mit den Objekten auch deren endlose Balken- und Drehrad-Animationen; asynchron,
       weil wir gerade im Abschluss einer Animation dieses Objekts stecken */
    lv_obj_delete_async(backdrop);
    backdrop = NULL;
}

static void fade_out(lv_timer_t * t)
{
    LV_UNUSED(t);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, backdrop);
    lv_anim_set_exec_cb(&a, set_opa);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, finished);
    lv_anim_start(&a);
}

void ui_splash_start(void)
{
    backdrop = ui_plain_obj(lv_layer_top());
    lv_obj_set_size(backdrop, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE); /* Tippen während der Animation geht nicht durch */

    /* Lichtblitz aus der Mitte */
    lv_obj_t * flash = ui_plain_obj(backdrop);
    lv_obj_set_style_radius(flash, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(flash, COL_GREEN, 0);
    lv_obj_set_style_bg_opa(flash, LV_OPA_COVER, 0);
    anim(flash, set_flash_size, 6, LOGO_SIZE, POP_MS, 0, lv_anim_path_ease_out);
    anim(flash, set_opa, 230, LV_OPA_TRANSP, POP_MS, 0, lv_anim_path_ease_out);

    lv_obj_t * logo = ui_plain_obj(backdrop);
    lv_obj_set_size(logo, LOGO_SIZE, LOGO_SIZE);
    lv_obj_center(logo);
    lv_obj_set_style_transform_pivot_x(logo, LOGO_SIZE / 2, 0);
    lv_obj_set_style_transform_pivot_y(logo, LOGO_SIZE / 2, 0);
    part(logo, &logo_base, 0, 0);

    lv_obj_t * bar1 = part(logo, &logo_bar1, LOGO_BAR1_X, LOGO_BAR1_Y);
    lv_obj_t * bar2 = part(logo, &logo_bar2, LOGO_BAR2_X, LOGO_BAR2_Y);
    lv_obj_t * bar3 = part(logo, &logo_bar3, LOGO_BAR3_X, LOGO_BAR3_Y);
    lv_image_set_pivot(bar1, LOGO_BAR1_PIVOT_X, LOGO_BAR1_PIVOT_Y);
    lv_image_set_pivot(bar2, LOGO_BAR2_PIVOT_X, LOGO_BAR2_PIVOT_Y);
    lv_image_set_pivot(bar3, LOGO_BAR3_PIVOT_X, LOGO_BAR3_PIVOT_Y);
    bounce(bar1, 141, 550, 0);
    bounce(bar2, 179, 450, 150);
    bounce(bar3, 128, 650, 300);

    lv_obj_t * dial = part(logo, &logo_dial, LOGO_DIAL_X, LOGO_DIAL_Y);
    lv_image_set_pivot(dial, logo_dial.header.w / 2, logo_dial.header.h / 2);
    lv_anim_t spin;
    lv_anim_init(&spin);
    lv_anim_set_var(&spin, dial);
    lv_anim_set_exec_cb(&spin, set_rotation);
    lv_anim_set_values(&spin, 0, 3600);
    lv_anim_set_duration(&spin, SPIN_MS);
    lv_anim_set_repeat_count(&spin, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&spin);

    /* Aufspringen: klein und unsichtbar → etwas zu groß → Endgröße */
    set_scale(logo, 102);
    set_opa(logo, LV_OPA_TRANSP);
    anim(logo, set_opa, LV_OPA_TRANSP, LV_OPA_COVER, POP_MS * 6 / 10, 0, lv_anim_path_ease_out);
    anim(logo, set_scale, 102, 256, POP_MS, 0, lv_anim_path_overshoot);

    lv_timer_t * t = lv_timer_create(fade_out, POP_MS + HOLD_MS, NULL);
    lv_timer_set_repeat_count(t, 1);
}
