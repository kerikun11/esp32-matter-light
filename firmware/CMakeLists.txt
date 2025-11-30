# CMakeLists.txt for the esp-idf project
cmake_minimum_required(VERSION 3.16)

# ESP-IDF config
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project config
project(esp32-matter-light)

# Compile options
idf_build_set_property(CXX_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++2a" APPEND) # for use of typeof
idf_build_set_property(CXX_COMPILE_OPTIONS "-DCHIP_HAVE_CONFIG_H" APPEND)
idf_build_set_property(CXX_COMPILE_OPTIONS "-DARDUINO_USB_MODE=1" APPEND)
idf_build_set_property(CXX_COMPILE_OPTIONS "-DARDUINO_USB_CDC_ON_BOOT=1" APPEND)
