/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "app/smart_light_controller.h"

#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mdns.h>

#include <algorithm>

#include "device_common/ota/ota_service.h"
#include "device_common/system/app_log.h"

SmartLightController::SmartLightController()
    : command_handler_(command_parser_, settings_, settings_store_, ir_remote_,
                       brightness_sensor_, matter_light_),
      web_(settings_, settings_mutex_, settings_store_, ir_remote_, led_) {}

bool SmartLightController::begin() {
  led_.setBackground(RgbLed::Color::kGreen);

  if (!settings_store_.begin()) {
    LOGE("[NVS] Failed to open settings");
    return false;
  }
  settings_ = settings_store_.load();

  ir_remote_.begin(CONFIG_APP_PIN_IR_TRANSMITTER, CONFIG_APP_PIN_IR_RECEIVER);

  last_light_state_ = false;
  last_switch_state_ = true;
  last_night_state_ = false;
  if (!matter_light_.begin(last_light_state_, last_switch_state_, last_night_state_,
                           settings_.night_light_feature_enabled)) return false;

  network_.begin(settings_.hostname);
  web_.begin();
  if (!web_.rawHandle()) return false;
  registerOtaHandlers(web_.rawHandle());
  return true;
}

void SmartLightController::handle() {
  network_.handle();

  btn_.update();
  led_.update();
  motion_sensor_.update();

  // Locked for the rest of this function: settings_ (read/written below,
  // directly and via syncHostnames_()/command_handler_ and applyIrInput()/
  // commitOutputs_()) is shared with web_'s HTTP worker task.
  Lock lock(settings_mutex_);
  brightness_sensor_.update(
      static_cast<float>(settings_.ambient_light_threshold_percent) / 100.0f);
  if (web_.consumeRebootRequested()) {
    esp_restart();
  }
  syncHostnames();

  SmartLightRuntimeState state = buildRuntimeState();
  const SmartLightRuntimeState previous_state = state;
  WebAction web_action = WebAction::kNone;
  bool web_requested_value = false;
  if (web_.consumeRequestedLightState(state.light_state)) {
    web_action = WebAction::kLight;
    web_requested_value = state.light_state;
  } else if (web_.consumeRequestedSwitchState(state.switch_state)) {
    web_action = WebAction::kSwitch;
    web_requested_value = state.switch_state;
  } else if (web_.consumeRequestedNightState(state.night_state)) {
    web_action = WebAction::kNight;
    web_requested_value = state.night_state;
  }
  const SmartLightRuntimeState directly_requested_state = state;
  applyMatterEvents(state);
  SmartLightAutomation::applyButtonPress(btn_.pressed(), state);
  applyIrInput(state);
  SmartLightAutomation::applyDerivedRules(previous_state, state);
  reportWebAction(web_action, web_requested_value, directly_requested_state,
                  state);
  commitOutputs(state);
  // Must come after commitOutputs_(): it can block for ~150ms sending an IR
  // signal, and the web UI's toggle buttons render from these values, so
  // publishing the PRE-commit state here would make the very redirect the
  // browser follows right after POSTing /action show the OLD button state
  // until the next handle() iteration (or the next manual reload) catches
  // up -- this is what the "the button stays off until I reload" bug was.
  web_.setObservedStates(
      state.light_state, state.switch_state, state.night_state,
      static_cast<int>(brightness_sensor_.getNormalized() * 100.0f + 0.5f));
  if (web_action != WebAction::kNone) web_.completeAction();
  updateOccupancyLog(state.occupancy_state);
  updateStatusLed(state);
  handleDecommission();
}

void SmartLightController::syncHostnames() {
  bool hostname_updated = command_handler_.handle();
  if (web_.hostnameUpdated()) {
    hostname_updated = true;
    web_.clearHostnameUpdated();
  }

  if (hostname_updated) network_.setHostname(settings_.hostname);
}

SmartLightRuntimeState SmartLightController::buildRuntimeState() const {
  SmartLightRuntimeState state;
  state.light_state = last_light_state_;
  state.switch_state = last_switch_state_;
  state.night_state = last_night_state_;
  state.seconds_since_last_motion = motion_sensor_.getSecondsSinceLastMotion();
  state.occupancy_state =
      motion_sensor_.isOccupied(SmartLightAutomation::kOccupancyTimeoutSeconds);
  state.is_bright = brightness_sensor_.isBright();
  state.light_off_timeout_seconds = settings_.light_off_timeout_seconds;
  state.ambient_light_mode_enabled = settings_.ambient_light_mode_enabled;
  return state;
}

void SmartLightController::commitOutputs(const SmartLightRuntimeState& state) {
  const bool suppress_night_off_signal =
      last_night_state_ && !state.night_state && !last_light_state_ &&
      state.light_state;
  const bool suppress_light_off_signal =
      last_light_state_ && !state.light_state && !last_night_state_ &&
      state.night_state;
  commitSwitchState(state);
  commitNightState(state, suppress_night_off_signal);
  commitLightState(state, suppress_light_off_signal);
}

void SmartLightController::sendIrSignal(const IRRemote::IRData& data,
                                        const char* label) {
  LOGW("[IR-Tx] %s (size: %zu)", label, data.size());
  led_.blinkOnce(RgbLed::Color::kGreen);
  ir_remote_.send(data);
  LOGW("[IR-Tx] %s sent", label);
  vTaskDelay(pdMS_TO_TICKS(100));
}

void SmartLightController::applyMatterEvents(SmartLightRuntimeState& state) {
  MatterLight::Event event;
  if (!matter_light_.getEvent(event, 0)) return;

  bool force_light_resync = false;
  SmartLightAutomation::applyMatterEvent(event, state, force_light_resync);
  if (force_light_resync) {
    last_light_state_ = !state.light_state;
  }
}

void SmartLightController::applyIrInput(SmartLightRuntimeState& state) {
  if (!ir_remote_.available()) return;

  const auto ir_data = ir_remote_.get();
  ir_remote_.clear();
  if (IRRemote::isIrDataEqual(ir_data, settings_.ir_data_light_on)) {
    LOGI("[IR-Rx] Light ON Signal Received");
    if (!state.light_state) {
      state.light_state = true;
      state.switch_state = true;
    } else {
      state.switch_state = !state.switch_state;
    }
    LOGW("[SwitchState] %d (IR)", state.switch_state);
    led_.blinkOnce(RgbLed::Color::kGreen);
    return;
  }

  if (IRRemote::isIrDataEqual(ir_data, settings_.ir_data_light_off)) {
    LOGI("[IR-Rx] Light OFF Signal Received");
    if (state.light_state) {
      state.light_state = false;
      state.switch_state = false;
    } else {
      state.switch_state = !state.switch_state;
    }
    LOGW("[SwitchState] %d (IR)", state.switch_state);
    led_.blinkOnce(RgbLed::Color::kGreen);
    return;
  }

  LOGW("[IR-Rx] Unknown Signal Received");
  IRRemote::print(ir_data);
}

void SmartLightController::commitSwitchState(
    const SmartLightRuntimeState& state) {
  if (last_switch_state_ == state.switch_state) return;
  last_switch_state_ = state.switch_state;
  matter_light_.setSwitchState(state.switch_state);
}

void SmartLightController::commitNightState(const SmartLightRuntimeState& state,
                                            bool suppress_off_signal) {
  if (last_night_state_ == state.night_state) return;
  last_night_state_ = state.night_state;
  matter_light_.setNightState(state.night_state);

  if (state.night_state) {
    sendIrSignal(settings_.ir_data_night, "Night ON");
  } else if (!suppress_off_signal) {
    sendIrSignal(settings_.ir_data_light_off, "Light OFF (Night OFF)");
  }
}

void SmartLightController::commitLightState(const SmartLightRuntimeState& state,
                                            bool suppress_off_signal) {
  if (last_light_state_ == state.light_state) return;

  last_light_state_ = state.light_state;
  matter_light_.setLightState(state.light_state);

  if (state.light_state) {
    sendIrSignal(settings_.ir_data_light_on, "Light ON");
  } else if (!suppress_off_signal) {
    sendIrSignal(settings_.ir_data_light_off, "Light OFF");
  }
}

void SmartLightController::updateOccupancyLog(bool occupancy_state) {
  if (last_occupancy_state_ == occupancy_state) return;
  last_occupancy_state_ = occupancy_state;
  if (occupancy_state) {
    LOGI("[PIR] Motion Detected");
  } else {
    LOGI("[PIR] No Motion Timeout");
  }
}

void SmartLightController::updateStatusLed(
    const SmartLightRuntimeState& state) {
  led_.setBackground(SmartLightAutomation::selectStatusColor(
      state, matter_light_.isCommissioned() && !matter_light_.getStatus().commissioning_open, network_.hasIpv4()));
}

void SmartLightController::reportWebAction(
    WebAction action, bool requested_value,
    const SmartLightRuntimeState& directly_requested_state,
    const SmartLightRuntimeState& final_state) {
  if (action == WebAction::kNone) return;

  const char* action_label = "";
  switch (action) {
    case WebAction::kLight:
      action_label = "照明";
      break;
    case WebAction::kSwitch:
      action_label = "人感センサ連動";
      break;
    case WebAction::kNight:
      action_label = "常夜灯";
      break;
    case WebAction::kNone:
      return;
  }

  std::string message = action_label;
  message += requested_value ? "をオンにしました。" : "をオフにしました。";

  std::string linked_changes;
  auto append_change = [&linked_changes](const char* label, bool value) {
    if (!linked_changes.empty()) linked_changes += "、";
    linked_changes += label;
    linked_changes += value ? "をオン" : "をオフ";
  };

  if (action != WebAction::kLight &&
      directly_requested_state.light_state != final_state.light_state) {
    append_change("照明", final_state.light_state);
  }
  if (action != WebAction::kSwitch &&
      directly_requested_state.switch_state != final_state.switch_state) {
    append_change("人感センサ連動", final_state.switch_state);
  }
  if (action != WebAction::kNight &&
      directly_requested_state.night_state != final_state.night_state) {
    append_change("常夜灯", final_state.night_state);
  }

  if (!linked_changes.empty()) {
    message += " 連動して";
    message += linked_changes;
    message += "にしました。";
  }
  web_.showStatus(message);
}

void SmartLightController::handleDecommission() {
  if (btn_.longHoldStarted()) led_.blinkOnce(RgbLed::Color::kMagenta);
  if (btn_.longPressed()) {
    if (matter_light_.isCommissioned()) {
      matter_light_.decommission();
    } else {
      matter_light_.openCommissioningWindow();
    }
  }

  if (!matter_light_.isCommissioned()) {
    static int64_t last_pairing_log_ms = 0;
    const int64_t now = esp_timer_get_time() / 1000;
    if (now - last_pairing_log_ms > 10000) {
      last_pairing_log_ms = now;
      matter_light_.printOnboarding();
    }
  }
}
