/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once
#include <driver/gpio.h>
#include <esp_timer.h>

#include <climits>

class MotionSensor {
 public:
  explicit MotionSensor(int pin) : pin_(static_cast<gpio_num_t>(pin)) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin_;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
  }

  void update() {
    if (gpio_get_level(pin_)) {
      last_motion_time_ms_ = esp_timer_get_time() / 1000;
      seen_motion_ = true;
    }
  }

  int getSecondsSinceLastMotion() const {
    if (!seen_motion_) return INT_MAX;
    return (esp_timer_get_time() / 1000 - last_motion_time_ms_) / 1000;
  }

  bool isOccupied(int timeout_seconds) const {
    return getSecondsSinceLastMotion() < timeout_seconds;
  }

 private:
  const gpio_num_t pin_;
  int64_t last_motion_time_ms_ = 0;
  bool seen_motion_ = false;
};
