#pragma once
#include <Arduino.h>

#include <vector>

class IRTransmitter {
 public:
  explicit IRTransmitter(uint8_t pin, uint32_t carrier = 38000)
      : pin_(pin), carrier_(carrier) {
    ledcAttach(pin_, carrier_, 8);  // resolution = 8bit (duty 0–255)
    ledcWrite(pin_, 0);             // 初期OFF
  }

  void sendRaw(const std::vector<uint16_t>& durations_us) {
    bool mark = true;
    for (uint16_t duration_us : durations_us) {
      ledcWrite(pin_, mark ? 128 : 0);  // 50% or 0% duty
      delayMicroseconds(duration_us);
      mark = !mark;
    }
    ledcWrite(pin_, 0);  // 最後にOFF
  }

 private:
  uint8_t pin_;
  uint32_t carrier_;
};
