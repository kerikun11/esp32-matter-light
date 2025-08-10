/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <Arduino.h>
#include <Preferences.h>

class IRRemote {
 public:
  static constexpr const int IR_RECEIVE_TIMEOUT_US = 100'000;
  static constexpr const int RAW_DATA_BUFFER_SIZE = 800;
  static constexpr const int RAW_DATA_MIN_SIZE = 8;
  using IRDataElement = uint16_t;
  using IRData = std::vector<IRDataElement>;

  void begin(int tx, int rx);

  void handle();
  void clear();
  bool available();
  bool waitForAvailable(int timeout_ms = -1);
  IRData get();

  void send(const IRData& data);

  static bool isIrDataEqual(const IRData& a, const IRData& b,
                            float tolerance_percent = 50.0f);

  static bool saveToPreferences(Preferences& prefs, const char* key,
                                const IRData& data);
  static bool loadFromPreferences(Preferences& prefs, const char* key,
                                  IRData& data);

 private:
  enum class IR_RECEIVER_STATE {
    IR_RECEIVER_OFF,
    IR_RECEIVER_READY,
    IR_RECEIVER_RECEIVING,
    IR_RECEIVER_READING,
    IR_RECEIVER_AVAILABLE,
  };

  int pin_tx_, pin_rx_;
  volatile enum IR_RECEIVER_STATE state_;
  uint16_t raw_index_;
  uint16_t raw_data_[RAW_DATA_BUFFER_SIZE];
  uint32_t prev_us_;

  void isr();
  static void IRAM_ATTR isrEntryPoint(void* this_ptr);
};

////////////////////////////////////////////////////////////////////////////////

void IRRemote::begin(int tx, int rx) {
  pin_tx_ = tx;
  pin_rx_ = rx;
  pinMode(pin_tx_, OUTPUT);
  pinMode(pin_rx_, INPUT);
  digitalWrite(pin_tx_, LOW);
  attachInterruptArg(pin_rx_, isrEntryPoint, this, CHANGE);
  state_ = IR_RECEIVER_STATE::IR_RECEIVER_READY;
}

void IRRemote::isrEntryPoint(void* this_ptr) {
  reinterpret_cast<IRRemote*>(this_ptr)->isr();
}

bool IRRemote::available() {
  return state_ == IR_RECEIVER_STATE::IR_RECEIVER_AVAILABLE;
}

bool IRRemote::waitForAvailable(int timeout_ms) {
  unsigned long start = millis();
  while (!available()) {
    if (timeout_ms > 0 && millis() - start > timeout_ms) {
      return false;
    }
    handle();
    delay(1);
  }
  return true;
}

IRRemote::IRData IRRemote::get() {
  return IRData{raw_data_, raw_data_ + raw_index_};
}

void IRRemote::clear() {
  state_ = IR_RECEIVER_STATE::IR_RECEIVER_READY;
  LOGI("[IR] clear");
}

void IRRemote::send(const IRData& data) {
  noInterrupts();
  {
    enum IR_RECEIVER_STATE state_cache = state_;
    state_ = IR_RECEIVER_STATE::IR_RECEIVER_OFF;
    for (uint16_t count = 0; count < data.size(); count++) {
      uint64_t us = micros();
      uint16_t time = data[count];
      do {
        digitalWrite(pin_tx_, !(count & 1));
        delayMicroseconds(8);
        digitalWrite(pin_tx_, 0);
        delayMicroseconds(16);
      } while (int32_t(us + time - micros()) > 0);
    }
    state_ = state_cache;
  }
  interrupts();
  LOGI("[IR] Send OK (size: %zu)", data.size());
}

void IRRemote::isr() {
  uint32_t us = micros();
  uint32_t diff = us - prev_us_;

  switch (state_) {
    case IR_RECEIVER_STATE::IR_RECEIVER_OFF:
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_READY:
      state_ = IR_RECEIVER_STATE::IR_RECEIVER_RECEIVING;
      raw_index_ = 0;
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_RECEIVING:
      while (diff > 0xFFFF) {
        if (raw_index_ > RAW_DATA_BUFFER_SIZE - 2) {
          break;
        }
        raw_data_[raw_index_++] = 0xFFFF;
        raw_data_[raw_index_++] = 0;
        diff -= 0xFFFF;
      }
      if (raw_index_ > RAW_DATA_BUFFER_SIZE - 1) {
        break;
      }
      raw_data_[raw_index_++] = diff;
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_READING:
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_AVAILABLE:
      break;
  }

  prev_us_ = us;
}

void IRRemote::handle() {
  noInterrupts();
  uint32_t diff = micros() - prev_us_;
  interrupts();

  switch (state_) {
    case IR_RECEIVER_STATE::IR_RECEIVER_OFF:
    case IR_RECEIVER_STATE::IR_RECEIVER_READY:
    case IR_RECEIVER_STATE::IR_RECEIVER_AVAILABLE:
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_RECEIVING:
      if (diff > IR_RECEIVE_TIMEOUT_US) {
        state_ = IR_RECEIVER_STATE::IR_RECEIVER_READING;
        LOGI("[IR] End Receiving");
      }
      break;
    case IR_RECEIVER_STATE::IR_RECEIVER_READING:
      if (raw_index_ < RAW_DATA_MIN_SIZE) {
        LOGI("[IR] Raw Data Size: %d (skipped)", raw_index_);
        state_ = IR_RECEIVER_STATE::IR_RECEIVER_READY;
        break;
      } else if (raw_index_ >= RAW_DATA_BUFFER_SIZE) {
        LOGE("[IR] Raw Data Size: %d (overflow)", raw_index_);
        state_ = IR_RECEIVER_STATE::IR_RECEIVER_READY;
        break;
      }
      LOGI("[IR] Raw Data Size: %d", raw_index_);
      for (int i = 0; i < raw_index_; i++) {
        printf("%d", raw_data_[i]);
        if (i != raw_index_ - 1) printf(",");
      }
      printf("\n");
      state_ = IR_RECEIVER_STATE::IR_RECEIVER_AVAILABLE;
      break;
  }
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

bool IRRemote::saveToPreferences(Preferences& prefs, const char* key,
                                 const IRData& data) {
  size_t size = data.size() * sizeof(IRDataElement);
  return size == prefs.putBytes(key, data.data(), size);
}

bool IRRemote::loadFromPreferences(Preferences& prefs, const char* key,
                                   IRData& data) {
  size_t size = prefs.getBytesLength(key);
  if (size == 0) return false;
  data.resize(size / sizeof(IRDataElement));
  prefs.getBytes(key, data.data(), size);
  return true;
}
