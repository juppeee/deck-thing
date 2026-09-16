#include "ch422g.h"

#define CH422G_ADDR_MODE    0x24    /* bit 0: IO0–IO7 are outputs */
#define CH422G_ADDR_OUTPUT  0x38
#define I2C_TIMEOUT_MS      50

static i2c_master_dev_handle_t mode_dev;
static i2c_master_dev_handle_t output_dev;
static uint8_t shadow;

static esp_err_t add_device(i2c_master_bus_handle_t bus, uint8_t address, i2c_master_dev_handle_t * dev)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &cfg, dev);
}

esp_err_t ch422g_init(i2c_master_bus_handle_t bus)
{
    esp_err_t err = add_device(bus, CH422G_ADDR_MODE, &mode_dev);
    if(err == ESP_OK) err = add_device(bus, CH422G_ADDR_OUTPUT, &output_dev);
    if(err != ESP_OK) return err;

    const uint8_t mode = 0x01;
    err = i2c_master_transmit(mode_dev, &mode, 1, I2C_TIMEOUT_MS);
    if(err != ESP_OK) return err;

    /* everything low: touch and panel in reset, backlight off, USB-C port on native USB */
    shadow = 0;
    return i2c_master_transmit(output_dev, &shadow, 1, I2C_TIMEOUT_MS);
}

esp_err_t ch422g_restore(void)
{
    const uint8_t mode = 0x01;
    esp_err_t err = i2c_master_transmit(mode_dev, &mode, 1, I2C_TIMEOUT_MS);
    if(err != ESP_OK) return err;
    return i2c_master_transmit(output_dev, &shadow, 1, I2C_TIMEOUT_MS);
}

bool ch422g_shadows(uint8_t address)
{
    return (address >= 0x20 && address <= 0x27) || (address >= 0x30 && address <= 0x3F);
}

esp_err_t ch422g_set(uint8_t exio, bool level)
{
    if(level) shadow |= (uint8_t)(1u << exio);
    else shadow &= (uint8_t)~(1u << exio);
    return i2c_master_transmit(output_dev, &shadow, 1, I2C_TIMEOUT_MS);
}
