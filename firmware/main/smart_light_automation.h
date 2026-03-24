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
  bool night_state = false;
  bool occupancy_state = false;
  bool is_bright = false;
  int seconds_since_last_motion = 0;
  int light_off_timeout_seconds = 0;
  bool ambient_light_mode_enabled = true;
};

struct SmartLightStateDelta {
  bool light_state_changed = false;
  bool switch_state_changed = false;
  bool night_state_changed = false;
};

class SmartLightAutomation {
 public:
  static constexpr int kOccupancyTimeoutSeconds = 3;

  static void applyMatterEvent(const MatterLight::Event& event,
                               SmartLightRuntimeState& state,
                               bool& force_light_resync);
  static void applyButtonPress(bool pressed, SmartLightRuntimeState& state);
  static SmartLightStateDelta computeStateDelta(
      const SmartLightRuntimeState& previous_state,
      const SmartLightRuntimeState& state);
  static void applyDerivedRules(const SmartLightRuntimeState& previous_state,
                                SmartLightRuntimeState& state);
  static void applyOccupancyRules(SmartLightRuntimeState& state);
  static RgbLed::Color selectStatusColor(const SmartLightRuntimeState& state,
                                         bool commissioned, bool connected);

 private:
  static void applyLightNightInterlock_(const SmartLightStateDelta& delta,
                                        SmartLightRuntimeState& state);
  static void applyNightSwitchInterlock_(SmartLightRuntimeState& state);
  static void syncSwitchStateFromLight_(const SmartLightStateDelta& delta,
                                        SmartLightRuntimeState& state);
};
