/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "ir_remote.h"
#include "smart_light_settings.h"

class SmartLightWeb {
 public:
  SmartLightWeb(SmartLightSettings& settings,
                SmartLightSettingsStore& settings_store,
                IRRemote& ir_remote)
      : settings_(settings),
        settings_store_(settings_store),
        ir_remote_(ir_remote) {}

  void begin();
  void handle();
  void setObservedStates(bool light_state, bool switch_state,
                         int ambient_light_percent);

  bool hostnameUpdated() const { return hostname_updated_; }
  void clearHostnameUpdated() { hostname_updated_ = false; }
  bool consumeRequestedLightState(bool& light_state);
  bool consumeRequestedSwitchState(bool& switch_state);

 private:
  SmartLightSettings& settings_;
  SmartLightSettingsStore& settings_store_;
  IRRemote& ir_remote_;
  WebServer server_{80};
  bool hostname_updated_ = false;
  bool observed_light_state_ = false;
  bool observed_switch_state_ = false;
  int observed_ambient_light_percent_ = 0;
  bool requested_light_state_pending_ = false;
  bool requested_light_state_ = false;
  bool requested_switch_state_pending_ = false;
  bool requested_switch_state_ = false;

  void handleRoot();
  void handleSaveSettings();
  void handleRecord();
  void handleAction();
  void redirectRoot();
  void sendPage();
  String buildPage() const;
};
