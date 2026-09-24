/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once
#include <driver/gpio.h>
#include <esp_timer.h>

class Button {
 public:
  Button(int pin, uint32_t longPressMs = 5000, uint32_t debounceMs = 20)
      : pin_(static_cast<gpio_num_t>(pin)),
        long_press_ms_(longPressMs),
        debounce_ms_(debounceMs) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin_;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
  }

  void update();
  bool pressing() const { return pressing_; }
  bool pressed() const { return pressed_; }
  bool longPressed() const { return long_pressed_; }
  bool longHold() const { return long_hold_; }
  bool longHoldStarted() const { return long_hold_start_; }

 private:
  const gpio_num_t pin_;
  const uint32_t long_press_ms_;
  const uint32_t debounce_ms_;

  bool prev_ = false;
  bool pressing_ = false;
  bool pressed_ = false;
  bool long_pressed_ = false;
  bool long_hold_ = false;
  bool long_hold_start_ = false;
  bool long_hold_start_triggered_ = false;

  bool last_raw_ = false;
  int64_t last_debounce_time_ = 0;
  int64_t pressed_at_ = 0;
};

////////////////////////////////////////////////////////////////////////////////

inline void Button::update() {
  const int64_t now = esp_timer_get_time() / 1000;
  bool raw = gpio_get_level(pin_) == 0;

  if (raw != last_raw_) {
    last_debounce_time_ = now;
    last_raw_ = raw;
  }

  if ((now - last_debounce_time_) < debounce_ms_) {
    return;
  }

  bool current = raw;
  pressed_ = false;
  long_pressed_ = false;
  pressing_ = current;

  if (current && !prev_) {
    pressed_at_ = now;
    long_hold_ = false;
    long_hold_start_triggered_ = false;
  } else if (!current && prev_) {
    uint32_t duration = now - pressed_at_;
    if (duration >= long_press_ms_) {
      long_pressed_ = true;
    } else {
      pressed_ = true;
    }
    pressed_at_ = 0;
    long_hold_ = false;
    long_hold_start_triggered_ = false;
  } else if (current && (now - pressed_at_ >= long_press_ms_)) {
    long_hold_ = true;
    if (!long_hold_start_triggered_) {
      long_hold_start_ = true;
      long_hold_start_triggered_ = true;
    } else {
      long_hold_start_ = false;
    }
  } else {
    long_hold_ = false;
    long_hold_start_ = false;
  }

  prev_ = current;
}
