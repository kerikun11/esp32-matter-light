/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "smart_light_settings.h"

bool SmartLightSettingsStore::begin() {
  return prefs_.begin(SmartLightSettings::kPrefPartition);
}

SmartLightSettings SmartLightSettingsStore::load() {
  SmartLightSettings settings;
  settings.hostname = prefs_.getString(SmartLightSettings::kPrefHostname,
                                       SmartLightSettings::kHostnameDefault)
                          .c_str();
  settings.light_off_timeout_seconds =
      prefs_.getInt(SmartLightSettings::kPrefTimeout,
                    SmartLightSettings::kLightOffTimeoutSecondsDefault);
  settings.ambient_light_mode_enabled =
      prefs_.getBool(SmartLightSettings::kPrefAmbient, true);
  settings.ambient_light_threshold_percent =
      prefs_.getInt(SmartLightSettings::kPrefAmbientThreshold,
                    SmartLightSettings::kAmbientLightThresholdPercentDefault);
  IRRemote::loadFromPreferences(prefs_, SmartLightSettings::kPrefIrOn,
                                settings.ir_data_light_on);
  IRRemote::loadFromPreferences(prefs_, SmartLightSettings::kPrefIrOff,
                                settings.ir_data_light_off);
  IRRemote::loadFromPreferences(prefs_, SmartLightSettings::kPrefIrNight,
                                settings.ir_data_night);

  LOGI("[Prefs] hostname: %s", settings.hostname.c_str());
  LOGI("[Prefs] light_off_timeout_seconds: %d",
       settings.light_off_timeout_seconds);
  LOGI("[Prefs] ambient_light_mode_enabled: %d",
       settings.ambient_light_mode_enabled);
  LOGI("[Prefs] ambient_light_threshold_percent: %d",
       settings.ambient_light_threshold_percent);
  LOGI("[Prefs] IR ON Data size: %zu", settings.ir_data_light_on.size());
  LOGI("[Prefs] IR OFF Data size: %zu", settings.ir_data_light_off.size());
  LOGI("[Prefs] IR NIGHT Data size: %zu", settings.ir_data_night.size());
  return settings;
}

void SmartLightSettingsStore::saveHostname(const std::string& hostname) {
  prefs_.putString(SmartLightSettings::kPrefHostname, hostname.c_str());
}

void SmartLightSettingsStore::saveLightOffTimeoutSeconds(int seconds) {
  prefs_.putInt(SmartLightSettings::kPrefTimeout, seconds);
}

void SmartLightSettingsStore::saveAmbientLightModeEnabled(bool enabled) {
  prefs_.putBool(SmartLightSettings::kPrefAmbient, enabled);
}

void SmartLightSettingsStore::saveAmbientLightThresholdPercent(
    int threshold_percent) {
  prefs_.putInt(SmartLightSettings::kPrefAmbientThreshold, threshold_percent);
}

void SmartLightSettingsStore::saveIrDataLightOn(const IRRemote::IRData& data) {
  IRRemote::saveToPreferences(prefs_, SmartLightSettings::kPrefIrOn, data);
}

void SmartLightSettingsStore::saveIrDataLightOff(const IRRemote::IRData& data) {
  IRRemote::saveToPreferences(prefs_, SmartLightSettings::kPrefIrOff, data);
}

void SmartLightSettingsStore::saveIrDataNight(const IRRemote::IRData& data) {
  IRRemote::saveToPreferences(prefs_, SmartLightSettings::kPrefIrNight, data);
}
