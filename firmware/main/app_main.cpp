/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <cstdio>

#include "ota_service.h"
#include "smart_light_controller.h"

namespace {

void initNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

}  // namespace

SmartLightController app;

extern "C" void app_main() {
  // app_log.h writes with plain fprintf(stdout, ...); without this, stdout
  // is fully buffered here (not line-buffered), so log lines can sit
  // unflushed for a long time -- observed as [IR-Tx]/etc. lines from LOGx()
  // going missing from a short serial-monitor capture even though the code
  // that logs them did run.
  setvbuf(stdout, nullptr, _IOLBF, 1024);

  initNvs();
  // As early as possible after a fresh OTA update, tell the bootloader the
  // new image booted successfully so it won't roll back to the previous one.
  confirmOtaBootIfPending();

  /* set log level */
  esp_log_level_set("esp_matter_attribute", ESP_LOG_WARN);
  esp_log_level_set("esp_matter_command", ESP_LOG_WARN);
  esp_log_level_set("ROUTE_HOOK", ESP_LOG_WARN);

  app.begin();
  while (true) {
    app.handle();
    vTaskDelay(1);
  }
}
