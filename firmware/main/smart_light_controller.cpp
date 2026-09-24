/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "smart_light_controller.h"

#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mdns.h>

#include "app_log.h"
#include "ota_service.h"

SmartLightController::SmartLightController()
    : command_handler_(command_parser_, settings_, settings_store_, ir_remote_,
                       brightness_sensor_),
      web_(settings_, settings_mutex_, settings_store_, ir_remote_, led_) {}

void SmartLightController::begin() {
  led_.setBackground(RgbLed::Color::Green);

  if (!settings_store_.begin()) {
    LOGE("[Prefs] Failed to open settings");
  }
  settings_ = settings_store_.load();

  ir_remote_.begin(CONFIG_APP_PIN_IR_TRANSMITTER, CONFIG_APP_PIN_IR_RECEIVER);

  last_light_state_ = false;
  last_switch_state_ = true;
  last_night_state_ = false;
  matter_light_.begin(last_light_state_, last_switch_state_, last_night_state_,
                      settings_.night_light_feature_enabled);

  web_.begin();
  registerOtaHandlers(web_.rawHandle());
}

void SmartLightController::handle() {
  syncWifiPowerSave_();

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
  syncHostnames_();

  SmartLightRuntimeState state = buildRuntimeState_();
  const SmartLightRuntimeState previous_state = state;
  WebAction web_action = WebAction::None;
  bool web_requested_value = false;
  if (web_.consumeRequestedLightState(state.light_state)) {
    web_action = WebAction::Light;
    web_requested_value = state.light_state;
  } else if (web_.consumeRequestedSwitchState(state.switch_state)) {
    web_action = WebAction::Switch;
    web_requested_value = state.switch_state;
  } else if (web_.consumeRequestedNightState(state.night_state)) {
    web_action = WebAction::Night;
    web_requested_value = state.night_state;
  }
  const SmartLightRuntimeState directly_requested_state = state;
  applyMatterEvents(state);
  SmartLightAutomation::applyButtonPress(btn_.pressed(), state);
  applyIrInput(state);
  SmartLightAutomation::applyDerivedRules(previous_state, state);
  reportWebAction_(web_action, web_requested_value, directly_requested_state,
                   state);
  commitOutputs_(state);
  // Must come after commitOutputs_(): it can block for ~150ms sending an IR
  // signal, and the web UI's toggle buttons render from these values, so
  // publishing the PRE-commit state here would make the very redirect the
  // browser follows right after POSTing /action show the OLD button state
  // until the next handle() iteration (or the next manual reload) catches
  // up -- this is what the "the button stays off until I reload" bug was.
  web_.setObservedStates(
      state.light_state, state.switch_state, state.night_state,
      static_cast<int>(brightness_sensor_.getNormalized() * 100.0f + 0.5f));
  updateOccupancyLog(state.occupancy_state);
  updateStatusLed(state);
  handleDecommission();
}

void SmartLightController::syncWifiPowerSave_() {
  constexpr int64_t kRetryIntervalMs = 1000;
  const int64_t now = esp_timer_get_time() / 1000;
  if (now - last_wifi_ps_attempt_ms_ < kRetryIntervalMs) return;
  last_wifi_ps_attempt_ms_ = now;

  // Right after begin(), the Wi-Fi driver may not be started yet (Matter
  // brings it up asynchronously), so this can transiently fail; retry until
  // the driver is ready instead of giving up after a single attempt.
  //
  // Keep reasserting this every second rather than stopping after the first
  // success: the Matter/Wi-Fi stack can re-enable modem-sleep power save on
  // its own later (e.g. around reconnects), and once that happens, the first
  // HTTP request after any idle period stalls for several seconds while the
  // radio wakes back up -- directly undermining the /update OTA endpoint's
  // "just curl it" usability.
  esp_wifi_set_ps(WIFI_PS_NONE);
}

void SmartLightController::syncHostnames_() {
  bool hostname_updated = command_handler_.handle();
  if (web_.hostnameUpdated()) {
    hostname_updated = true;
    web_.clearHostnameUpdated();
  }

  syncAdditionalMdnsHostname_(hostname_updated);
}

void SmartLightController::syncAdditionalMdnsHostname_(bool force) {
  constexpr int64_t kRetryIntervalMs = 1000;
  const int64_t now = esp_timer_get_time() / 1000;
  if (!force && now - last_mdns_sync_attempt_ms_ < kRetryIntervalMs) return;
  last_mdns_sync_attempt_ms_ = now;

  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip_info{};
  if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK ||
      ip_info.ip.addr == 0) {
    return;
  }

  if (!mdns_hostname_.empty() && mdns_hostname_ != settings_.hostname) {
    const esp_err_t err = mdns_delegate_hostname_remove(mdns_hostname_.c_str());
    if (err != ESP_OK) {
      LOGW("[mDNS] Failed to remove %s.local: %s", mdns_hostname_.c_str(),
           esp_err_to_name(err));
      return;
    }
    LOGI("[mDNS] Removed additional hostname: %s.local",
         mdns_hostname_.c_str());
    mdns_hostname_.clear();
    mdns_ipv4_address_ = 0;
  }

  mdns_ip_addr_t address{};
  address.addr.type = ESP_IPADDR_TYPE_V4;
  address.addr.u_addr.ip4 = ip_info.ip;

  if (mdns_hostname_.empty()) {
    const esp_err_t err =
        mdns_delegate_hostname_add(settings_.hostname.c_str(), &address);
    if (err != ESP_OK) {
      if (err != last_mdns_error_) {
        LOGW("[mDNS] Failed to add %s.local: %s", settings_.hostname.c_str(),
             esp_err_to_name(err));
      }
      last_mdns_error_ = err;
      return;
    }
    last_mdns_error_ = ESP_OK;
    if (!mdns_hostname_exists(settings_.hostname.c_str())) {
      char primary_hostname[MDNS_NAME_BUF_LEN] = {};
      const esp_err_t get_err = mdns_hostname_get(primary_hostname);
      LOGW("[mDNS] %s.local was not added (primary: %s)",
           settings_.hostname.c_str(),
           get_err == ESP_OK ? primary_hostname : "unavailable");
      return;
    }
    mdns_hostname_ = settings_.hostname;
    mdns_ipv4_address_ = ip_info.ip.addr;
    LOGI("[mDNS] Added additional hostname: %s.local -> " IPSTR,
         mdns_hostname_.c_str(), IP2STR(&ip_info.ip));
    return;
  }

  if (mdns_ipv4_address_ == ip_info.ip.addr) return;
  const esp_err_t err =
      mdns_delegate_hostname_set_address(mdns_hostname_.c_str(), &address);
  if (err != ESP_OK) {
    LOGW("[mDNS] Failed to update %s.local: %s", mdns_hostname_.c_str(),
         esp_err_to_name(err));
    return;
  }
  mdns_ipv4_address_ = ip_info.ip.addr;
  LOGI("[mDNS] Updated address: %s.local -> " IPSTR, mdns_hostname_.c_str(),
       IP2STR(&ip_info.ip));
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

void SmartLightController::reportWebAction_(
    WebAction action, bool requested_value,
    const SmartLightRuntimeState& directly_requested_state,
    const SmartLightRuntimeState& final_state) {
  if (action == WebAction::None) return;

  const char* action_label = "";
  switch (action) {
    case WebAction::Light:
      action_label = "照明";
      break;
    case WebAction::Switch:
      action_label = "人感センサ連動";
      break;
    case WebAction::Night:
      action_label = "常夜灯";
      break;
    case WebAction::None:
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

  if (action != WebAction::Light &&
      directly_requested_state.light_state != final_state.light_state) {
    append_change("照明", final_state.light_state);
  }
  if (action != WebAction::Switch &&
      directly_requested_state.switch_state != final_state.switch_state) {
    append_change("人感センサ連動", final_state.switch_state);
  }
  if (action != WebAction::Night &&
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
  if (btn_.longHoldStarted()) led_.blinkOnce(RgbLed::Color::Magenta);
  if (btn_.longPressed()) {
    if (matter_light_.isCommissioned()) {
      matter_light_.decommission();
    } else {
      matter_light_.openCommissioningWindow();
    }
  }

  if (!matter_light_.isCommissioned()) {
    static int64_t last_pairing_log_ms_ = 0;
    const int64_t now = esp_timer_get_time() / 1000;
    if (now - last_pairing_log_ms_ > 10000) {
      last_pairing_log_ms_ = now;
      matter_light_.printOnboarding();
    }
  }
}
