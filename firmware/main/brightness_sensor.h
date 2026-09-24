/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once
#include <esp_adc/adc_oneshot.h>
#include <esp_timer.h>

#include <algorithm>

#include "app_log.h"

class BrightnessSensor {
 public:
  explicit BrightnessSensor(int pin) {
    adc_unit_t unit_id;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel(pin, &unit_id, &channel) != ESP_OK) {
      LOGE("[Brightness] GPIO%d is not ADC-capable", pin);
      return;
    }
    channel_ = channel;

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = unit_id;
    if (adc_oneshot_new_unit(&unit_cfg, &adc_handle_) != ESP_OK) {
      LOGE("[Brightness] adc_oneshot_new_unit failed");
      return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_oneshot_config_channel(adc_handle_, channel_, &chan_cfg) !=
        ESP_OK) {
      LOGE("[Brightness] adc_oneshot_config_channel failed");
    }
  }

  void update(float threshold = 0.5f, float hysteresis = 0.1f) {
    int raw = 0;
    if (adc_handle_) adc_oneshot_read(adc_handle_, channel_, &raw);
    float value = std::clamp(static_cast<float>(raw) / 4095.0f, 0.0f, 1.0f);

    bool new_bright;
    if (was_bright_) {
      new_bright = (value >= threshold - hysteresis);
    } else {
      new_bright = (value >= threshold + hysteresis);
    }

    if (new_bright != was_bright_) {
      last_change_millis_ = esp_timer_get_time() / 1000;
      was_bright_ = new_bright;
    }

    is_bright_ = new_bright;
    normalized_value_ = value;
  }

  float getNormalized() const { return normalized_value_; }

  bool isBright() const { return is_bright_; }

  int64_t getMillisSinceChange() const {
    return esp_timer_get_time() / 1000 - last_change_millis_;
  }

 private:
  adc_oneshot_unit_handle_t adc_handle_ = nullptr;
  adc_channel_t channel_ = ADC_CHANNEL_0;
  float normalized_value_ = 0.0f;
  bool is_bright_ = false;
  bool was_bright_ = false;
  int64_t last_change_millis_ = 0;
};
