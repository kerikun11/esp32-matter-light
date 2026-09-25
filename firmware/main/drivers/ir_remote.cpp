#include "drivers/ir_remote.h"

////////////////////////////////////////////////////////////////////////////////

void IRRemote::begin(int tx, int rx) {
  pin_tx_ = static_cast<gpio_num_t>(tx);
  pin_rx_ = static_cast<gpio_num_t>(rx);
  state_ = IrReceiverState::kIrReceiverStart;

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

void IRRemote::clear() {
  LOGD("[IR] clear");
  state_ = IrReceiverState::kIrReceiverStart;
}

bool IRRemote::available() {
  portENTER_CRITICAL(&mux_);
  uint32_t diff = static_cast<uint32_t>(esp_timer_get_time() - prev_us_);
  portEXIT_CRITICAL(&mux_);
  switch (state_) {
    case IrReceiverState::kIrReceiverOff:
    case IrReceiverState::kIrReceiverStart:
    case IrReceiverState::kIrReceiverAvailable:
      break;
    case IrReceiverState::kIrReceiverReceiving:
      if (diff > kRawDataTimeoutUs)
        state_ = IrReceiverState::kIrReceiverWaitingBlank;
      break;
    case IrReceiverState::kIrReceiverWaitingBlank:
      if (diff > kIrFinalizingTimeoutUs)
        state_ = IrReceiverState::kIrReceiverFinalizing;
      break;
    case IrReceiverState::kIrReceiverFinalizing:
      if (raw_index_ < kRawDataMinSize) {
        LOGD("[IR] Raw Data Size: %d (skipped)", raw_index_);
        state_ = IrReceiverState::kIrReceiverStart;
        break;
      } else if (raw_index_ >= kRawDataBufferSize) {
        LOGE("[IR] Raw Data Size: %d (overflow)", raw_index_);
        state_ = IrReceiverState::kIrReceiverStart;
        break;
      }
      LOGI("[IR] Raw Data Size: %d", raw_index_);
      state_ = IrReceiverState::kIrReceiverAvailable;
      break;
  }
  return state_ == IrReceiverState::kIrReceiverAvailable;
}

bool IRRemote::waitForAvailable(int timeout_ms) {
  const int64_t start = esp_timer_get_time() / 1000;
  while (!available()) {
    if (timeout_ms > 0 && esp_timer_get_time() / 1000 - start > timeout_ms) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}

IRRemote::IRData IRRemote::get() {
  return IRData{raw_data_, raw_data_ + raw_index_};
}

void IRRemote::isrEntryPoint(void* this_ptr) {
  static_cast<IRRemote*>(this_ptr)->isr();
}

void IRRemote::send(const IRData& data) {
  // The 8us/16us bit-bang loop below generates the IR carrier waveform
  // directly by hand-timing GPIO toggles; if ANY interrupt (Wi-Fi, BLE,
  // the FreeRTOS tick, ...) preempts it mid-pulse, that pulse stretches by
  // however long the interrupt took, which is enough to make a real IR
  // receiver fail to decode the signal even though this function completes
  // and reports success. Disabling only the RX GPIO's own interrupt (as a
  // prior version of this function did, to avoid disturbing Wi-Fi/BLE
  // timing) does not protect the waveform; use a full critical section.
  portENTER_CRITICAL(&mux_);
  {
    enum IrReceiverState state_cache = state_;
    state_ = IrReceiverState::kIrReceiverOff;
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

void IRRemote::isr() {
  portENTER_CRITICAL_ISR(&mux_);
  uint64_t us = esp_timer_get_time();
  uint32_t diff = static_cast<uint32_t>(us - prev_us_);

  switch (state_) {
    case IrReceiverState::kIrReceiverOff:
    case IrReceiverState::kIrReceiverFinalizing:
    case IrReceiverState::kIrReceiverAvailable:
      break;
    case IrReceiverState::kIrReceiverStart:
      raw_index_ = 0;
      state_ = IrReceiverState::kIrReceiverReceiving;
      break;
    case IrReceiverState::kIrReceiverReceiving:
      if (diff > kRawDataTimeoutUs) {
        state_ = IrReceiverState::kIrReceiverWaitingBlank;
        break;
      }
      if (raw_index_ < kRawDataBufferSize) raw_data_[raw_index_++] = diff;
      break;
    case IrReceiverState::kIrReceiverWaitingBlank:
      if (diff > kIrFinalizingTimeoutUs)
        state_ = IrReceiverState::kIrReceiverFinalizing;
      break;
  }

  prev_us_ = us;
  portEXIT_CRITICAL_ISR(&mux_);
}

void IRRemote::print(const IRData& data, const char* label) {
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

bool IRRemote::isIrDataEqual(const IRData& a, const IRData& b,
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

bool IRRemote::saveToStorage(NvsStore& prefs, const char* key,
                             const IRData& data) {
  print(data, key);
  size_t size = data.size() * sizeof(IRDataElement);
  return size == prefs.writeBlob(key, data.data(), size);
}

bool IRRemote::loadFromStorage(NvsStore& prefs, const char* key,
                               IRData& data) {
  size_t size = prefs.blobSize(key);
  if (size == 0) return false;
  data.resize(size / sizeof(IRDataElement));
  prefs.readBlob(key, data.data(), size);
  print(data, key);
  return true;
}
