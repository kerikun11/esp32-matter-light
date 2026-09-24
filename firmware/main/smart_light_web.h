/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <esp_http_server.h>

#include <string>

#include "ir_remote.h"
#include "lockable.h"
#include "rgb_led.h"
#include "smart_light_settings.h"

// Owns the on-device settings UI's HTTP server. All member state below,
// plus `settings` (shared with SmartLightController), is read from the main
// app task and written from the HTTP server's worker task (esp_http_server
// callbacks run on their own task, unlike Arduino's WebServer::handleClient()
// which used to run inline in the main loop), so every access goes through
// `mutex`, which the caller owns and shares with SmartLightController.
class SmartLightWeb {
 public:
  SmartLightWeb(SmartLightSettings& settings, Mutex& settings_mutex,
                SmartLightSettingsStore& settings_store,
                IRRemote& ir_remote, RgbLed& led)
      : settings_(settings),
        mutex_(settings_mutex),
        settings_store_(settings_store),
        ir_remote_(ir_remote),
        led_(led) {}

  void begin();
  httpd_handle_t rawHandle() const { return server_; }

  void setObservedStates(bool light_state, bool switch_state, bool night_state,
                         int ambient_light_percent);

  bool hostnameUpdated();
  void clearHostnameUpdated();
  bool consumeRequestedLightState(bool& light_state);
  bool consumeRequestedSwitchState(bool& switch_state);
  bool consumeRequestedNightState(bool& night_state);
  bool consumeRebootRequested();
  void showStatus(const std::string& message, bool is_error = false);

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
  Mutex& mutex_;
  SmartLightSettingsStore& settings_store_;
  IRRemote& ir_remote_;
  RgbLed& led_;
  httpd_handle_t server_ = nullptr;

  bool hostname_updated_ = false;
  bool observed_light_state_ = false;
  bool observed_switch_state_ = false;
  bool observed_night_state_ = false;
  int observed_ambient_light_percent_ = 0;
  PendingState requested_light_state_;
  PendingState requested_switch_state_;
  PendingState requested_night_state_;
  bool reboot_requested_ = false;
  std::string status_message_;
  bool status_is_error_ = false;

  esp_err_t handleRoot(httpd_req_t* req);
  esp_err_t handleSaveSettings(httpd_req_t* req);
  esp_err_t handleRecord(httpd_req_t* req);
  esp_err_t handleAction(httpd_req_t* req);
  void sendPage(httpd_req_t* req);
  std::string buildPage() const;

  static esp_err_t handleRootTrampoline(httpd_req_t* req) {
    return static_cast<SmartLightWeb*>(req->user_ctx)->handleRoot(req);
  }
  static esp_err_t handleSaveSettingsTrampoline(httpd_req_t* req) {
    return static_cast<SmartLightWeb*>(req->user_ctx)->handleSaveSettings(req);
  }
  static esp_err_t handleRecordTrampoline(httpd_req_t* req) {
    return static_cast<SmartLightWeb*>(req->user_ctx)->handleRecord(req);
  }
  static esp_err_t handleActionTrampoline(httpd_req_t* req) {
    return static_cast<SmartLightWeb*>(req->user_ctx)->handleAction(req);
  }
};
