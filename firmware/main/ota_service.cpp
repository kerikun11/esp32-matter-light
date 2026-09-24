/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "ota_service.h"

#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "app_log.h"

namespace {

constexpr size_t kOtaRecvBufSize = 4096;

bool queryFlagSet(httpd_req_t* req, const char* key) {
  const size_t query_len = httpd_req_get_url_query_len(req);
  if (query_len == 0) return false;
  std::string query(query_len, '\0');
  if (httpd_req_get_url_query_str(req, query.data(), query_len + 1) != ESP_OK) {
    return false;
  }
  char value[8] = {};
  return httpd_query_key_value(query.c_str(), key, value, sizeof(value)) ==
             ESP_OK &&
         std::string(value) == "1";
}

void sendPlainError(httpd_req_t* req, const char* status, const char* msg) {
  LOGE("[OTA] %s", msg);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, msg);
}

// esp_restart() right after sending the HTTP response can cut the TCP
// connection before curl has read it, so reboot from a short-lived task
// instead of inline in the handler.
void rebootAfterDelay(void*) {
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
}

esp_err_t handleUpdate(httpd_req_t* req) {
  LOGI("[Web] POST %s", req->uri);

  if (req->content_len == 0) {
    sendPlainError(req, "400 Bad Request", "empty body\n");
    return ESP_OK;
  }

  const esp_partition_t* update_partition =
      esp_ota_get_next_update_partition(nullptr);
  if (!update_partition) {
    sendPlainError(req, "500 Internal Server Error",
                   "no OTA partition available\n");
    return ESP_OK;
  }

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err =
      esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  if (err != ESP_OK) {
    sendPlainError(req, "500 Internal Server Error", "esp_ota_begin failed\n");
    return ESP_OK;
  }

  std::string buf(kOtaRecvBufSize, '\0');
  size_t received = 0;
  int consecutive_timeouts = 0;
  constexpr int kMaxConsecutiveTimeouts = 5;  // ~25s of dead silence total
  while (received < req->content_len) {
    const int ret = httpd_req_recv(
        req, buf.data(),
        std::min(buf.size(), req->content_len - received));
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      if (++consecutive_timeouts > kMaxConsecutiveTimeouts) {
        esp_ota_abort(ota_handle);
        sendPlainError(req, "408 Request Timeout", "upload stalled\n");
        return ESP_OK;
      }
      continue;
    }
    consecutive_timeouts = 0;
    if (ret <= 0) {
      esp_ota_abort(ota_handle);
      sendPlainError(req, "400 Bad Request", "receive failed\n");
      return ESP_OK;
    }
    if (esp_ota_write(ota_handle, buf.data(), ret) != ESP_OK) {
      esp_ota_abort(ota_handle);
      sendPlainError(req, "500 Internal Server Error", "write failed\n");
      return ESP_OK;
    }
    received += ret;
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    sendPlainError(req, "400 Bad Request",
                   err == ESP_ERR_OTA_VALIDATE_FAILED
                       ? "image validation failed\n"
                       : "esp_ota_end failed\n");
    return ESP_OK;
  }

  if (!queryFlagSet(req, "skip_check")) {
    esp_app_desc_t new_desc;
    const esp_app_desc_t* running_desc = esp_app_get_description();
    if (esp_ota_get_partition_description(update_partition, &new_desc) !=
            ESP_OK ||
        strncmp(new_desc.project_name, running_desc->project_name,
                sizeof(new_desc.project_name)) != 0) {
      LOGE("[OTA] project_name mismatch: image='%s' device='%s'",
           new_desc.project_name, running_desc->project_name);
      const std::string msg =
          "project_name mismatch: image is '" +
          std::string(new_desc.project_name) + "', this device is '" +
          std::string(running_desc->project_name) +
          "'. Pass ?skip_check=1 to flash it anyway.\n";
      sendPlainError(req, "400 Bad Request", msg.c_str());
      return ESP_OK;
    }
  }

  if (esp_ota_set_boot_partition(update_partition) != ESP_OK) {
    sendPlainError(req, "500 Internal Server Error",
                   "esp_ota_set_boot_partition failed\n");
    return ESP_OK;
  }

  LOGW("[OTA] Update OK (%zu bytes), rebooting...", received);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "OK, rebooting\n");
  xTaskCreate(rebootAfterDelay, "ota_reboot", 2048, nullptr, 5, nullptr);
  return ESP_OK;
}

esp_err_t handleVersion(httpd_req_t* req) {
  LOGI("[Web] GET %s", req->uri);
  const esp_app_desc_t* desc = esp_app_get_description();
  char buf[256];
  const int len = snprintf(
      buf, sizeof(buf),
      "{\"project_name\":\"%s\",\"version\":\"%s\",\"idf_ver\":\"%s\","
      "\"date\":\"%s\",\"time\":\"%s\"}\n",
      desc->project_name, desc->version, desc->idf_ver, desc->date,
      desc->time);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, buf, len);
  return ESP_OK;
}

}  // namespace

void registerOtaHandlers(httpd_handle_t server) {
  const httpd_uri_t update_uri = {.uri = "/update", .method = HTTP_POST,
                                  .handler = &handleUpdate,
                                  .user_ctx = nullptr};
  const httpd_uri_t version_uri = {.uri = "/version", .method = HTTP_GET,
                                   .handler = &handleVersion,
                                   .user_ctx = nullptr};
  httpd_register_uri_handler(server, &update_uri);
  httpd_register_uri_handler(server, &version_uri);
}

void confirmOtaBootIfPending() {
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
      ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
    // No further self-test beyond "we made it to app_main"; that's already
    // enough signal that the image isn't completely broken.
    esp_ota_mark_app_valid_cancel_rollback();
    LOGI("[OTA] New image confirmed valid, rollback cancelled");
  }
#endif
}
