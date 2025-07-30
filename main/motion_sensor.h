/**
 * @copyright 2025 Ryotaro Onuki
 * @license LGPL-2.1
 */
#pragma once
#include <Arduino.h>

class MotionSensor {
 public:
  explicit MotionSensor(uint8_t pin) : pin_(pin) {
    pinMode(pin_, INPUT_PULLDOWN);
  }
  int getSecondsSinceLastMotion() {
    if (digitalRead(pin_) == HIGH) last_motion_time_ms_ = millis();
    return (millis() - last_motion_time_ms_) / 1000;
  }

 private:
  const uint8_t pin_;
  unsigned long last_motion_time_ms_ = INT_MAX;
};
