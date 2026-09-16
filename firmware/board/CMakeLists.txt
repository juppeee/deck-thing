cmake_minimum_required(VERSION 3.16)

# LVGL (git submodule shared with the simulator) is wrapped by components/lvgl instead of being
# added as a component directory: PlatformIO puts all object files of a component into one folder,
# and LVGL's own component file would compile files with the same name twice.
# Never set EXTRA_COMPONENT_DIRS here - PlatformIO passes src/ in it.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# LVGL includes "lv_conf.h" (LV_CONF_INCLUDE_SIMPLE); make ours visible to every component
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}/include" APPEND)

project(deck_thing_board)
