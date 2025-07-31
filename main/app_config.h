/**
 * @copyright 2025 Ryotaro Onuki
 * SPDX-License-Identifier: LGPL-2.1
 */
#pragma once

/* Pins */
#if CONFIG_IDF_TARGET_ESP32S3
#define PIN_BUTTON BOOT_PIN
#define PIN_MOTION_SENSOR 1
#define PIN_LIGHT_SENSOR 2
#define PIN_IR_TRANSMITTER 4
#define PIN_IR_RECEIVER 5
#elif CONFIG_IDF_TARGET_ESP32C6
// https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html
#define PIN_BUTTON 5
#define PIN_MOTION_SENSOR 18
#define PIN_LIGHT_SENSOR 4  //< ADC (0-6)
#define PIN_IR_TRANSMITTER 6
#define PIN_IR_RECEIVER 14
#else
#error "unsupported target"
#endif

/* Timeout */
#define LIGHT_OFF_TIMEOUT_SECONDS_DEFAULT (5 * 60)  // 5 minutes
#define OCCUPANCY_TIMEOUT_SECONDS 3
