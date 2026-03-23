/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "brightness_sensor.h"
#include "ir_remote.h"
#include "matter_light.h"
#include "smart_light_settings.h"

class SmartLightWeb {
 public:
  SmartLightWeb(SmartLightSettings& settings,
                SmartLightSettingsStore& settings_store,
                IRRemote& ir_remote,
                BrightnessSensor& brightness_sensor,
                MatterLight& matter_light)
      : settings_(settings),
        settings_store_(settings_store),
        ir_remote_(ir_remote),
        brightness_sensor_(brightness_sensor),
        matter_light_(matter_light) {}

  void begin();
  void handle();
  void setObservedStates(bool light_state, bool switch_state);

  bool hostnameUpdated() const { return hostname_updated_; }
  void clearHostnameUpdated() { hostname_updated_ = false; }
  bool consumeRequestedLightState(bool& light_state);
  bool consumeRequestedSwitchState(bool& switch_state);

 private:
  SmartLightSettings& settings_;
  SmartLightSettingsStore& settings_store_;
  IRRemote& ir_remote_;
  BrightnessSensor& brightness_sensor_;
  MatterLight& matter_light_;
  WebServer server_{80};
  bool hostname_updated_ = false;
  bool observed_light_state_ = false;
  bool observed_switch_state_ = false;
  bool requested_light_state_pending_ = false;
  bool requested_light_state_ = false;
  bool requested_switch_state_pending_ = false;
  bool requested_switch_state_ = false;

  void handleRoot();
  void handleSaveSettings();
  void handleRecord();
  void handleAction();
  void handleReboot();
  void sendPage(const String& message = String(), bool is_error = false);
  String buildPage(const String& message, bool is_error) const;
  String htmlEscape(const String& input) const;
};
