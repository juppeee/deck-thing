# LVGL from the shared submodule, registered as an ESP-IDF component.
#
# Why not LVGL's own component file: it compiles every source, and two of them share a name
# (debugging/vg_lite_tvg/vg_lite_matrix.c and libs/vg_lite_driver/VGLite/vg_lite_matrix.c).
# PlatformIO puts all objects of a component into one folder and refuses to build that.
# Both folders are for VGLite GPUs, which the ESP32-S3 doesn't have.
set(LVGL_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../third_party/lvgl)

file(GLOB_RECURSE LVGL_SOURCES ${LVGL_ROOT}/src/*.c ${LVGL_ROOT}/src/*.cpp)
list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/src/(debugging/vg_lite_tvg|libs/vg_lite_driver)/.*")

idf_component_register(
    SRCS ${LVGL_SOURCES}
    INCLUDE_DIRS ${LVGL_ROOT} ${LVGL_ROOT}/src
    PRIV_REQUIRES esp_timer log freertos
)

# osal/lv_freertos.c includes "atomic.h" without the freertos/ prefix; ESP-IDF only exposes
# that folder privately to its own FreeRTOS component
idf_build_get_property(idf_path IDF_PATH)
target_include_directories(${COMPONENT_LIB} PRIVATE ${idf_path}/components/freertos/FreeRTOS-Kernel/include/freertos)

# lv_conf.h comes from the project's include/ folder (see the project CMakeLists.txt)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE)
# LVGL is third-party code: its warnings must not stop the build
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
