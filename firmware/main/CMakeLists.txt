# CMakeLists.txt for the esp-idf main component
idf_component_register(SRC_DIRS "." INCLUDE_DIRS "."
  PRIV_REQUIRES
    driver
    esp_adc
    esp_app_format
    esp_driver_usb_serial_jtag
    esp_http_server
    esp_netif
    esp_system
    esp_timer
    esp_wifi
    app_update
    freertos
    led_strip
    log
    mdns
    nvs_flash
    json
)

# set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
# target_compile_options(${COMPONENT_LIB} PUBLIC "-DCHIP_HAVE_CONFIG_H")
# target_compile_definitions(${COMPONENT_LIB} PUBLIC CHIP_HAVE_CONFIG_H=1)

target_compile_options(${COMPONENT_LIB} PRIVATE
  -fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=
  -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=
)

# Generate compressed assets only when their source or the builder changes.
set(WEB_ASSET_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/web_assets.h")
add_custom_command(OUTPUT "${WEB_ASSET_HEADER}"
  COMMAND ${PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/web/build_web.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html" "${WEB_ASSET_HEADER}"
  DEPENDS "web/index.html" "../tools/web/build_web.py" "../tools/web/requirements.txt"
  VERBATIM)
add_custom_target(smart_light_web_assets DEPENDS "${WEB_ASSET_HEADER}")
add_dependencies(${COMPONENT_LIB} smart_light_web_assets)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
