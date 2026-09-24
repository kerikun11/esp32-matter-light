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

# esp_matter's vendored connectedhomeip fails to build its ClosureControl cluster
# with GCC 14 + C++20 (ambiguous operator== for std::optional<T>). Patch it via a
# vendored copy of arduino-esp32's fix, scoped to just the esp_matter component.
# See: https://github.com/espressif/arduino-esp32/issues/12851
idf_component_get_property(esp_matter_lib espressif__esp_matter COMPONENT_LIB)
target_compile_options(${esp_matter_lib} PRIVATE
  "-include${CMAKE_CURRENT_SOURCE_DIR}/config/matter_closure_patch.h")
