/**
 * @file servo_motor.h
 * @brief SG90 servo (50Hz) for ESP-IDF v5.5 / LEDC
 * - 初期フリー（PWM無出力）
 * - setTargetDegree(deg, speed_dps=0): 速度0=即時移動
 * - 目標到達後は200msキープしてから自動free()
 */
#pragma once
#include <math.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

class ServoMotor {
 public:
  ServoMotor() = default;

  // pin: GPIO, ch: LEDCチャネル(0..7), timer: LEDC_TIMER_0..3
  bool begin(int pin, int ch = 0, ledc_timer_t timer = LEDC_TIMER_0) {
    pin_ = pin;
    ch_ = ch;
    timer_ = timer;
    period_us_ = 1000000UL / kFreqHz;
    max_duty_ = (1UL << kResBits) - 1;

    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.timer_num = timer_;
    t.duty_resolution = (ledc_timer_bit_t)kResBits;
    t.freq_hz = kFreqHz;
    t.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&t) != ESP_OK) return false;

    current_ = target_ = 90.0f;
    speed_ = 0.0f;
    attached_ = false;  // 初期はフリー
    last_ms_ = 0;
    dwell_start_ms_ = 0;  // 停止中でない
    return true;
  }

  void free() {
    if (!attached_) return;
    ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch_, 0);
    gpio_reset_pin((gpio_num_t)pin_);
    attached_ = false;
    dwell_start_ms_ = 0;
  }

  // speed_dps==0 → 即時移動（200ms保持後にfree）
  // >0 → handle()で追従し、到達後200ms保持してfree
  void setTargetDegree(float deg, float speed_dps = 0.0f) {
    target_ = clamp_(deg, 0.0f, 180.0f);
    speed_ = fabsf(speed_dps);

    if (!attached_) {
      ledc_channel_config_t c = {};
      c.gpio_num = (gpio_num_t)pin_;
      c.speed_mode = LEDC_LOW_SPEED_MODE;
      c.channel = (ledc_channel_t)ch_;
      c.intr_type = LEDC_INTR_DISABLE;
      c.timer_sel = timer_;
      c.duty = 0;
      c.hpoint = 0;
      if (ledc_channel_config(&c) != ESP_OK) return;
      attached_ = true;
    }

    if (speed_ == 0.0f) {
      // 即時移動 → 200msキープ → free は handle() 側で実施
      current_ = target_;
      writeUs_(degToUs_(current_));
      dwell_start_ms_ = millis_();  // キープ開始
    } else {
      last_ms_ = millis_();
      dwell_start_ms_ = 0;  // まだ到達していない
    }
  }

  void handle() {
    uint32_t now = millis_();

    // 終点キープ中なら、200ms経過でfree
    if (attached_ && dwell_start_ms_ != 0) {
      if (now - dwell_start_ms_ >= dwell_ms_) {
        free();
      }
      return;  // キープ中は追加更新なし
    }

    if (!attached_ || speed_ <= 0.0f) return;

    float dt = (now - last_ms_) / 1000.0f;
    if (dt <= 0.0f) return;
    last_ms_ = now;

    float step = speed_ * dt;
    if (fabsf(target_ - current_) <= step) {
      // 到達 → 一発最終出力 → 200msキープ開始（freeはhandleが後で行う）
      current_ = target_;
      writeUs_(degToUs_(current_));
      speed_ = 0.0f;
      dwell_start_ms_ = now;
      return;
    }

    // 途中 → 追従更新
    current_ = (target_ > current_) ? (current_ + step) : (current_ - step);
    writeUs_(degToUs_(current_));
  }

  float readDegree() const { return current_; }
  bool attached() const { return attached_; }

 private:
  // 固定レンジ
  static constexpr uint16_t kMinUs = 500, kMaxUs = 2400;
  static constexpr uint32_t kFreqHz = 50;
  static constexpr uint8_t kResBits = 16;
  static constexpr uint32_t dwell_ms_ = 200;  // ← 終点での保持時間

  static inline float clamp_(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  }
  static inline uint16_t degToUs_(float deg) {
    float t = clamp_(deg, 0.0f, 180.0f) / 180.0f;
    return (uint16_t)lroundf(kMinUs + t * (kMaxUs - kMinUs));
  }
  static inline uint32_t millis_() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
  }

  void writeUs_(uint16_t us) {
    uint32_t duty = (uint32_t)((uint64_t)us * max_duty_ / period_us_);
    if (duty > max_duty_) duty = max_duty_;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch_, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch_);
  }

  int pin_ = -1, ch_ = 0;
  ledc_timer_t timer_ = LEDC_TIMER_0;
  bool attached_ = false;
  uint32_t period_us_ = 20000, max_duty_ = (1u << kResBits) - 1;
  float current_ = 90.0f, target_ = 90.0f, speed_ = 0.0f;
  uint32_t last_ms_ = 0;
  uint32_t dwell_start_ms_ = 0;  // 0=未キープ, 非0=キープ開始時刻
};
