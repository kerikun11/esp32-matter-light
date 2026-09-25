/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 *
 * Recursive FreeRTOS mutex shared by the app and HTTP worker tasks.
 * Recursive locking permits an app transaction to call HTTP state helpers.
 */
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Mutex {
 public:
  Mutex() : handle_(xSemaphoreCreateRecursiveMutex()) {}
  ~Mutex() { vSemaphoreDelete(handle_); }
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;

  operator SemaphoreHandle_t() const { return handle_; }

 private:
  SemaphoreHandle_t handle_;
};

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
  }
  ~Lock() { xSemaphoreGiveRecursive(mutex_); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;

 private:
  SemaphoreHandle_t mutex_;
};
