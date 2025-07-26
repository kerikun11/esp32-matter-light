#pragma once
#include <Arduino.h>

class LightSensor {
 public:
  explicit LightSensor(uint8_t pin) : pin_(pin) {}

  void update() {
    int raw = analogRead(pin_);
    normalizedValue_ = constrain(static_cast<float>(raw) / 1023.0f, 0.0f, 1.0f);
  }

  float getNormalized() const { return normalizedValue_; }

 private:
  const uint8_t pin_;
  float normalizedValue_ = 0.0f;
};
