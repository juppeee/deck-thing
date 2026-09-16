/**
 * Waveshare ESP32-S3-Touch-LCD-4.3 – pins and panel timings.
 *
 * Sources, cross-checked: Waveshare documentation, Espressif's ESP32_Display_Panel board file
 * (BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_4_3.h) and the board schematic.
 */
#pragma once

/* I2C: GT911 touch, CH422G IO expander and the external 3.3 V sensor port share one bus */
#define BOARD_I2C_SDA       8
#define BOARD_I2C_SCL       9
#define BOARD_TOUCH_INT     4

/* RGB panel (ST7262), RGB565 */
#define LCD_H_RES           800
#define LCD_V_RES           480
#define LCD_PCLK_HZ         (16 * 1000 * 1000)
#define LCD_HSYNC_PULSE     4
#define LCD_HSYNC_BACK      8
#define LCD_HSYNC_FRONT     8
#define LCD_VSYNC_PULSE     4
#define LCD_VSYNC_BACK      8
#define LCD_VSYNC_FRONT     8

#define LCD_GPIO_HSYNC      46
#define LCD_GPIO_VSYNC      3
#define LCD_GPIO_DE         5
#define LCD_GPIO_PCLK       7
/* DATA0 … DATA15 = B0–B4, G0–G5, R0–R4 */
#define LCD_GPIO_DATA { 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40 }

/* CH422G outputs (bit number = schematic EXIO number) */
#define EXIO_TP_RST         1   /* GT911 reset, active low */
#define EXIO_LCD_BL         2   /* backlight driver enable (on/off only) */
#define EXIO_LCD_RST        3   /* panel reset, active low */
#define EXIO_SD_CS          4   /* microSD chip select, active low */
#define EXIO_USB_SEL        5   /* low = native USB on the USB-C port */
