/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <ArduinoOTA.h>

inline const char* ota_error_name(ota_error_t error) {
  switch (error) {
    case OTA_AUTH_ERROR:
      return "auth";
    case OTA_BEGIN_ERROR:
      return "begin";
    case OTA_CONNECT_ERROR:
      return "connect";
    case OTA_RECEIVE_ERROR:
      return "receive";
    case OTA_END_ERROR:
      return "end";
    default:
      return "unknown";
  }
}
