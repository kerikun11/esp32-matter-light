/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <esp_err.h>

#include <cstdint>
#include <string>

#include "app/smart_light_automation.h"
#include "board/app_config.h"
#include "console/command_parser.h"
#include "console/smart_light_commands.h"
#include "device_common/drivers/button.h"
#include "device_common/drivers/rgb_led.h"
#include "device_common/network/network_health.h"
#include "device_common/system/lockable.h"
#include "drivers/brightness_sensor.h"
#include "drivers/ir_remote.h"
#include "drivers/motion_sensor.h"
#include "http/smart_light_web.h"
#include "matter/matter_light.h"
#include "settings/smart_light_settings.h"

class SmartLightController {
 public:
  SmartLightController();

  bool begin();
  void handle();

 private:
  enum class WebAction {
    kNone,
    kLight,
    kSwitch,
    kNight
  };

  Button btn_{CONFIG_APP_PIN_BUTTON};
  RgbLed led_{CONFIG_APP_PIN_RGB_LED};
  MotionSensor motion_sensor_{CONFIG_APP_PIN_MOTION_SENSOR};
  BrightnessSensor brightness_sensor_{CONFIG_APP_PIN_LIGHT_SENSOR};
  IRRemote ir_remote_;
  CommandParser command_parser_;
  SmartLightSettingsStore settings_store_;
  SmartLightSettings settings_;
  // Guards settings_ (and the IR data it carries), which is shared between
  // this class (main app task) and web_ (esp_http_server's worker task).
  Mutex settings_mutex_;
  MatterLight matter_light_;
  SmartLightCommandHandler command_handler_;
  SmartLightWeb web_;

  bool last_light_state_ = false;
  bool last_switch_state_ = false;
  bool last_night_state_ = false;
  bool last_occupancy_state_ = false;
  NetworkHealth network_;
  void syncHostnames();
  SmartLightRuntimeState buildRuntimeState() const;
  void commitOutputs(const SmartLightRuntimeState& state);
  void sendIrSignal(const IRRemote::IRData& data, const char* label);
  void applyMatterEvents(SmartLightRuntimeState& state);
  void applyIrInput(SmartLightRuntimeState& state);
  void commitSwitchState(const SmartLightRuntimeState& state);
  void commitNightState(const SmartLightRuntimeState& state,
                        bool suppress_off_signal);
  void commitLightState(const SmartLightRuntimeState& state,
                        bool suppress_off_signal);
  void updateOccupancyLog(bool occupancy_state);
  void updateStatusLed(const SmartLightRuntimeState& state);
  void reportWebAction(WebAction action, bool requested_value,
                       const SmartLightRuntimeState& directly_requested_state,
                       const SmartLightRuntimeState& final_state);
  void handleDecommission();
};
