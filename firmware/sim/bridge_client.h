/**
 * Simulator ↔ Brücke über TCP (127.0.0.1:8766).
 * Gleiches Rahmenformat wie später USB/Bluetooth auf dem Gerät:
 *   A5 5A | Typ (1 Byte) | Länge (4 Byte, little endian) | Inhalt
 */
#pragma once

void bridge_client_start(void);
