#include "gt911.h"

#include "esp_log.h"

#define REG_PRODUCT_ID  0x8140  /* 4 ASCII characters, "911" */
#define REG_X_RES       0x8146
#define REG_STATUS      0x814E  /* bit 7: new data, low nibble: number of points */
#define REG_POINT1_X    0x8150  /* x low, x high, y low, y high */
#define I2C_TIMEOUT_MS  50

static const char * TAG = "gt911";
static i2c_master_dev_handle_t dev;
static uint16_t last_x, last_y;
static bool last_pressed;

static esp_err_t read_reg(uint16_t reg, uint8_t * buf, size_t len)
{
    const uint8_t addr[2] = { reg >> 8, reg & 0xFF };
    return i2c_master_transmit_receive(dev, addr, sizeof(addr), buf, len, I2C_TIMEOUT_MS);
}

static esp_err_t write_reg8(uint16_t reg, uint8_t value)
{
    const uint8_t buf[3] = { reg >> 8, reg & 0xFF, value };
    return i2c_master_transmit(dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

esp_err_t gt911_init(i2c_master_bus_handle_t bus, uint8_t * address)
{
    const uint8_t candidates[] = { 0x5D, 0x14 };
    for(size_t i = 0; i < sizeof(candidates); i++) {
        if(i2c_master_probe(bus, candidates[i], I2C_TIMEOUT_MS) != ESP_OK) continue;
        const i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = candidates[i],
            .scl_speed_hz = 400000,
        };
        esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev);
        if(err != ESP_OK) return err;
        *address = candidates[i];

        uint8_t id[5] = { 0 };
        uint8_t res[4] = { 0 };
        read_reg(REG_PRODUCT_ID, id, 4);
        read_reg(REG_X_RES, res, 4);
        ESP_LOGI(TAG, "GT%s at 0x%02X, configured for %u x %u", (char *)id, candidates[i],
                 res[0] | (res[1] << 8), res[2] | (res[3] << 8));
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t gt911_read(uint16_t * x, uint16_t * y, bool * pressed)
{
    uint8_t status;
    esp_err_t err = read_reg(REG_STATUS, &status, 1);
    if(err != ESP_OK) return err;

    if(status & 0x80) {
        uint8_t points = status & 0x0F;
        if(points > 0 && points <= 5) {
            uint8_t p[4];
            err = read_reg(REG_POINT1_X, p, sizeof(p));
            if(err == ESP_OK) {
                last_x = p[0] | (p[1] << 8);
                last_y = p[2] | (p[3] << 8);
            }
        }
        last_pressed = points > 0;
        write_reg8(REG_STATUS, 0); /* acknowledge, otherwise no new data follows */
    }
    *x = last_x;
    *y = last_y;
    *pressed = last_pressed;
    return ESP_OK;
}
