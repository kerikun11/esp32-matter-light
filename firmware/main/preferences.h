/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 *
 * Minimal drop-in replacement for Arduino's Preferences class, backed
 * directly by the ESP-IDF NVS API.
 */
#pragma once

#include <nvs.h>
#include <nvs_flash.h>

#include <string>

#include "app_log.h"

class Preferences {
 public:
  bool begin(const char* name, bool read_only = false) {
    const esp_err_t err = nvs_open(
        name, read_only ? NVS_READONLY : NVS_READWRITE, &handle_);
    if (err != ESP_OK) {
      LOGE("[Prefs] nvs_open(%s) failed: %s", name, esp_err_to_name(err));
      return false;
    }
    open_ = true;
    return true;
  }

  void end() {
    if (open_) nvs_close(handle_);
    open_ = false;
  }

  std::string getString(const char* key,
                        const char* default_value = "") const {
    size_t length = 0;
    if (!open_ || nvs_get_str(handle_, key, nullptr, &length) != ESP_OK) {
      return default_value;
    }
    std::string value(length, '\0');
    if (nvs_get_str(handle_, key, value.data(), &length) != ESP_OK) {
      return default_value;
    }
    value.resize(length > 0 ? length - 1 : 0);  // drop the trailing NUL
    return value;
  }

  bool putString(const char* key, const std::string& value) {
    if (!open_) return false;
    return nvs_set_str(handle_, key, value.c_str()) == ESP_OK &&
           nvs_commit(handle_) == ESP_OK;
  }

  int32_t getInt(const char* key, int32_t default_value = 0) const {
    int32_t value = default_value;
    if (!open_ || nvs_get_i32(handle_, key, &value) != ESP_OK) {
      return default_value;
    }
    return value;
  }

  bool putInt(const char* key, int32_t value) {
    if (!open_) return false;
    return nvs_set_i32(handle_, key, value) == ESP_OK &&
           nvs_commit(handle_) == ESP_OK;
  }

  bool getBool(const char* key, bool default_value = false) const {
    uint8_t value = default_value ? 1 : 0;
    if (!open_ || nvs_get_u8(handle_, key, &value) != ESP_OK) {
      return default_value;
    }
    return value != 0;
  }

  bool putBool(const char* key, bool value) {
    if (!open_) return false;
    return nvs_set_u8(handle_, key, value ? 1 : 0) == ESP_OK &&
           nvs_commit(handle_) == ESP_OK;
  }

  size_t getBytesLength(const char* key) const {
    size_t length = 0;
    if (!open_ || nvs_get_blob(handle_, key, nullptr, &length) != ESP_OK) {
      return 0;
    }
    return length;
  }

  size_t getBytes(const char* key, void* buf, size_t max_len) const {
    size_t length = max_len;
    if (!open_ || nvs_get_blob(handle_, key, buf, &length) != ESP_OK) {
      return 0;
    }
    return length;
  }

  size_t putBytes(const char* key, const void* data, size_t len) {
    if (!open_) return 0;
    if (nvs_set_blob(handle_, key, data, len) != ESP_OK) return 0;
    if (nvs_commit(handle_) != ESP_OK) return 0;
    return len;
  }

 private:
  nvs_handle_t handle_ = 0;
  bool open_ = false;
};
