idf_component_register(
  SRCS
    "drivers/ir_remote.cpp"
    "app/smart_light_automation.cpp"
    "app/smart_light_controller.cpp"
    "app_main.cpp"
    "console/smart_light_commands.cpp"
    "http/smart_light_web.cpp"
    "settings/smart_light_settings.cpp"
  INCLUDE_DIRS "."
  PRIV_REQUIRES device_common esp_matter driver esp_adc esp_driver_usb_serial_jtag
                esp_app_format esp_http_server esp_netif esp_system esp_timer
                esp_wifi app_update freertos led_strip log mdns nvs_flash json
)
target_compile_options(${COMPONENT_LIB} PRIVATE
  -fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=
  -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=
)
include("${CMAKE_CURRENT_LIST_DIR}/../components/device_common/cmake/web_assets.cmake")
device_common_web_assets(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../web/index.html")
