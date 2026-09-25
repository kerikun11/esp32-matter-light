#include "device_common/system/runtime.h"

#include <esp_log.h>
#include <nvs_flash.h>

#include <cstdio>
namespace device_common {
void initializeRuntime() {
  setvbuf(stdout, nullptr, _IOLBF, 1024);
  auto err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  esp_log_level_set("esp_matter_attribute", ESP_LOG_WARN);
  esp_log_level_set("esp_matter_command", ESP_LOG_WARN);
  esp_log_level_set("ROUTE_HOOK", ESP_LOG_WARN);
}
}  // namespace device_common
