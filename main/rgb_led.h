#pragma once
#include <Arduino.h>

class RgbLed {
 public:
  enum class Color { Off, Red, Green, Blue, Yellow, Cyan, Magenta, White };

  explicit RgbLed(uint8_t pin) : pin_(pin) {}

  void setBackground(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t total = r + g + b;
    if (total > 0) {
      float scale = 32.0f / total;
      r_ = uint8_t(r * scale);
      g_ = uint8_t(g * scale);
      b_ = uint8_t(b * scale);
    } else {
      r_ = g_ = b_ = 0;
    }
    rgbLedWrite(pin_, r_, g_, b_);
  }

  void setBackground(Color color) {
    switch (color) {
      case Color::Off:
        setBackground(0, 0, 0);
        break;
      case Color::Red:
        setBackground(32, 0, 0);
        break;
      case Color::Green:
        setBackground(0, 32, 0);
        break;
      case Color::Blue:
        setBackground(0, 0, 32);
        break;
      case Color::Yellow:
        setBackground(16, 16, 0);
        break;
      case Color::Cyan:
        setBackground(0, 16, 16);
        break;
      case Color::Magenta:
        setBackground(16, 0, 16);
        break;
      case Color::White:
        setBackground(11, 11, 10);  // 合計32になるよう調整
        break;
    }
  }

  void off() { setBackground(Color::Off); }

  void blinkOnce(Color color, uint16_t durationMs = 500) {
    blink_color_ = color;
    blink_duration_ = durationMs;
    blink_start_ = millis();
    blinking_ = true;
    setTemporary(color);
  }

  void update() {
    if (blinking_ && (millis() - blink_start_ >= blink_duration_)) {
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
  uint32_t blink_start_ = 0;
  uint16_t blink_duration_ = 0;
  Color blink_color_ = Color::Off;

  void setTemporary(Color color) {
    switch (color) {
      case Color::Off:
        rgbLedWrite(pin_, 0, 0, 0);
        break;
      case Color::Red:
        rgbLedWrite(pin_, 32, 0, 0);
        break;
      case Color::Green:
        rgbLedWrite(pin_, 0, 32, 0);
        break;
      case Color::Blue:
        rgbLedWrite(pin_, 0, 0, 32);
        break;
      case Color::Yellow:
        rgbLedWrite(pin_, 16, 16, 0);
        break;
      case Color::Cyan:
        rgbLedWrite(pin_, 0, 16, 16);
        break;
      case Color::Magenta:
        rgbLedWrite(pin_, 16, 0, 16);
        break;
      case Color::White:
        rgbLedWrite(pin_, 11, 11, 10);
        break;
    }
  }
};
