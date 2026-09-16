/**
 * CH422G IO expander. It has no register pointer: every register is its own I2C address
 * (0x24 mode, 0x38 outputs), and a write is a single byte. The output register cannot be
 * read back, so the driver keeps a shadow copy.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"

esp_err_t ch422g_init(i2c_master_bus_handle_t bus);
esp_err_t ch422g_set(uint8_t exio, bool level);

/** Writes mode and outputs again, e.g. after something else was sent to one of its addresses. */
esp_err_t ch422g_restore(void);

/**
 * True for addresses the CH422G answers on the same bus (0x20–0x27 and 0x30–0x3F, seen in the
 * I2C scan). It reads the address itself as a command, so talking to another chip there can
 * switch the backlight or reset the panel – never send anything to these addresses.
 */
bool ch422g_shadows(uint8_t address);
