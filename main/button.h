#pragma once
#include <Arduino.h>

class Button {
 public:
  Button(uint8_t pin, uint32_t longPressMs = 1000, uint32_t debounceMs = 20)
      : pin_(pin), long_press_ms_(longPressMs), debounce_ms_(debounceMs) {
    pinMode(pin_, INPUT_PULLUP);
  }

  void update() {
    unsigned long now = millis();
    bool raw = digitalRead(pin_) == LOW;

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

  bool pressing() const { return pressing_; }
  bool pressed() const { return pressed_; }
  bool longPressed() const { return long_pressed_; }
  bool longHold() const { return long_hold_; }
  bool longHoldStarted() const { return long_hold_start_; }

 private:
  const uint8_t pin_;
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
  unsigned long last_debounce_time_ = 0;
  unsigned long pressed_at_ = 0;
};
