/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "smart_light_automation.h"

#include "app_log.h"

void SmartLightAutomation::applyMatterEvent(const MatterLight::Event& event,
                                            SmartLightRuntimeState& state,
                                            bool& force_light_resync) {
  state.light_state = event.light_state;
  state.switch_state = event.switch_state;

  switch (event.type) {
    case MatterLight::EventType::LightOn:
      LOGW("[Event] Light ON");
      force_light_resync = true;
      break;
    case MatterLight::EventType::LightOff:
      LOGW("[Event] Light OFF");
      force_light_resync = true;
      break;
    case MatterLight::EventType::SwitchOn:
      LOGW("[Event] Switch ON");
      break;
    case MatterLight::EventType::SwitchOff:
      LOGW("[Event] Switch OFF");
      break;
  }
}

void SmartLightAutomation::applyButtonPress(bool pressed,
                                            SmartLightRuntimeState& state) {
  if (!pressed) return;
  state.light_state = !state.light_state;
  LOGW("[LightState] %d (Button)", state.light_state);
}

void SmartLightAutomation::syncSwitchStateFromLight(
    bool light_state_changed, SmartLightRuntimeState& state) {
  if (!light_state_changed) return;

  if (state.occupancy_state) {
    state.switch_state = state.light_state;
    LOGW("[SwitchState] %d (LightState)", state.switch_state);
    return;
  }

  state.switch_state = !state.light_state;
  LOGW("[SwitchState] %d (LightState and No Motion)", state.switch_state);
}

void SmartLightAutomation::applyOccupancyRules(SmartLightRuntimeState& state) {
  if (!state.switch_state) return;

  if (!state.light_state && state.occupancy_state &&
      (!state.ambient_light_mode_enabled || !state.is_bright)) {
    state.light_state = true;
    LOGW("[LightState] %d (Occupancy Sensor)", state.light_state);
  }

  if (state.light_state &&
      state.seconds_since_last_motion > state.light_off_timeout_seconds) {
    state.light_state = false;
    LOGW("[LightState] %d (Occupancy Sensor)", state.light_state);
  }
}

RgbLed::Color SmartLightAutomation::selectStatusColor(
    const SmartLightRuntimeState& state, bool commissioned, bool connected) {
  if (!commissioned) return RgbLed::Color::Magenta;
  if (!connected) return RgbLed::Color::Red;
  if (!state.switch_state) return RgbLed::Color::Off;

  if (!state.light_state && state.ambient_light_mode_enabled && state.is_bright) {
    return state.occupancy_state ? RgbLed::Color::Cyan : RgbLed::Color::Yellow;
  }

  return state.occupancy_state ? RgbLed::Color::Blue : RgbLed::Color::White;
}
