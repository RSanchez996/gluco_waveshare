cmake_minimum_required(VERSION 3.16)
# Apply configuration to the application AND all bundled source components.
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
add_compile_definitions(ARDUINO_USB_MODE=1 ARDUINO_USB_CDC_ON_BOOT=1 LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE LV_KCONFIG_IGNORE)
set(ENV{IDF_COMPONENT_MANAGER} "0")
set(PROJECT_VER "2.0.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gluco_waveshare)
