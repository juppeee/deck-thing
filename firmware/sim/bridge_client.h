/**
 * Simulator ↔ bridge over TCP (127.0.0.1:8766).
 * Same frame format as USB on the device:
 *   A5 5A | type (1 byte) | length (4 bytes, little endian) | payload
 */
#pragma once

void bridge_client_start(void);
