/**
 * Link to the PC app, independent of the transport (TCP in the simulator, USB on the board).
 *
 * The transport delivers received bytes from its own thread (link_feed); an LVGL timer
 * applies complete frames to the interface, because LVGL is not thread-safe.
 *   Frame: A5 5A | type (1 byte) | length (4 bytes, little endian) | payload   – see docs/protocol.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Sends raw bytes to the PC app. Called under a lock, so never concurrently. */
typedef void (*link_send_fn)(const uint8_t * data, size_t len);

/** Time from the PC (Unix seconds, offset to UTC in seconds) – for devices without a clock, else NULL. */
typedef void (*link_time_fn)(int64_t unix_seconds, int32_t utc_offset_seconds);

/** Call once from the LVGL thread before the transport starts. hello_json is copied. */
void link_init(link_send_fn send, link_time_fn on_time, const char * hello_json);

/** Received bytes, in pieces of any size (transport thread). */
void link_feed(const uint8_t * data, size_t len);

/** Connection is up or gone (transport thread). Drops a half-read frame. */
void link_set_connected(bool connected);

/** Reports device, firmware and capabilities to the PC app. */
void link_send_hello(void);
