/**
 * USB transport: the ESP32-S3's built-in USB serial (the board's USB-C socket labelled "USB").
 * The PC sees a serial port (VID 303A, PID 1001); the PC app finds it by itself.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/** Send function for link_init(). */
void usb_link_send(const uint8_t * data, size_t len);

/** Start the driver and receive task (after link_init). */
void usb_link_start(void);
