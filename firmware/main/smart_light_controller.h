/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "app_config.h"
#include "brightness_sensor.h"
#include "button.h"
#include "command_parser.h"
#include "ir_remote.h"
#include "matter_light.h"
#include "motion_sensor.h"
#include "rgb_led.h"
#include "smart_light_automation.h"
#include "smart_light_commands.h"
#include "smart_light_settings.h"

class SmartLightController {
 public:
  SmartLightController();

  void begin();
  void handle();

 private:
  Button btn_{CONFIG_APP_PIN_BUTTON};
  RgbLed led_{CONFIG_APP_PIN_RGB_LED};
  MotionSensor motion_sensor_{CONFIG_APP_PIN_MOTION_SENSOR};
  BrightnessSensor brightness_sensor_{CONFIG_APP_PIN_LIGHT_SENSOR};
  IRRemote ir_remote_;
  CommandParser command_parser_{Serial};
  SmartLightSettingsStore settings_store_;
  SmartLightSettings settings_;
  SmartLightCommandHandler command_handler_;
  MatterLight matter_light_;

  bool last_light_state_ = false;
  bool last_switch_state_ = false;
  bool last_occupancy_state_ = false;

  void setupOta();
  void applyMatterEvents(SmartLightRuntimeState& state);
  void applyIrInput(SmartLightRuntimeState& state);
  void commitSwitchState(const SmartLightRuntimeState& state);
  void commitLightState(const SmartLightRuntimeState& state);
  void updateOccupancyLog(bool occupancy_state);
  void updateStatusLed(const SmartLightRuntimeState& state);
  void handleDecommission();
};
