/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once
#include <esp_timer.h>
#include <led_strip.h>

#include "device_common/system/app_log.h"

class RgbLed {
 public:
  enum class Color {
    kOff,
    kRed,
    kGreen,
    kBlue,
    kYellow,
    kCyan,
    kMagenta,
    kWhite
  };

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

  void off() { setBackground(Color::kOff); }

  void blinkOnce(Color color, uint16_t duration_ms = 200) {
    blink_start_ = esp_timer_get_time() / 1000;
    blink_duration_ = duration_ms;
    blinking_ = true;
    setColor(color, /*is_background=*/false);
  }

  void update() {
    if (blinking_ &&
        esp_timer_get_time() / 1000 - blink_start_ >= blink_duration_) {
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
  int64_t blink_start_ = 0;
  uint16_t blink_duration_ = 0;

  void write(uint8_t r, uint8_t g, uint8_t b) {
    if (!strip_) return;
    led_strip_set_pixel(strip_, 0, r, g, b);
    led_strip_refresh(strip_);
  }

  void setColor(Color color, bool is_background) {
    uint8_t raw_r = 0, raw_g = 0, raw_b = 0;
    switch (color) {
      case Color::kOff:
        break;
      case Color::kRed:
        raw_r = 255;
        break;
      case Color::kGreen:
        raw_g = 255;
        break;
      case Color::kBlue:
        raw_b = 255;
        break;
      case Color::kYellow:
        raw_r = raw_g = 255;
        break;
      case Color::kCyan:
        raw_g = raw_b = 255;
        break;
      case Color::kMagenta:
        raw_r = raw_b = 255;
        break;
      case Color::kWhite:
        raw_r = raw_g = raw_b = 255;
        break;
    }

    uint8_t total = raw_r + raw_g + raw_b;
    uint8_t scaled_r = 0, scaled_g = 0, scaled_b = 0;
    if (total > 0) {
      float scale = 4.0f / total;
      scaled_r = static_cast<uint8_t>(raw_r * scale);
      scaled_g = static_cast<uint8_t>(raw_g * scale);
      scaled_b = static_cast<uint8_t>(raw_b * scale);
    }

    if (is_background) {
      r_ = scaled_r;
      g_ = scaled_g;
      b_ = scaled_b;
      if (!blinking_) {
        write(r_, g_, b_);
      }
    } else {
      write(scaled_r, scaled_g, scaled_b);
    }
  }
};
