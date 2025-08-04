/**
 * @copyright 2025 Ryotaro Onuki
 * SPDX-License-Identifier: LGPL-2.1
 */
#pragma once

/* Pin Assign */
#if CONFIG_IDF_TARGET_ESP32S3

#define PIN_BUTTON BOOT_PIN  //< 0
#define PIN_MOTION_SENSOR 1
#define PIN_LIGHT_SENSOR 2  //< ADC
#define PIN_IR_TRANSMITTER 4
#define PIN_IR_RECEIVER 5
// #define PIN_RGB_LED 48  //< defined in pins_arduino.h

#elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6
#if 0
// ESP32-C6 DevKitC-1
// https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html
#define PIN_BUTTON BOOT_PIN  //< 9
#define PIN_MOTION_SENSOR 4
#define PIN_LIGHT_SENSOR 5  //< ADC (0-6)
#define PIN_IR_TRANSMITTER 6
#define PIN_IR_RECEIVER 7
// #define PIN_RGB_LED 8  //< defined in pins_arduino.h

#else
// Seeed Studio XIAO ESP32-C6
// https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/#hardware-overview
#define PIN_LIGHT_SENSOR 0  //< ADC (0-6)
#define PIN_IR_TRANSMITTER 21
#define PIN_IR_RECEIVER 20
#define PIN_MOTION_SENSOR 19
#define PIN_BUTTON 18
#define PIN_RGB_LED 17
#endif

#else
#error "unsupported target"
#endif

/* Timeout */
#define OCCUPANCY_TIMEOUT_SECONDS 3                   // 3 seconds
#define LIGHT_OFF_TIMEOUT_SECONDS_DEFAULT (5 * 60)    // 5 minutes
#define MOTION_SWITCH_AUTO_ON_SECONDS (10 * 60 * 60)  // 10 hours
