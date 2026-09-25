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

#include "device_common/storage/nvs_store.h"
#include "device_common/system/app_log.h"

class IRRemote {
 public:
  static constexpr const int kRawDataBufferSize = 800;
  static constexpr const int kRawDataMinSize = 8;
  static constexpr const int kRawDataTimeoutUs = 40'000;
  static constexpr const int kIrFinalizingTimeoutUs = 100'000;
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

  static bool saveToStorage(NvsStore& prefs, const char* key,
                            const IRData& data);
  static bool loadFromStorage(NvsStore& prefs, const char* key,
                              IRData& data);

 private:
  enum class IrReceiverState {
    kIrReceiverOff,
    kIrReceiverStart,
    kIrReceiverReceiving,
    kIrReceiverWaitingBlank,
    kIrReceiverFinalizing,
    kIrReceiverAvailable,
  };

  gpio_num_t pin_tx_, pin_rx_;
  volatile IrReceiverState state_;
  uint16_t raw_index_;
  uint16_t raw_data_[kRawDataBufferSize];
  volatile uint64_t prev_us_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

  void isr();
  static void IRAM_ATTR isrEntryPoint(void* this_ptr);
};
