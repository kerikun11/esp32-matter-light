/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 *
 * curl-friendly OTA over the on-device HTTP server:
 *   curl --data-binary @firmware.bin http://<hostname>.local/update
 *   curl http://<hostname>.local/version
 *
 * `POST /update` rejects an image whose embedded project_name (the
 * CMake PROJECT_NAME baked into esp_app_desc_t) doesn't match this
 * device's own project_name, to guard against flashing the wrong
 * firmware onto the wrong device type on a LAN with mixed ESP32
 * devices. Pass `?skip_check=1` to bypass this (e.g. right after a
 * deliberate PROJECT_NAME rename).
 */
#pragma once

#include <esp_http_server.h>

// Registers the /update and /version handlers on an already-started
// httpd server (e.g. the one SmartLightWeb owns).
void registerOtaHandlers(httpd_handle_t server);

// Call once at startup: if the currently running image is pending
// rollback verification (i.e. this is the first boot after an OTA
// update), mark it valid so the bootloader won't roll it back.
void confirmOtaBootIfPending();
