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

# esp_matter's vendored connectedhomeip fails to build its ClosureControl cluster
# with GCC 14 + C++20 (ambiguous operator== for std::optional<T>). arduino-esp32
# carries a fix for this (matter_closure_patch.h), but only injects it into its own
# Matter.h, not into a direct esp_matter build. Reuse that same file, scoped to the
# esp_matter component, instead of keeping a local copy.
# See: https://github.com/espressif/arduino-esp32/issues/12851
idf_component_get_property(esp_matter_lib espressif__esp_matter COMPONENT_LIB)
idf_component_get_property(arduino_esp32_dir espressif__arduino-esp32 COMPONENT_DIR)
target_compile_options(${esp_matter_lib} PRIVATE
  "-include${arduino_esp32_dir}/libraries/Matter/src/matter_closure_patch.h")
