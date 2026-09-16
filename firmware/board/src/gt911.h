/**
 * GT911 capacitive touch controller, first touch point only.
 * Reset and address selection (INT low during reset = 0x5D) happen before gt911_init().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"

/** Finds the controller at 0x5D or 0x14; *address receives the one that answered. */
esp_err_t gt911_init(i2c_master_bus_handle_t bus, uint8_t * address);

/** Latest touch; keeps the previous state when the controller has nothing new. */
esp_err_t gt911_read(uint16_t * x, uint16_t * y, bool * pressed);
