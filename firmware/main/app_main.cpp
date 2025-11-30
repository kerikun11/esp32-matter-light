/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#if 1
#include "smart_light_controller.h"

SmartLightController app_;

void setup() {
  Serial.begin(CONFIG_MONITOR_BAUD);
  app_.begin();

  /* set log level */
  esp_log_level_set("esp_matter_attribute", ESP_LOG_WARN);
  esp_log_level_set("esp_matter_command", ESP_LOG_WARN);
  esp_log_level_set("ROUTE_HOOK", ESP_LOG_WARN);
}

void loop() {
  app_.handle();
  yield();
}

#else  // Matter Light Example

#include <Arduino.h>

#include "app_config.h"
#include "app_log.h"
#include "matter_light.h"
#include "rgb_led.h"

MatterLight matter_light_;
RgbLed led_(CONFIG_APP_PIN_RGB_LED);

void setup() {
  Serial.begin(CONFIG_MONITOR_BAUD);
  matter_light_.begin(true, true);
  matter_light_.printOnboarding();
}

void loop() {
  /* status */
  if (!matter_light_.isCommissioned()) {
    led_.setBackground(RgbLed::Color::Magenta);
  } else if (!matter_light_.isConnected()) {
    led_.setBackground(RgbLed::Color::Red);
  } else {
    led_.setBackground(RgbLed::Color::White);
  }

  /* event */
  MatterLight::Event event;
  if (matter_light_.getEvent(event, 0)) {
    led_.blinkOnce(RgbLed::Color::Blue);
    switch (event.type) {
      case MatterLight::EventType::LightOn:
        LOGI("[Event] Light ON");
        break;
      case MatterLight::EventType::LightOff:
        LOGI("[Event] Light OFF");
        break;
      case MatterLight::EventType::SwitchOn:
        LOGI("[Event] Switch ON");
        break;
      case MatterLight::EventType::SwitchOff:
        LOGI("[Event] Switch OFF");
        break;
    }
    LOGW("[Event] Light %s, Switch %s", event.light_state ? "ON" : "OFF",
         event.switch_state ? "ON" : "OFF");
  }

  led_.update();
  yield();
}
#endif
