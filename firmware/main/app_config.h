/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

/* Pin Assign */
#if CONFIG_IDF_TARGET_ESP32S3

#define CONFIG_APP_PIN_BUTTON BOOT_PIN  //< 0
#define CONFIG_APP_PIN_MOTION_SENSOR 1
#define CONFIG_APP_PIN_LIGHT_SENSOR 2  //< ADC
#define CONFIG_APP_PIN_IR_TRANSMITTER 4
#define CONFIG_APP_PIN_IR_RECEIVER 5
#define CONFIG_APP_PIN_RGB_LED PIN_RGB_LED  //< 48 (defined in pins_arduino.h)

#elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6
#if 0
// ESP32-C6 DevKitC-1
// https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html
#define CONFIG_APP_PIN_LIGHT_SENSOR 5  //< ADC (0-6)
#define CONFIG_APP_PIN_IR_TRANSMITTER 6
#define CONFIG_APP_PIN_IR_RECEIVER 7
#define CONFIG_APP_PIN_MOTION_SENSOR 4
#define CONFIG_APP_PIN_BUTTON BOOT_PIN      //< 9
#define CONFIG_APP_PIN_RGB_LED PIN_RGB_LED  //< 8 (defined in pins_arduino.h)

#else
// Seeed Studio XIAO ESP32-C6
// https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/#hardware-overview
#define CONFIG_APP_PIN_LIGHT_SENSOR 0  //< ADC (0-6)
#define CONFIG_APP_PIN_IR_TRANSMITTER 21
#define CONFIG_APP_PIN_IR_RECEIVER 20
#define CONFIG_APP_PIN_MOTION_SENSOR 19
#define CONFIG_APP_PIN_BUTTON 18
#define CONFIG_APP_PIN_RGB_LED 17
#endif

#else
#error "unsupported target"
#endif

/* Hostname */
#define CONFIG_APP_HOSTNAME_DEFAULT "esp32-matter-light"

/* Timeout */
#define CONFIG_APP_OCCUPANCY_TIMEOUT_SECONDS 3                 // 3 seconds
#define CONFIG_APP_LIGHT_OFF_TIMEOUT_SECONDS_DEFAULT (5 * 60)  // 5 minutes
