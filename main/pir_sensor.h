#pragma once
#include <Arduino.h>

class PirSensor {
 public:
  explicit PirSensor(uint8_t pin, uint32_t clearDelayMs = 3000)
      : pin_(pin), clearDelayMs_(clearDelayMs) {
    pinMode(pin_, INPUT_PULLDOWN);
  }

  void update() {
    bool current = digitalRead(pin_) == HIGH;
    uint32_t now = millis();

    if (current) {
      lastMotionTime_ = now;
      motionActive_ = true;
    } else if (motionActive_ && (now - lastMotionTime_ > clearDelayMs_)) {
      motionActive_ = false;
    }
  }

  bool getMotionDetected() const { return motionActive_; }

 private:
  const uint8_t pin_;
  const uint32_t clearDelayMs_;

  uint32_t lastMotionTime_ = 0;
  bool motionActive_ = false;
};
