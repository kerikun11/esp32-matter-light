/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once
#include <Arduino.h>

#include <climits>

class MotionSensor {
 public:
  explicit MotionSensor(uint8_t pin) : pin_(pin) {
    pinMode(pin_, INPUT_PULLDOWN);
  }

  void update() {
    if (digitalRead(pin_) == HIGH) {
      last_motion_time_ms_ = millis();
      seen_motion_ = true;
    }
  }

  int getSecondsSinceLastMotion() const {
    if (!seen_motion_) return INT_MAX;
    return (millis() - last_motion_time_ms_) / 1000;
  }

  bool isOccupied(int timeout_seconds) const {
    return getSecondsSinceLastMotion() < timeout_seconds;
  }

 private:
  const uint8_t pin_;
  unsigned long last_motion_time_ms_ = 0;
  bool seen_motion_ = false;
};
