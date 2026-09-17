# Board firmware: its own hardware layer plus the shared parts from firmware/
set(FW ${CMAKE_CURRENT_LIST_DIR}/../..)
file(GLOB UI_SOURCES ${FW}/ui/*.c ${FW}/ui/fonts/*.c ${FW}/ui/images/*.c)

idf_component_register(
    SRCS main.c ch422g.c gt911.c usb_link.c
         ${FW}/link/link.c
         ${UI_SOURCES}
         ${FW}/third_party/cJSON/cJSON.c
    INCLUDE_DIRS "." ${FW}/ui ${FW}/link ${FW}/third_party/cJSON
    REQUIRES lvgl esp_lcd esp_driver_i2c esp_driver_gpio esp_driver_usb_serial_jtag esp_timer
)
