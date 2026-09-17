/**
 * Deck Thing – firmware for the Waveshare ESP32-S3-Touch-LCD-4.3.
 *
 * Connects the shared interface (../../ui) to the hardware: RGB panel, GT911 touch,
 * CH422G for resets and backlight, and the link to the PC app over USB.
 * The knob is not connected yet (see README).
 *
 * Core split: display, LVGL and interface on core 1, USB receive on core 0.
 * The display interrupt copies picture lines 60 times per second; when it ran on the same core as
 * USB receive, the interrupt watchdog fired on the first cover and the board rebooted.
 *
 * Against tearing while swiping, LVGL draws straight into two frame buffers of the panel, and the
 * panel only switches to the finished one at the next frame. Modelled on Waveshare's lvgl_port
 * (12_lvgl_transplant, mode 3) and "avoid_tearing" from Espressif's esp_lvgl_port. With a bounce
 * buffer, wait for on_frame_buf_complete, not VSYNC – the bounce buffer reads ahead before VSYNC,
 * otherwise LVGL draws into the buffer that is on screen (flicker).
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board.h"
#include "ch422g.h"
#include "gt911.h"
#include "link.h"
#include "ui.h"
#include "usb_link.h"

#define GUI_CORE        1
#define GUI_STACK       (32 * 1024)
#define HELLO           "{\"proto\":1,\"fw\":\"board-0.1\",\"has_knob\":false,\"w\":800,\"h\":480}"

static const char * TAG = "deck_thing";

static i2c_master_bus_handle_t i2c_bus;
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t frame_sem;
static bool touch_ok;

/* ---------- Hardware ---------- */

static void i2c_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &i2c_bus));
}

/* GT911 picks its address from INT when reset is released: low = 0x5D */
static void touch_reset(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_INT,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(BOARD_TOUCH_INT, 0);
    ch422g_set(EXIO_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    ch422g_set(EXIO_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(60));
    io.mode = GPIO_MODE_INPUT;
    gpio_config(&io);
}

/* runs in the interrupt: the panel finished reading a frame and took over the new buffer */
static IRAM_ATTR bool on_frame_done(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_data_t * edata, void * ctx)
{
    (void)p;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(frame_sem, &woken);
    return woken == pdTRUE;
}

/* Must run on GUI_CORE: the panel interrupt is allocated on the core that creates the panel */
static void lcd_init(void)
{
    ch422g_set(EXIO_LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    ch422g_set(EXIO_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(20));

    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = LCD_HSYNC_PULSE,
            .hsync_back_porch = LCD_HSYNC_BACK,
            .hsync_front_porch = LCD_HSYNC_FRONT,
            .vsync_pulse_width = LCD_VSYNC_PULSE,
            .vsync_back_porch = LCD_VSYNC_BACK,
            .vsync_front_porch = LCD_VSYNC_FRONT,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        /* detour through a small buffer in internal RAM: the picture stays calm while PSRAM is busy */
        .bounce_buffer_size_px = LCD_H_RES * 10,
        .hsync_gpio_num = LCD_GPIO_HSYNC,
        .vsync_gpio_num = LCD_GPIO_VSYNC,
        .de_gpio_num = LCD_GPIO_DE,
        .pclk_gpio_num = LCD_GPIO_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = LCD_GPIO_DATA,
        .flags.fb_in_psram = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    frame_sem = xSemaphoreCreateBinary();
    const esp_lcd_rgb_panel_event_callbacks_t cbs = { .on_frame_buf_complete = on_frame_done };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, NULL));
}

/* take the time from the PC – the board has no clock of its own */
static void set_clock(int64_t unix_seconds, int32_t utc_offset_seconds)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    if(llabs((long long)now.tv_sec - unix_seconds) > 2) {
        const struct timeval tv = { .tv_sec = (time_t)unix_seconds };
        settimeofday(&tv, NULL);
    }
    static int32_t current_offset = -1;
    if(utc_offset_seconds != current_offset) {
        current_offset = utc_offset_seconds;
        /* POSIX time zone: sign inverted, "UTC-2:00" means two hours ahead of UTC */
        int32_t a = utc_offset_seconds < 0 ? -utc_offset_seconds : utc_offset_seconds;
        char tz[24];
        snprintf(tz, sizeof(tz), "UTC%c%d:%02d", utc_offset_seconds >= 0 ? '-' : '+', (int)(a / 3600), (int)(a % 3600 / 60));
        setenv("TZ", tz, 1);
        tzset();
    }
}

/* ---------- LVGL ---------- */

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* px is one of the panel buffers: draw_bitmap then copies nothing and just switches */
static void flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px)
{
    LV_UNUSED(area);
    if(lv_display_flush_is_last(disp)) {
        esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, px);
        xSemaphoreTake(frame_sem, 0); /* drop stale signals only after switching, or one arrives too early */
        xSemaphoreTake(frame_sem, pdMS_TO_TICKS(100)); /* keep drawing only once the panel has switched */
    }
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_UNUSED(indev);
    uint16_t x, y;
    bool pressed;
    if(!touch_ok || gt911_read(&x, &y, &pressed) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Sets up display, touch and interface on GUI_CORE and then stays there as the LVGL loop */
static void gui_task(void * arg)
{
    LV_UNUSED(arg);
    touch_reset();
    lcd_init();
    uint8_t touch_addr = 0;
    touch_ok = gt911_init(i2c_bus, &touch_addr) == ESP_OK;
    if(!touch_ok) ESP_LOGE(TAG, "GT911 not found");

    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t * disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    void * fb0, * fb1;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb0, &fb1));
    lv_display_set_buffers(disp, fb0, fb1, LCD_H_RES * LCD_V_RES * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_indev_t * touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read_cb);

    ui_init(disp, NULL);
    link_init(usb_link_send, set_clock, HELLO);
    usb_link_start();

    /* draw the first frame before the backlight comes on – no noise at power-up */
    lv_refr_now(disp);
    ch422g_set(EXIO_LCD_BL, true);
    ESP_LOGI(TAG, "running; touch %s, free PSRAM %u kB, internal RAM %u kB", touch_ok ? "ok" : "missing",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    for(;;) {
        uint32_t wait = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(LV_CLAMP(1, wait, 20)));
    }
}

void app_main(void)
{
    i2c_init();
    if(ch422g_init(i2c_bus) != ESP_OK) ESP_LOGE(TAG, "CH422G not answering - no backlight and resets");
    xTaskCreatePinnedToCore(gui_task, "gui", GUI_STACK, NULL, 5, NULL, GUI_CORE);
}
