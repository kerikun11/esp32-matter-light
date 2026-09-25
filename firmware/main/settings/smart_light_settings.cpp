/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "settings/smart_light_settings.h"

bool SmartLightSettingsStore::begin() {
  return storage_.open(SmartLightSettings::kPrefNamespace);
}

SmartLightSettings SmartLightSettingsStore::load() {
  SmartLightSettings settings;
  settings.device_name =
      storage_.readString(SmartLightSettings::kPrefDeviceName,
                          SmartLightSettings::kDeviceNameDefault)
          .c_str();
  settings.hostname = storage_.readString(SmartLightSettings::kPrefHostname,
                                          SmartLightSettings::kHostnameDefault)
                          .c_str();
  settings.light_off_timeout_seconds =
      storage_.readInt32(SmartLightSettings::kPrefTimeout,
                         SmartLightSettings::kLightOffTimeoutSecondsDefault);
  settings.ambient_light_mode_enabled =
      storage_.readBool(SmartLightSettings::kPrefAmbient, true);
  settings.ambient_light_threshold_percent =
      storage_.readInt32(SmartLightSettings::kPrefAmbientThreshold,
                         SmartLightSettings::kAmbientLightThresholdPercentDefault);
  settings.night_light_feature_enabled =
      storage_.readBool(SmartLightSettings::kPrefNightFeature, true);
  IRRemote::loadFromStorage(storage_, SmartLightSettings::kPrefIrOn,
                            settings.ir_data_light_on);
  IRRemote::loadFromStorage(storage_, SmartLightSettings::kPrefIrOff,
                            settings.ir_data_light_off);
  IRRemote::loadFromStorage(storage_, SmartLightSettings::kPrefIrNight,
                            settings.ir_data_night);

  LOGI("[NVS] device_name: %s", settings.device_name.c_str());
  LOGI("[NVS] hostname: %s", settings.hostname.c_str());
  LOGI("[NVS] light_off_timeout_seconds: %d",
       settings.light_off_timeout_seconds);
  LOGI("[NVS] ambient_light_mode_enabled: %d",
       settings.ambient_light_mode_enabled);
  LOGI("[NVS] ambient_light_threshold_percent: %d",
       settings.ambient_light_threshold_percent);
  LOGI("[NVS] night_light_feature_enabled: %d",
       settings.night_light_feature_enabled);
  LOGI("[NVS] IR ON Data size: %zu", settings.ir_data_light_on.size());
  LOGI("[NVS] IR OFF Data size: %zu", settings.ir_data_light_off.size());
  LOGI("[NVS] IR NIGHT Data size: %zu", settings.ir_data_night.size());
  return settings;
}

void SmartLightSettingsStore::saveDeviceName(const std::string& device_name) {
  storage_.writeString(SmartLightSettings::kPrefDeviceName, device_name.c_str());
}

void SmartLightSettingsStore::saveHostname(const std::string& hostname) {
  storage_.writeString(SmartLightSettings::kPrefHostname, hostname.c_str());
}

void SmartLightSettingsStore::saveLightOffTimeoutSeconds(int seconds) {
  storage_.writeInt32(SmartLightSettings::kPrefTimeout, seconds);
}

void SmartLightSettingsStore::saveAmbientLightModeEnabled(bool enabled) {
  storage_.writeBool(SmartLightSettings::kPrefAmbient, enabled);
}

void SmartLightSettingsStore::saveAmbientLightThresholdPercent(
    int threshold_percent) {
  storage_.writeInt32(SmartLightSettings::kPrefAmbientThreshold, threshold_percent);
}

void SmartLightSettingsStore::saveNightLightFeatureEnabled(bool enabled) {
  storage_.writeBool(SmartLightSettings::kPrefNightFeature, enabled);
}

void SmartLightSettingsStore::saveIrDataLightOn(const IRRemote::IRData& data) {
  IRRemote::saveToStorage(storage_, SmartLightSettings::kPrefIrOn, data);
}

void SmartLightSettingsStore::saveIrDataLightOff(const IRRemote::IRData& data) {
  IRRemote::saveToStorage(storage_, SmartLightSettings::kPrefIrOff, data);
}

void SmartLightSettingsStore::saveIrDataNight(const IRRemote::IRData& data) {
  IRRemote::saveToStorage(storage_, SmartLightSettings::kPrefIrNight, data);
}
