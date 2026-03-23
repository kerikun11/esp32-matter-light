/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <Arduino.h>

#include "matter_light.h"
#include "rgb_led.h"

struct SmartLightRuntimeState {
  bool light_state = false;
  bool switch_state = false;
  bool occupancy_state = false;
  bool is_bright = false;
  int seconds_since_last_motion = 0;
  int light_off_timeout_seconds = 0;
  bool ambient_light_mode_enabled = true;
};

class SmartLightAutomation {
 public:
  static constexpr int kOccupancyTimeoutSeconds = 3;

  static void applyMatterEvent(const MatterLight::Event& event,
                               SmartLightRuntimeState& state,
                               bool& force_light_resync);
  static void applyButtonPress(bool pressed, SmartLightRuntimeState& state);
  static void syncSwitchStateFromLight(bool light_state_changed,
                                       SmartLightRuntimeState& state);
  static void applyOccupancyRules(SmartLightRuntimeState& state);
  static RgbLed::Color selectStatusColor(const SmartLightRuntimeState& state,
                                         bool commissioned, bool connected);
};
