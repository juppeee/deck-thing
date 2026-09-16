/**
 * Windows-Simulator: öffnet die Geräte-Oberfläche in einem 800×480-Fenster.
 * Maus = Touch, Mausrad = Knauf drehen, mittlere Maustaste = Knauf drücken.
 */
#include <windows.h>

#include "bridge_client.h"
#include "lvgl.h"
#include "ui.h"

int main(void)
{
    lv_init();

    lv_display_t * display = lv_windows_create_display(L"Deck Thing Simulator", 800, 480, 100, false, true);
    if(display == NULL) return -1;

    lv_windows_acquire_pointer_indev(display);
    lv_indev_t * knob = lv_windows_acquire_encoder_indev(display);

    ui_init(display, knob);
    bridge_client_start(); /* verbindet sich mit bridge.py, sobald sie läuft */

    for(;;) {
        uint32_t wait_ms = lv_timer_handler();
        if(wait_ms < 1) wait_ms = 1;
        if(wait_ms > 10) wait_ms = 10;
        Sleep(wait_ms);
    }
}
