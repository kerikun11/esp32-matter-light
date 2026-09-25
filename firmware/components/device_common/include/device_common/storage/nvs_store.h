/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 *
 * Owns an ESP-IDF NVS namespace. Each write is committed before returning.
 */
#pragma once

#include <nvs.h>
#include <nvs_flash.h>

#include <string>

#include "device_common/system/app_log.h"

class NvsStore {
 public:
  NvsStore() = default;
  ~NvsStore() { close(); }
  NvsStore(const NvsStore&) = delete;
  NvsStore& operator=(const NvsStore&) = delete;

  bool open(const char* name, bool read_only = false) {
    close();
    const esp_err_t err = nvs_open(
        name, read_only ? NVS_READONLY : NVS_READWRITE, &handle_);
    if (err != ESP_OK) {
      LOGE("[NVS] nvs_open(%s) failed: %s", name, esp_err_to_name(err));
      return false;
    }
    open_ = true;
    return true;
  }

  void close() {
    if (open_) nvs_close(handle_);
    open_ = false;
  }

  std::string readString(const char* key,
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

  bool writeString(const char* key, const std::string& value) {
    if (!open_) return false;
    return finishWrite(nvs_set_str(handle_, key, value.c_str()), key);
  }

  int32_t readInt32(const char* key, int32_t default_value = 0) const {
    int32_t value = default_value;
    if (!open_ || nvs_get_i32(handle_, key, &value) != ESP_OK) {
      return default_value;
    }
    return value;
  }

  bool writeInt32(const char* key, int32_t value) {
    if (!open_) return false;
    return finishWrite(nvs_set_i32(handle_, key, value), key);
  }

  bool readBool(const char* key, bool default_value = false) const {
    uint8_t value = default_value ? 1 : 0;
    if (!open_ || nvs_get_u8(handle_, key, &value) != ESP_OK) {
      return default_value;
    }
    return value != 0;
  }

  bool writeBool(const char* key, bool value) {
    if (!open_) return false;
    return finishWrite(nvs_set_u8(handle_, key, value ? 1 : 0), key);
  }

  size_t blobSize(const char* key) const {
    size_t length = 0;
    if (!open_ || nvs_get_blob(handle_, key, nullptr, &length) != ESP_OK) {
      return 0;
    }
    return length;
  }

  size_t readBlob(const char* key, void* buf, size_t max_len) const {
    size_t length = max_len;
    if (!open_ || nvs_get_blob(handle_, key, buf, &length) != ESP_OK) {
      return 0;
    }
    return length;
  }

  size_t writeBlob(const char* key, const void* data, size_t len) {
    if (!open_) return 0;
    if (!finishWrite(nvs_set_blob(handle_, key, data, len), key)) return 0;
    return len;
  }

 private:
  bool finishWrite(esp_err_t err, const char* key) {
    if (err == ESP_OK) err = nvs_commit(handle_);
    if (err != ESP_OK) LOGE("[NVS] Write %s failed: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
  }
  nvs_handle_t handle_ = 0;
  bool open_ = false;
};
