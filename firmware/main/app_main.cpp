#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app/smart_light_controller.h"
#include "device_common/ota/ota_service.h"
#include "device_common/system/runtime.h"

SmartLightController app;
extern "C" void app_main() {
  device_common::initializeRuntime();
  ESP_ERROR_CHECK(app.begin() ? ESP_OK : ESP_FAIL);
  // Confirm only after the application has initialized its services.
  confirmOtaBootIfPending();
  while (true) {
    app.handle();
    vTaskDelay(1);
  }
}
