idf_component_register(
  SRCS "src/http/device_http.cpp" "src/system/runtime.cpp" "src/matter/matter_service.cpp" "src/ota/ota_service.cpp" "src/network/network_health.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_matter esp_http_server esp_netif esp_event freertos nvs_flash
           esp_timer driver led_strip
  PRIV_REQUIRES app_update esp_app_format esp_system esp_wifi mdns json
)
