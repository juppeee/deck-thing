# Windows simulator for the device interface.
# The same UI code (../ui) runs on the ESP32-S3; here LVGL's own Windows driver
# opens an 800×480 window (mouse = touch, mouse wheel = knob).
#
# Build:  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
#         cmake --build build --config Release
cmake_minimum_required(VERSION 3.16)
project(deck_thing_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(FW_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

# LVGL with its own configuration, without examples, demos and ThorVG
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${FW_DIR}/third_party/lvgl ${CMAKE_BINARY_DIR}/lvgl)

file(GLOB UI_SOURCES ${FW_DIR}/ui/*.c ${FW_DIR}/ui/fonts/*.c ${FW_DIR}/ui/images/*.c)

add_executable(deck_thing_sim main.c bridge_client.c ${FW_DIR}/link/link.c ${UI_SOURCES} ${FW_DIR}/third_party/cJSON/cJSON.c)
target_include_directories(deck_thing_sim PRIVATE ${FW_DIR}/ui ${FW_DIR}/link ${FW_DIR}/third_party/cJSON ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(deck_thing_sim PRIVATE lvgl::lvgl ws2_32)

if(MSVC)
  # sources are UTF-8 (umlauts in texts)
  target_compile_options(deck_thing_sim PRIVATE /utf-8)
  target_compile_options(lvgl PRIVATE /utf-8)
  target_compile_definitions(deck_thing_sim PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
  set_property(TARGET deck_thing_sim PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
endif()
