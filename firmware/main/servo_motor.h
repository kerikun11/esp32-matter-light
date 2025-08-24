/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once
#include <Arduino.h>
#include <math.h>
#include <stdint.h>

class ServoMotor {
 public:
  ServoMotor() = default;

  // pin: サーボ信号ピン
  // pwr_pin: サーボ電源EN(未使用は-1)
  // pwr_on_level: ENのON論理（true=HIGHでON）
  bool begin(int pin, int pwr_pin = -1, bool pwr_on_level = true) {
    pin_ = pin;
    pwr_pin_ = pwr_pin;
    pwr_on_level_ = pwr_on_level;

    period_us_ = 1000000UL / kFreqHz;   // 20000us
    max_duty_ = (1UL << kResBits) - 1;  // 16bit

    // LEDC 初期化（pinベース, ch自動割当）
    if (!ledcAttach((uint8_t)pin_, kFreqHz, kResBits)) return false;
    ledcWrite((uint8_t)pin_, 0);  // Low固定（逆給電抑制）

    // 電源GPIO初期状態: OFF
    if (pwr_pin_ >= 0) {
      pinMode(pwr_pin_, OUTPUT);
      digitalWrite(pwr_pin_, pwr_on_level_ ? LOW : HIGH);
      powered_ = false;
    }

    current_ = target_ = 90.0f;
    speed_ = 0.0f;
    last_ms_ = 0;
    start_hold_until_ms_ = 0;
    end_hold_until_ms_ = 0;
    return true;
  }

  // 電源OFFでフリー（LEDCは維持、出力=Low(duty=0)）
  void free() {
    ledcWrite((uint8_t)pin_, 0);  // 出力Low維持
    if (pwr_pin_ >= 0) {
      digitalWrite(pwr_pin_, pwr_on_level_ ? LOW : HIGH);  // 電源OFF
      powered_ = false;
    }
    start_hold_until_ms_ = 0;
    end_hold_until_ms_ = 0;
  }

  // speed_dps==0: 即時移動→(終端)100ms保持→free()
  // >0: 駆動開始前に100ms保持→追従→到達後100ms保持→free()
  void setTargetDegree(float deg, float speed_dps = 0.0f) {
    target_ = clamp_(deg, 0.0f, 180.0f);
    speed_ = fabsf(speed_dps);

    ensurePowerOn_();  // 電源ON & 100ms安定待ち

    const uint32_t now = millis();

    if (speed_ == 0.0f) {
      // 即時移動 → 終端100ms保持
      current_ = target_;
      writeUs_(degToUs_(current_));
      end_hold_until_ms_ = now + kHoldMs;  // 100ms後に handle()→free()
      start_hold_until_ms_ = 0;
    } else {
      // 駆動開始前100ms保持（現角のまま）
      writeUs_(degToUs_(current_));
      start_hold_until_ms_ = now + kHoldMs;
      end_hold_until_ms_ = 0;
      last_ms_ = now;  // ここからdt計算開始
    }
  }

  void handle() {
    const uint32_t now = millis();

    // 開始前保持中
    if (start_hold_until_ms_ != 0) {
      if ((int32_t)(now - start_hold_until_ms_) >= 0) {
        start_hold_until_ms_ = 0;  // 保持終了 → 以後移動開始
        last_ms_ = now;
      }
      return;
    }

    // 到達後保持中（100ms経過でfree）
    if (end_hold_until_ms_ != 0) {
      if ((int32_t)(now - end_hold_until_ms_) >= 0) {
        end_hold_until_ms_ = 0;
        free();
      }
      return;
    }

    // 速度0なら何もしない
    if (speed_ <= 0.0f) return;

    float dt = (now - last_ms_) / 1000.0f;
    if (dt <= 0.0f) return;
    last_ms_ = now;

    float step = speed_ * dt;
    if (fabsf(target_ - current_) <= step) {
      // 到達 → 一発最終出力 → 100ms保持開始
      current_ = target_;
      writeUs_(degToUs_(current_));
      speed_ = 0.0f;
      end_hold_until_ms_ = now + kHoldMs;
      return;
    }

    // 途中追従
    current_ = (target_ > current_) ? (current_ + step) : (current_ - step);
    writeUs_(degToUs_(current_));
  }

  float readDegree() const { return current_; }
  bool powered() const { return powered_; }

 private:
  // 定数
  static constexpr uint16_t kMinUs = 500, kMaxUs = 2400;
  static constexpr uint32_t kFreqHz = 50;
  static constexpr uint8_t kResBits = 16;
  static constexpr uint32_t kHoldMs = 100;          // 開始/終了の保持時間
  static constexpr uint32_t kPowerOnDelayMs = 100;  // 電源投入後の安定待ち

  static inline float clamp_(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  }
  static inline uint16_t degToUs_(float deg) {
    float t = clamp_(deg, 0.0f, 180.0f) / 180.0f;
    return (uint16_t)lroundf(kMinUs + t * (kMaxUs - kMinUs));
  }

  void writeUs_(uint16_t us) {
    uint32_t duty = (uint32_t)((uint64_t)us * max_duty_ / period_us_);
    if (duty > max_duty_) duty = max_duty_;
    ledcWrite((uint8_t)pin_, duty);
  }

  void ensurePowerOn_() {
    if (pwr_pin_ >= 0 && !powered_) {
      ledcWrite((uint8_t)pin_, 0);  // 先にLow維持（逆給電抑制）
      digitalWrite(pwr_pin_, pwr_on_level_ ? HIGH : LOW);  // 電源ON
      powered_ = true;
      delay(kPowerOnDelayMs);  // 100ms
    }
  }

  int pin_ = -1;
  int pwr_pin_ = -1;
  bool pwr_on_level_ = true;

  bool powered_ = false;

  uint32_t period_us_ = 20000;
  uint32_t max_duty_ = (1u << kResBits) - 1;

  float current_ = 90.0f, target_ = 90.0f, speed_ = 0.0f;
  uint32_t last_ms_ = 0;
  uint32_t start_hold_until_ms_ = 0;  // 駆動開始前100ms保持の期限
  uint32_t end_hold_until_ms_ = 0;    // 到達後100ms保持の期限
};
