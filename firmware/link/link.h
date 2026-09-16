/**
 * Verbindung zur PC-App, unabhängig vom Transportweg (TCP im Simulator, USB auf dem Board).
 *
 * Der Transport liefert empfangene Bytes aus seinem eigenen Thread (link_feed); ein LVGL-Timer
 * übernimmt fertige Rahmen in die Oberfläche, weil LVGL nicht threadsicher ist.
 *   Rahmen: A5 5A | Typ (1 Byte) | Länge (4 Byte, little endian) | Inhalt   – siehe docs/protocol.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Schickt rohe Bytes zur PC-App. Wird unter einer Sperre aufgerufen, also nie gleichzeitig. */
typedef void (*link_send_fn)(const uint8_t * data, size_t len);

/** Uhrzeit vom PC (Unix-Sekunden, Abstand zu UTC in Sekunden) – für Geräte ohne eigene Uhr, sonst NULL. */
typedef void (*link_time_fn)(int64_t unix_seconds, int32_t utc_offset_seconds);

/** Einmal aus dem LVGL-Thread aufrufen, bevor der Transport startet. hello_json wird kopiert. */
void link_init(link_send_fn send, link_time_fn on_time, const char * hello_json);

/** Empfangene Bytes, in beliebigen Stücken (Transport-Thread). */
void link_feed(const uint8_t * data, size_t len);

/** Verbindung steht bzw. ist weg (Transport-Thread). Verwirft einen halb gelesenen Rahmen. */
void link_set_connected(bool connected);

/** Meldet Gerät, Firmware und Fähigkeiten an die PC-App. */
void link_send_hello(void);
