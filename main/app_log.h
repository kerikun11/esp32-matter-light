/**
 * @copyright 2025 Ryotaro Onuki
 * SPDX-License-Identifier: LGPL-2.1
 */
#pragma once

#include <Arduino.h>

/* app log level (0: None, 1: Error, 2: Warn, 3: Info, 4: Debug) */
#ifndef APP_LOG_LEVEL
#define APP_LOG_LEVEL 3
#endif

/* app log utils */
#define APP_LOG_STRINGIFY(n) #n
#define APP_LOG_TO_STRING(n) APP_LOG_STRINGIFY(n)
/* app log base */
#define APP_LOG_BASE(l, c, f, ...)                                \
  do {                                                            \
    const auto us = micros();                                     \
    fprintf(stdout,                                               \
            c "[" l "][%d.%06d][" __FILE__                        \
              ":" APP_LOG_TO_STRING(__LINE__) "]\e[0m " f "\n",   \
            int(us / 1000000), int(us % 1000000), ##__VA_ARGS__); \
  } while (0)

/* app log definitions for use */
#if APP_LOG_LEVEL >= 1
#define APP_LOGE(fmt, ...) APP_LOG_BASE("E", "\e[31m", fmt, ##__VA_ARGS__)
#else
#define APP_LOGE(fmt, ...)
#endif
#if APP_LOG_LEVEL >= 2
#define APP_LOGW(fmt, ...) APP_LOG_BASE("W", "\e[33m", fmt, ##__VA_ARGS__)
#else
#define APP_LOGW(fmt, ...)
#endif
#if APP_LOG_LEVEL >= 3
#define APP_LOGI(fmt, ...) APP_LOG_BASE("I", "\e[32m", fmt, ##__VA_ARGS__)
#else
#define APP_LOGI(fmt, ...)
#endif
#if APP_LOG_LEVEL >= 4
#define APP_LOGD(fmt, ...) APP_LOG_BASE("D", "\e[34m", fmt, ##__VA_ARGS__)
#else
#define APP_LOGD(fmt, ...)
#endif

/* show warning */
#if APP_LOG_LEVEL >= 4
#warning "APP_LOGD is Enabled"
#endif

/* aliases */
#define LOGE APP_LOGE
#define LOGW APP_LOGW
#define LOGI APP_LOGI
#define LOGD APP_LOGD
