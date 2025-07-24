cmake_minimum_required(VERSION 3.16)

# ESP-IDF config
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project config
project(esp32_matter_device)

# idf_build_set_property(CXX_COMPILE_OPTIONS
# "-std=gnu++2a;-Os;-DCHIP_HAVE_CONFIG_H" APPEND)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=c++2a" APPEND)
idf_build_set_property(CXX_COMPILE_OPTIONS "-DCHIP_HAVE_CONFIG_H" APPEND)
