/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_intr_alloc.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "app_log.h"
#include "preferences.h"

class IRRemote {
 public:
  static constexpr const int RAW_DATA_BUFFER_SIZE = 800;
  static constexpr const int RAW_DATA_MIN_SIZE = 8;
  static constexpr const int RAW_DATA_TIMEOUT_US = 40'000;
  static constexpr const int IR_FINALIZING_TIMEOUT_US = 100'000;
  using IRDataElement = uint16_t;
  using IRData = std::vector<IRDataElement>;

  void begin(int tx, int rx);

  void clear();
  bool available();
  bool waitForAvailable(int timeout_ms = -1);
  IRData get();

  void send(const IRData& data);

  static void print(const IRData& data, const char* label = NULL);
  static bool isIrDataEqual(const IRData& a, const IRData& b,
                            float tolerance_percent = 50.0f);

  static bool saveToPreferences(Preferences& prefs, const char* key,
                                const IRData& data);
  static bool loadFromPreferences(Preferences& prefs, const char* key,
                                  IRData& data);

 private:
  enum class IR_RECEIVER_STATE {
    IR_RECEIVER_OFF,
    IR_RECEIVER_START,
    IR_RECEIVER_RECEIVING,
    IR_RECEIVER_WAITING_BLANK,
    IR_RECEIVER_FINALIZING,
    IR_RECEIVER_AVAILABLE,
  };

  gpio_num_t pin_tx_, pin_rx_;
  volatile IR_RECEIVER_STATE state_;
  uint16_t raw_index_;
  uint16_t raw_data_[RAW_DATA_BUFFER_SIZE];
  volatile uint64_t prev_us_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

  void isr();
  static void IRAM_ATTR isrEntryPoint(void* this_ptr);
};

////////////////////////////////////////////////////////////////////////////////

inline void IRRemote::begin(int tx, int rx) {
  pin_tx_ = static_cast<gpio_num_t>(tx);
  pin_rx_ = static_cast<gpio_num_t>(rx);
  state_ = IR_RECEIVER_STATE::IR_RECEIVER_START;

  gpio_config_t tx_cfg = {};
  tx_cfg.pin_bit_mask = 1ULL << pin_tx_;
  tx_cfg.mode = GPIO_MODE_OUTPUT;
  gpio_config(&tx_cfg);
  gpio_set_level(pin_tx_, 0);

  gpio_config_t rx_cfg = {};
  rx_cfg.pin_bit_mask = 1ULL << pin_rx_;
  rx_cfg.mode = GPIO_MODE_INPUT;
  rx_cfg.intr_type = GPIO_INTR_ANYEDGE;
  gpio_config(&rx_cfg);

  static bool isr_service_installed = false;
  if (!isr_service_installed) {
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    isr_service_installed = true;
  }
  gpio_isr_handler_add(pin_rx_, isrEntryPoint, this);
}

inline void IRRemote::clear() {
  LOGD("[IR] clear");
  state_ = IR_RECEIVER_STATE::IR_RECEIVER_START;
}

inline bool IRRemote::available() {
  portENTER_CRITICAL(&mux_);
  uint32_t diff = static_cast<uint32_t>(esp_timer_get_time() - prev_us_);
  portEXIT_CRITICAL(&mux_);
  switch (state_) {
    case IR_RECEIVER_STATE::IR_RECEIVER_OFF:
    case IR_RECEIVER_STATE::IR_RECEIVER_START:
    case IR_RECEIVER_STATE::IR_RECEIVER_AVAILABLE:
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_RECEIVING:
      if (diff > RAW_DATA_TIMEOUT_US)
        state_ = IR_RECEIVER_STATE::IR_RECEIVER_WAITING_BLANK;
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_WAITING_BLANK:
      if (diff > IR_FINALIZING_TIMEOUT_US)
        state_ = IR_RECEIVER_STATE::IR_RECEIVER_FINALIZING;
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_FINALIZING:
      if (raw_index_ < RAW_DATA_MIN_SIZE) {
        LOGD("[IR] Raw Data Size: %d (skipped)", raw_index_);
        state_ = IR_RECEIVER_STATE::IR_RECEIVER_START;
        break;
      } else if (raw_index_ >= RAW_DATA_BUFFER_SIZE) {
        LOGE("[IR] Raw Data Size: %d (overflow)", raw_index_);
        state_ = IR_RECEIVER_STATE::IR_RECEIVER_START;
        break;
      }
      LOGI("[IR] Raw Data Size: %d", raw_index_);
      state_ = IR_RECEIVER_STATE::IR_RECEIVER_AVAILABLE;
      break;
  }
  return state_ == IR_RECEIVER_STATE::IR_RECEIVER_AVAILABLE;
}

inline bool IRRemote::waitForAvailable(int timeout_ms) {
  const int64_t start = esp_timer_get_time() / 1000;
  while (!available()) {
    if (timeout_ms > 0 && esp_timer_get_time() / 1000 - start > timeout_ms) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}

inline IRRemote::IRData IRRemote::get() {
  return IRData{raw_data_, raw_data_ + raw_index_};
}

inline void IRRemote::isrEntryPoint(void* this_ptr) {
  static_cast<IRRemote*>(this_ptr)->isr();
}

inline void IRRemote::send(const IRData& data) {
  // The 8us/16us bit-bang loop below generates the IR carrier waveform
  // directly by hand-timing GPIO toggles; if ANY interrupt (Wi-Fi, BLE,
  // the FreeRTOS tick, ...) preempts it mid-pulse, that pulse stretches by
  // however long the interrupt took, which is enough to make a real IR
  // receiver fail to decode the signal even though this function completes
  // and reports success. Disabling only the RX GPIO's own interrupt (as a
  // prior version of this function did, to avoid disturbing Wi-Fi/BLE
  // timing) does NOT protect against this -- only a full critical section,
  // matching the original Arduino noInterrupts()/interrupts() this was
  // ported from, does.
  portENTER_CRITICAL(&mux_);
  {
    enum IR_RECEIVER_STATE state_cache = state_;
    state_ = IR_RECEIVER_STATE::IR_RECEIVER_OFF;
    for (uint16_t count = 0; count < data.size(); count++) {
      uint64_t us = esp_timer_get_time();
      uint16_t time = data[count];
      do {
        gpio_set_level(pin_tx_, !(count & 1));
        esp_rom_delay_us(8);
        gpio_set_level(pin_tx_, 0);
        esp_rom_delay_us(16);
      } while (int32_t(us + time - esp_timer_get_time()) > 0);
    }
    state_ = state_cache;
  }
  portEXIT_CRITICAL(&mux_);
  LOGD("[IR] Send OK (size: %zu)", data.size());
}

inline void IRRemote::isr() {
  portENTER_CRITICAL_ISR(&mux_);
  uint64_t us = esp_timer_get_time();
  uint32_t diff = static_cast<uint32_t>(us - prev_us_);

  switch (state_) {
    case IR_RECEIVER_STATE::IR_RECEIVER_OFF:
    case IR_RECEIVER_STATE::IR_RECEIVER_FINALIZING:
    case IR_RECEIVER_STATE::IR_RECEIVER_AVAILABLE:
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_START:
      raw_index_ = 0;
      state_ = IR_RECEIVER_STATE::IR_RECEIVER_RECEIVING;
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_RECEIVING:
      if (diff > RAW_DATA_TIMEOUT_US) {
        state_ = IR_RECEIVER_STATE::IR_RECEIVER_WAITING_BLANK;
        break;
      }
      if (raw_index_ < RAW_DATA_BUFFER_SIZE) raw_data_[raw_index_++] = diff;
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_WAITING_BLANK:
      if (diff > IR_FINALIZING_TIMEOUT_US)
        state_ = IR_RECEIVER_STATE::IR_RECEIVER_FINALIZING;
      break;
  }

  prev_us_ = us;
  portEXIT_CRITICAL_ISR(&mux_);
}

inline void IRRemote::print(const IRData& data, const char* label) {
  if (label)
    LOGI("[IR] Raw Data (size: %zu) %s", data.size(), label);
  else
    LOGI("[IR] Raw Data (size: %zu)", data.size());
  for (size_t i = 0; i < data.size(); ++i) {
    printf("%d", data[i]);
    if (i != data.size() - 1) printf(",");
  }
  printf("\n");
}

inline bool IRRemote::isIrDataEqual(const IRData& a, const IRData& b,
                                    float tolerance_percent) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    float expected = static_cast<float>(b[i]);
    float diff = std::abs(static_cast<float>(a[i]) - expected);
    float allowed = expected * (tolerance_percent / 100.0f);
    if (diff > allowed) {
      return false;
    }
  }
  return true;
}

inline bool IRRemote::saveToPreferences(Preferences& prefs, const char* key,
                                        const IRData& data) {
  print(data, key);
  size_t size = data.size() * sizeof(IRDataElement);
  return size == prefs.putBytes(key, data.data(), size);
}

inline bool IRRemote::loadFromPreferences(Preferences& prefs, const char* key,
                                          IRData& data) {
  size_t size = prefs.getBytesLength(key);
  if (size == 0) return false;
  data.resize(size / sizeof(IRDataElement));
  prefs.getBytes(key, data.data(), size);
  print(data, key);
  return true;
}
