#pragma once
#include <Arduino.h>

class RgbLed {
 public:
  enum class Color { Off, Red, Green, Blue, Yellow, Cyan, Magenta, White };

  explicit RgbLed(uint8_t pin) : pin_(pin) {}

  void setBackground(Color color) {
    setColor(color, /*is_background=*/true);
  }

  void off() {
    setBackground(Color::Off);
  }

  void blinkOnce(Color color, uint16_t durationMs = 500) {
    blinkStart_ = millis();
    blinkDuration_ = durationMs;
    blinking_ = true;
    setColor(color, /*is_background=*/false);
  }

  void update() {
    if (blinking_ && millis() - blinkStart_ >= blinkDuration_) {
      rgbLedWrite(pin_, r_, g_, b_);
      blinking_ = false;
    }
  }

 private:
  const uint8_t pin_;
  uint8_t r_ = 0;
  uint8_t g_ = 0;
  uint8_t b_ = 0;

  bool blinking_ = false;
  uint32_t blinkStart_ = 0;
  uint16_t blinkDuration_ = 0;

  void setColor(Color color, bool is_background) {
    uint8_t rawR = 0, rawG = 0, rawB = 0;
    switch (color) {
      case Color::Off:     break;
      case Color::Red:     rawR = 255; break;
      case Color::Green:   rawG = 255; break;
      case Color::Blue:    rawB = 255; break;
      case Color::Yellow:  rawR = rawG = 255; break;
      case Color::Cyan:    rawG = rawB = 255; break;
      case Color::Magenta: rawR = rawB = 255; break;
      case Color::White:   rawR = rawG = rawB = 255; break;
    }

    uint8_t total = rawR + rawG + rawB;
    uint8_t scaledR = 0, scaledG = 0, scaledB = 0;
    if (total > 0) {
      float scale = 32.0f / total;
      scaledR = static_cast<uint8_t>(rawR * scale);
      scaledG = static_cast<uint8_t>(rawG * scale);
      scaledB = static_cast<uint8_t>(rawB * scale);
    }

    if (is_background) {
      r_ = scaledR;
      g_ = scaledG;
      b_ = scaledB;
      if (!blinking_) {
        rgbLedWrite(pin_, r_, g_, b_);
      }
    } else {
      rgbLedWrite(pin_, scaledR, scaledG, scaledB);
    }
  }
};
