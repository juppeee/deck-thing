#include "usb_link.h"

#include <stdbool.h>
#include <stdlib.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"

#define RX_CHUNK        2048
#define SILENCE_MS      4000   /* the PC app sends the playback state every 0.5 s – this much silence = disconnected */
#define HELLO_EVERY_MS  2000

static const char * TAG = "usb_link";

void usb_link_send(const uint8_t * data, size_t len)
{
    /* wait briefly instead of blocking: if nobody reads on the PC, interface and receive must not hang */
    usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(50));
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void usb_task(void * arg)
{
    (void)arg;
    uint8_t * buf = malloc(RX_CHUNK);
    bool connected = false;
    int64_t last_rx = 0;
    int64_t last_hello = 0;

    for(;;) {
        int n = usb_serial_jtag_read_bytes(buf, RX_CHUNK, pdMS_TO_TICKS(50));
        int64_t now = now_ms();
        if(n > 0) {
            last_rx = now;
            if(!connected) {
                connected = true;
                link_set_connected(true);
                link_send_hello();
                ESP_LOGI(TAG, "PC app connected");
            }
            link_feed(buf, (size_t)n);
        }
        else if(connected && now - last_rx > SILENCE_MS) {
            connected = false;
            link_set_connected(false);
            ESP_LOGI(TAG, "PC app disconnected");
        }
        /* while no app talks, say hello regularly – it may open the port later */
        if(!connected && now - last_hello > HELLO_EVERY_MS) {
            last_hello = now;
            link_send_hello();
        }
    }
}

void usb_link_start(void)
{
    /* Both ring buffers small enough to land in internal RAM (larger blocks go to PSRAM).
       The driver fills them from the interrupt – from PSRAM that was too slow and, together with the
       display interrupt, tripped the watchdog. Large frames still arrive, just in more pieces. */
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 4096,
        .rx_buffer_size = 8192,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    xTaskCreatePinnedToCore(usb_task, "usb_link", 8192, NULL, 6, NULL, 0); /* core 0, apart from the display */
}
