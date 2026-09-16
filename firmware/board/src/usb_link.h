/**
 * Transportweg USB: der eingebaute USB-Serial-Anschluss des ESP32-S3 (USB-C-Buchse „USB“ am Board).
 * Am PC erscheint ein serieller Port (VID 303A, PID 1001); die PC-App findet ihn von selbst.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/** Sendefunktion für link_init(). */
void usb_link_send(const uint8_t * data, size_t len);

/** Treiber und Empfangs-Task starten (nach link_init). */
void usb_link_start(void);
