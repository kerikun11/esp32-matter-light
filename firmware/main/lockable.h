/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 *
 * A tiny RAII mutex. Needed because the settings/IR-data/web state used to
 * be touched only from the single Arduino loop() task; now that the on-device
 * HTTP server (esp_http_server) runs its handlers on its own worker task,
 * that same state is read and written from two real FreeRTOS tasks and needs
 * explicit synchronization.
 *
 * Recursive because SmartLightController::handle() holds this mutex across
 * calls into SmartLightWeb methods that lock the very same mutex (it's
 * shared by reference) -- a plain mutex would deadlock on that reentry.
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
