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
)

# set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
# target_compile_options(${COMPONENT_LIB} PUBLIC "-DCHIP_HAVE_CONFIG_H")
# target_compile_definitions(${COMPONENT_LIB} PUBLIC CHIP_HAVE_CONFIG_H=1)

target_compile_options(${COMPONENT_LIB} PRIVATE
  -fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=
  -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=
)
