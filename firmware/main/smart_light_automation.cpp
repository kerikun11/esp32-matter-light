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
  state.night_state = event.night_state;

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
    case MatterLight::EventType::NightOn:
      LOGW("[Event] Night ON");
      break;
    case MatterLight::EventType::NightOff:
      LOGW("[Event] Night OFF");
      break;
  }
}

void SmartLightAutomation::applyButtonPress(bool pressed,
                                            SmartLightRuntimeState& state) {
  if (!pressed) return;
  state.light_state = !state.light_state;
  LOGW("[LightState] %d (Button)", state.light_state);
}

SmartLightStateDelta SmartLightAutomation::computeStateDelta(
    const SmartLightRuntimeState& previous_state,
    const SmartLightRuntimeState& state) {
  SmartLightStateDelta delta;
  delta.light_state_changed = previous_state.light_state != state.light_state;
  delta.switch_state_changed = previous_state.switch_state != state.switch_state;
  delta.night_state_changed = previous_state.night_state != state.night_state;
  return delta;
}

void SmartLightAutomation::applyDerivedRules(
    const SmartLightRuntimeState& previous_state,
    SmartLightRuntimeState& state) {
  applyLightNightInterlock_(computeStateDelta(previous_state, state), state);
  applyNightSwitchInterlock_(state);
  if (!state.night_state) {
    syncSwitchStateFromLight_(computeStateDelta(previous_state, state), state);
  }
  applyOccupancyRules(state);
  applyNightSwitchInterlock_(state);
}

void SmartLightAutomation::applyLightNightInterlock_(
    const SmartLightStateDelta& delta, SmartLightRuntimeState& state) {
  if (delta.light_state_changed && state.night_state) {
    state.night_state = false;
    LOGW("[NightState] %d (LightState)", state.night_state);
  }
  if (delta.night_state_changed && state.light_state) {
    state.light_state = false;
    LOGW("[LightState] %d (NightState)", state.light_state);
  }
}

void SmartLightAutomation::applyNightSwitchInterlock_(
    SmartLightRuntimeState& state) {
  if (!state.night_state || !state.switch_state) return;

  state.switch_state = false;
  LOGW("[SwitchState] %d (NightState)", state.switch_state);
}

void SmartLightAutomation::syncSwitchStateFromLight_(
    const SmartLightStateDelta& delta, SmartLightRuntimeState& state) {
  if (!delta.light_state_changed) return;

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
