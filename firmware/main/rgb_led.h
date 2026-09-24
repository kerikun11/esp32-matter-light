/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once
#include <esp_timer.h>
#include <led_strip.h>

#include "app_log.h"

class RgbLed {
 public:
  enum class Color { Off, Red, Green, Blue, Yellow, Cyan, Magenta, White };

  explicit RgbLed(int pin) {
    led_strip_config_t strip_cfg = {};
    strip_cfg.strip_gpio_num = pin;
    strip_cfg.max_leds = 1;
    strip_cfg.led_model = LED_MODEL_WS2812;
    strip_cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmt_cfg = {};
    rmt_cfg.resolution_hz = 10 * 1000 * 1000;  // 10MHz, matches WS2812 timing
    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip_) != ESP_OK) {
      LOGE("[RgbLed] led_strip_new_rmt_device failed for GPIO%d", pin);
    }
  }

  void setBackground(Color color) { setColor(color, /*is_background=*/true); }

  void off() { setBackground(Color::Off); }

  void blinkOnce(Color color, uint16_t durationMs = 200) {
    blinkStart_ = esp_timer_get_time() / 1000;
    blinkDuration_ = durationMs;
    blinking_ = true;
    setColor(color, /*is_background=*/false);
  }

  void update() {
    if (blinking_ &&
        esp_timer_get_time() / 1000 - blinkStart_ >= blinkDuration_) {
      write(r_, g_, b_);
      blinking_ = false;
    }
  }

 private:
  led_strip_handle_t strip_ = nullptr;
  uint8_t r_ = 0;
  uint8_t g_ = 0;
  uint8_t b_ = 0;

  bool blinking_ = false;
  int64_t blinkStart_ = 0;
  uint16_t blinkDuration_ = 0;

  void write(uint8_t r, uint8_t g, uint8_t b) {
    if (!strip_) return;
    led_strip_set_pixel(strip_, 0, r, g, b);
    led_strip_refresh(strip_);
  }

  void setColor(Color color, bool is_background) {
    uint8_t rawR = 0, rawG = 0, rawB = 0;
    switch (color) {
      case Color::Off:
        break;
      case Color::Red:
        rawR = 255;
        break;
      case Color::Green:
        rawG = 255;
        break;
      case Color::Blue:
        rawB = 255;
        break;
      case Color::Yellow:
        rawR = rawG = 255;
        break;
      case Color::Cyan:
        rawG = rawB = 255;
        break;
      case Color::Magenta:
        rawR = rawB = 255;
        break;
      case Color::White:
        rawR = rawG = rawB = 255;
        break;
    }

    uint8_t total = rawR + rawG + rawB;
    uint8_t scaledR = 0, scaledG = 0, scaledB = 0;
    if (total > 0) {
      float scale = 4.0f / total;
      scaledR = static_cast<uint8_t>(rawR * scale);
      scaledG = static_cast<uint8_t>(rawG * scale);
      scaledB = static_cast<uint8_t>(rawB * scale);
    }

    if (is_background) {
      r_ = scaledR;
      g_ = scaledG;
      b_ = scaledB;
      if (!blinking_) {
        write(r_, g_, b_);
      }
    } else {
      write(scaledR, scaledG, scaledB);
    }
  }
};
