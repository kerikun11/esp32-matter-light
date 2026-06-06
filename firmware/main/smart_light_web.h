/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "ir_remote.h"
#include "rgb_led.h"
#include "smart_light_settings.h"

class SmartLightWeb {
 public:
  SmartLightWeb(SmartLightSettings& settings,
                SmartLightSettingsStore& settings_store,
                IRRemote& ir_remote, RgbLed& led)
      : settings_(settings),
        settings_store_(settings_store),
        ir_remote_(ir_remote),
        led_(led) {}

  void begin();
  void handle();
  void setObservedStates(bool light_state, bool switch_state, bool night_state,
                         int ambient_light_percent);

  bool hostnameUpdated() const { return hostname_updated_; }
  void clearHostnameUpdated() { hostname_updated_ = false; }
  bool consumeRequestedLightState(bool& light_state);
  bool consumeRequestedSwitchState(bool& switch_state);
  bool consumeRequestedNightState(bool& night_state);
  bool consumeRebootRequested();
  void showStatus(const String& message, bool is_error = false);

 private:
  struct PendingState {
    bool pending = false;
    bool value = false;

    void request(bool requested_value) {
      value = requested_value;
      pending = true;
    }

    bool consume(bool& requested_value) {
      if (!pending) return false;
      requested_value = value;
      pending = false;
      return true;
    }
  };

  SmartLightSettings& settings_;
  SmartLightSettingsStore& settings_store_;
  IRRemote& ir_remote_;
  RgbLed& led_;
  WebServer server_{80};
  bool hostname_updated_ = false;
  bool observed_light_state_ = false;
  bool observed_switch_state_ = false;
  bool observed_night_state_ = false;
  int observed_ambient_light_percent_ = 0;
  PendingState requested_light_state_;
  PendingState requested_switch_state_;
  PendingState requested_night_state_;
  bool reboot_requested_ = false;
  String status_message_;
  bool status_is_error_ = false;

  void handleRoot();
  void handleSaveSettings();
  void handleRecord();
  void handleAction();
  void sendPage();
  String buildPage() const;
};
