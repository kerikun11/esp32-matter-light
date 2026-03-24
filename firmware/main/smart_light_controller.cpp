/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "smart_light_controller.h"

#include "app_log.h"

SmartLightController::SmartLightController()
    : command_handler_(command_parser_, settings_, settings_store_, ir_remote_,
                       brightness_sensor_),
      web_(settings_, settings_store_, ir_remote_) {}

void SmartLightController::begin() {
  led_.setBackground(RgbLed::Color::Green);

  settings_store_.begin();
  settings_ = settings_store_.load();

  ir_remote_.begin(CONFIG_APP_PIN_IR_TRANSMITTER, CONFIG_APP_PIN_IR_RECEIVER);

  matter_light_.begin(true, true, false);
  last_light_state_ = false;
  last_switch_state_ = true;
  last_night_state_ = false;

  setupOta();
  web_.begin();
}

void SmartLightController::handle() {
  ArduinoOTA.handle();

  btn_.update();
  led_.update();
  motion_sensor_.update();
  brightness_sensor_.update(
      static_cast<float>(settings_.ambient_light_threshold_percent) / 100.0f);
  web_.setObservedStates(
      last_light_state_, last_switch_state_, last_night_state_,
      static_cast<int>(brightness_sensor_.getNormalized() * 100.0f + 0.5f));
  web_.handle();
  syncHostnameIfNeeded_();

  SmartLightRuntimeState state = buildRuntimeState_();
  const SmartLightRuntimeState previous_state = state;
  (void)web_.consumeRequestedLightState(state.light_state);
  (void)web_.consumeRequestedSwitchState(state.switch_state);
  (void)web_.consumeRequestedNightState(state.night_state);
  applyMatterEvents(state);
  SmartLightAutomation::applyButtonPress(btn_.pressed(), state);
  applyIrInput(state);
  SmartLightAutomation::applyDerivedRules(previous_state, state);
  commitOutputs_(state);
  updateOccupancyLog(state.occupancy_state);
  updateStatusLed(state);
  handleDecommission();
}

void SmartLightController::setupOta() {
  ArduinoOTA.setHostname(settings_.hostname.c_str());
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([]() {
    auto cmd = ArduinoOTA.getCommand();
    LOGI("[OTA] Start updating %s",
         cmd == U_FLASH ? "sketch"
                        : (cmd == U_SPIFFS ? "filesystem" : "unknown"));
  });
  ArduinoOTA.onEnd([]() { LOGI("[OTA] End"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    LOGI("[OTA] Progress: %u%% (%d/%d)", 100 * progress / total, progress,
         total);
  });
  ArduinoOTA.onError([](ota_error_t error) { LOGI("[OTA] Error: %d", error); });
  ArduinoOTA.begin();
}

void SmartLightController::syncHostnameIfNeeded_() {
  if (command_handler_.handle()) {
    ArduinoOTA.setHostname(settings_.hostname.c_str());
  }
  if (!web_.hostnameUpdated()) return;

  ArduinoOTA.setHostname(settings_.hostname.c_str());
  web_.clearHostnameUpdated();
}

SmartLightRuntimeState SmartLightController::buildRuntimeState_() const {
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

void SmartLightController::commitOutputs_(const SmartLightRuntimeState& state) {
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

void SmartLightController::sendIrSignal_(const IRRemote::IRData& data,
                                         const char* label) {
  LOGW("[IR-Tx] %s (size: %zu)", label, data.size());
  led_.blinkOnce(RgbLed::Color::Green);
  ir_remote_.send(data);
  LOGW("[IR-Tx] %s sent", label);
  delay(100);
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
    led_.blinkOnce(RgbLed::Color::Green);
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
    led_.blinkOnce(RgbLed::Color::Green);
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
    sendIrSignal_(settings_.ir_data_night, "Night ON");
  } else if (!suppress_off_signal) {
    sendIrSignal_(settings_.ir_data_light_off, "Light OFF (Night OFF)");
  }
}

void SmartLightController::commitLightState(const SmartLightRuntimeState& state,
                                            bool suppress_off_signal) {
  if (last_light_state_ == state.light_state) return;

  last_light_state_ = state.light_state;
  matter_light_.setLightState(state.light_state);

  if (state.light_state) {
    sendIrSignal_(settings_.ir_data_light_on, "Light ON");
  } else if (!suppress_off_signal) {
    sendIrSignal_(settings_.ir_data_light_off, "Light OFF");
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
      state, matter_light_.isCommissioned(), matter_light_.isConnected()));
}

void SmartLightController::handleDecommission() {
  if (btn_.longHoldStarted()) led_.blinkOnce(RgbLed::Color::Magenta);
  if (btn_.longPressed()) {
    if (matter_light_.isCommissioned()) {
      matter_light_.decommission();
    } else {
      matter_light_.openCommissioningWindow();
    }
  }

  if (!matter_light_.isCommissioned()) {
    static long last_pairing_log_ms_ = 0;
    const long now = millis();
    if (now - last_pairing_log_ms_ > 10000) {
      last_pairing_log_ms_ = now;
      matter_light_.printOnboarding();
    }
  }
}
