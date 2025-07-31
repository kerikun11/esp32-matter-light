/**
 * @copyright 2025 Ryotaro Onuki
 * @license LGPL-2.1
 */
#pragma once

#include <Arduino.h>

class IRRemote {
 public:
  static constexpr const int IR_RECEIVE_TIMEOUT_US = 200'000;
  static constexpr const int RAW_DATA_BUFFER_SIZE = 800;
  static constexpr const int RAW_DATA_MIN_SIZE = 8;

  void begin(int tx, int rx);

  void handle();
  bool available();
  std::vector<uint16_t> get();
  void clear();

  void send(const std::vector<uint16_t>& data);

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
  ledcAttach(tx, 38000, 8);
  ledcWrite(tx, 0);
  pinMode(rx, INPUT);
  attachInterruptArg(rx, isrEntryPoint, this, CHANGE);
  state_ = IR_RECEIVER_STATE::IR_RECEIVER_READY;
}

void IRRemote::isrEntryPoint(void* this_ptr) {
  reinterpret_cast<IRRemote*>(this_ptr)->isr();
}

bool IRRemote::available() {
  return state_ == IR_RECEIVER_STATE::IR_RECEIVER_AVAILABLE;
}

std::vector<uint16_t> IRRemote::get() {
  return std::vector<uint16_t>{raw_data_, raw_data_ + raw_index_};
}

void IRRemote::clear() {
  state_ = IR_RECEIVER_STATE::IR_RECEIVER_READY;
  LOGI("[IR] clear");
}

void IRRemote::send(const std::vector<uint16_t>& data) {
  noInterrupts();
  {
    enum IR_RECEIVER_STATE state_cache = state_;
    state_ = IR_RECEIVER_STATE::IR_RECEIVER_OFF;
    bool mark = true;
    for (uint16_t duration_us : data) {
      ledcWrite(pin_tx_, mark ? 128 : 0);
      delayMicroseconds(duration_us);
      mark = !mark;
    }
    ledcWrite(pin_tx_, 0);
    state_ = state_cache;
  }
  interrupts();
  LOGI("[IR] Send OK");
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
      }
      if (raw_index_ >= RAW_DATA_BUFFER_SIZE) {
        LOGE("[IR] Raw Data Size: %d (overflow)", raw_index_);
        state_ = IR_RECEIVER_STATE::IR_RECEIVER_READY;
        break;
      }
      LOGI("[IR] Raw Data:");
      for (int i = 0; i < raw_index_; i++) {
        printf("%d", raw_data_[i]);
        if (i != raw_index_ - 1) printf(",");
      }
      printf("\n");
      state_ = IR_RECEIVER_STATE::IR_RECEIVER_AVAILABLE;
      break;
  }
}
