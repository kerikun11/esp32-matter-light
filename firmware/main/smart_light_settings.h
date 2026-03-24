/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <Preferences.h>

#include <string>

#include "app_log.h"
#include "ir_remote.h"

struct SmartLightSettings {
  static constexpr const char* kPrefPartition = "matter";
  static constexpr const char* kPrefHostname = "hostname";
  static constexpr const char* kPrefTimeout = "timeout";
  static constexpr const char* kPrefAmbient = "ambient";
  static constexpr const char* kPrefAmbientThreshold = "ambient_th";
  static constexpr const char* kPrefIrOn = "ir_on";
  static constexpr const char* kPrefIrOff = "ir_off";

  static constexpr const char* kHostnameDefault = "esp32-matter-light";
  static constexpr int kLightOffTimeoutSecondsDefault = 5 * 60;
  static constexpr int kAmbientLightThresholdPercentDefault = 50;

  std::string hostname = kHostnameDefault;
  int light_off_timeout_seconds = kLightOffTimeoutSecondsDefault;
  bool ambient_light_mode_enabled = true;
  int ambient_light_threshold_percent = kAmbientLightThresholdPercentDefault;
  IRRemote::IRData ir_data_light_on;
  IRRemote::IRData ir_data_light_off;
};

class SmartLightSettingsStore {
 public:
  bool begin();
  SmartLightSettings load();

  void saveHostname(const std::string& hostname);
  void saveLightOffTimeoutSeconds(int seconds);
  void saveAmbientLightModeEnabled(bool enabled);
  void saveAmbientLightThresholdPercent(int threshold_percent);
  void saveIrDataLightOn(const IRRemote::IRData& data);
  void saveIrDataLightOff(const IRRemote::IRData& data);

 private:
  Preferences prefs_;
};
