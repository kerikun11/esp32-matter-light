/**
 * @copyright 2025 Ryotaro Onuki
 * @license LGPL-2.1
 */
#pragma once
#include <Arduino.h>

class BrightnessSensor {
 public:
  explicit BrightnessSensor(uint8_t pin) : pin_(pin) {}

  void update(float threshold = 0.5f, float hysteresis = 0.1f) {
    int raw = analogRead(pin_);
    float value = constrain(static_cast<float>(raw) / 1023.0f, 0.0f, 1.0f);

    bool new_bright;
    if (was_bright_) {
      new_bright = (value >= threshold - hysteresis);
    } else {
      new_bright = (value >= threshold + hysteresis);
    }

    if (new_bright != was_bright_) {
      last_change_millis_ = millis();
      was_bright_ = new_bright;
    }

    is_bright_ = new_bright;
    normalized_value_ = value;
  }

  float getNormalized() const { return normalized_value_; }

  bool isBright() const { return is_bright_; }

  unsigned long getMillisSinceChange() const {
    return millis() - last_change_millis_;
  }

 private:
  const uint8_t pin_;
  float normalized_value_ = 0.0f;
  bool is_bright_ = false;
  bool was_bright_ = false;
  unsigned long last_change_millis_ = 0;
};
