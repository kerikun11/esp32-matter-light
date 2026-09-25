/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "smart_light_web.h"

#include <app/server/Server.h>
#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/task.h>
#include <platform/PlatformManager.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "web_asset_http.h"
#include "web_assets.h"
#include "web_utils.h"

namespace {

constexpr uint16_t kIrRecordTimeoutMs = 10000;
constexpr uint16_t kIrResultIndicatorMs = 500;

std::string requestHeader(httpd_req_t* req, const char* name) {
  const size_t length = httpd_req_get_hdr_value_len(req, name);
  if (length == 0) return {};
  std::string value(length + 1, '\0');
  if (httpd_req_get_hdr_value_str(req, name, value.data(), value.size()) != ESP_OK) return {};
  value.resize(length);
  return value;
}

}  // namespace

void SmartLightWeb::begin() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // Fabric removal synchronously runs Matter cleanup callbacks. The HTTPD
  // default (4096 bytes) is too small for that call chain on ESP32-C6 and
  // trips the stack protector inside newlib formatting code.
  config.stack_size = 8192;
  config.max_uri_handlers = 9;
  // max_open_sockets(7) reserves 3 for the server's own internal use, so
  // only ~4 slots are actually available to clients. Without this, once
  // those fill up with connections the client hasn't explicitly closed
  // (most HTTP clients don't send Connection: close), a new request has to
  // wait for an old one to hit recv_wait_timeout(5s) rather than the server
  // reclaiming the least-recently-used slot immediately -- observed as
  // request stalls in multiples of ~5s under repeated/automated requests.
  config.lru_purge_enable = true;
  if (httpd_start(&server_, &config) != ESP_OK) {
    LOGE("[Web] httpd_start failed");
    return;
  }

  const httpd_uri_t root_uri = {
      .uri = "/", .method = HTTP_GET, .handler = &handleRootTrampoline, .user_ctx = this};
  const httpd_uri_t settings_uri = {
      .uri = "/settings", .method = HTTP_POST, .handler = &handleSaveSettingsTrampoline, .user_ctx = this};
  const httpd_uri_t record_uri = {
      .uri = "/record", .method = HTTP_POST, .handler = &handleRecordTrampoline, .user_ctx = this};
  const httpd_uri_t action_uri = {
      .uri = "/action", .method = HTTP_POST, .handler = &handleActionTrampoline, .user_ctx = this};
  const httpd_uri_t state_uri = {
      .uri = "/state", .method = HTTP_GET, .handler = &handleStateTrampoline, .user_ctx = this};
  const httpd_uri_t info_uri = {
      .uri = "/device-info", .method = HTTP_GET, .handler = &handleDeviceInfoTrampoline, .user_ctx = this};
  const httpd_uri_t matter_uri = {
      .uri = "/matter", .method = HTTP_POST, .handler = &handleMatterTrampoline, .user_ctx = this};
  httpd_register_uri_handler(server_, &matter_uri);
  httpd_register_uri_handler(server_, &info_uri);
  httpd_register_uri_handler(server_, &root_uri);
  httpd_register_uri_handler(server_, &state_uri);
  httpd_register_uri_handler(server_, &settings_uri);
  httpd_register_uri_handler(server_, &record_uri);
  httpd_register_uri_handler(server_, &action_uri);

  LOGI("[Web] HTTP server started on port 80");
}

void SmartLightWeb::setObservedStates(bool light_state, bool switch_state,
                                      bool night_state,
                                      int ambient_light_percent) {
  Lock lock(mutex_);
  observed_light_state_ = light_state;
  observed_switch_state_ = switch_state;
  observed_night_state_ = night_state;
  observed_ambient_light_percent_ = ambient_light_percent;
}

bool SmartLightWeb::hostnameUpdated() {
  Lock lock(mutex_);
  return hostname_updated_;
}

void SmartLightWeb::clearHostnameUpdated() {
  Lock lock(mutex_);
  hostname_updated_ = false;
}

bool SmartLightWeb::consumeRequestedLightState(bool& light_state) {
  Lock lock(mutex_);
  return requested_light_state_.consume(light_state);
}

bool SmartLightWeb::consumeRequestedSwitchState(bool& switch_state) {
  Lock lock(mutex_);
  return requested_switch_state_.consume(switch_state);
}

bool SmartLightWeb::consumeRequestedNightState(bool& night_state) {
  Lock lock(mutex_);
  return requested_night_state_.consume(night_state);
}

bool SmartLightWeb::consumeRebootRequested() {
  Lock lock(mutex_);
  if (!reboot_requested_ || esp_timer_get_time() < reboot_after_us_) return false;
  reboot_requested_ = false;
  return true;
}

void SmartLightWeb::completeAction() {
  Lock lock(mutex_);
  action_in_progress_ = false;
}

void SmartLightWeb::showStatus(const std::string& message, bool is_error) {
  Lock lock(mutex_);
  status_message_ = message;
  status_is_error_ = is_error;
}

esp_err_t SmartLightWeb::handleRoot(httpd_req_t* req) {
  logRequest(req);
  return sendPage(req);
}

esp_err_t SmartLightWeb::handleSaveSettings(httpd_req_t* req) {
  logRequest(req);
  const auto fields = parseFormBody(req);
  const std::string device_name = trim(formValue(fields, "device_name"));
  const std::string hostname = trim(formValue(fields, "hostname"));
  const int timeout_seconds = atoi(formValue(fields, "timeout").c_str());
  const int ambient_threshold =
      atoi(formValue(fields, "ambient_threshold").c_str());
  if (device_name.empty() || device_name.length() > 64 || hostname.empty() ||
      timeout_seconds <= 0 || ambient_threshold < 0 ||
      ambient_threshold > 100) {
    showStatus("入力内容を確認してください。設定は保存されませんでした。",
               true);
    return respondMutation(req);
  }

  {
    Lock lock(mutex_);
    settings_.device_name = device_name;
    settings_.hostname = hostname;
    settings_.light_off_timeout_seconds = timeout_seconds;
    settings_.ambient_light_threshold_percent = ambient_threshold;
    hostname_updated_ = true;
  }
  settings_store_.saveDeviceName(device_name);
  settings_store_.saveHostname(hostname);
  settings_store_.saveLightOffTimeoutSeconds(timeout_seconds);
  settings_store_.saveAmbientLightThresholdPercent(ambient_threshold);
  showStatus("基本設定を保存しました。");
  return respondMutation(req);
}

esp_err_t SmartLightWeb::handleRecord(httpd_req_t* req) {
  logRequest(req);
  const auto fields = parseFormBody(req);
  const std::string target = formValue(fields, "target");
  if (target != "on" && target != "off" && target != "night") {
    showStatus("赤外線リモコンの記録対象が不正です。", true);
    return respondMutation(req);
  }

  ir_remote_.clear();
  led_.blinkOnce(RgbLed::Color::kGreen, kIrRecordTimeoutMs + 1000);
  if (!ir_remote_.waitForAvailable(kIrRecordTimeoutMs)) {
    led_.blinkOnce(RgbLed::Color::kRed, kIrResultIndicatorMs);
    showStatus("赤外線信号を受信できませんでした。もう一度お試しください。",
               true);
    return respondMutation(req);
  }

  const auto ir_data = ir_remote_.get();
  std::string recorded_button;
  if (target == "on") {
    {
      Lock lock(mutex_);
      settings_.ir_data_light_on = ir_data;
    }
    settings_store_.saveIrDataLightOn(ir_data);
    recorded_button = "点灯";
  } else if (target == "off") {
    {
      Lock lock(mutex_);
      settings_.ir_data_light_off = ir_data;
    }
    settings_store_.saveIrDataLightOff(ir_data);
    recorded_button = "消灯";
  } else {
    {
      Lock lock(mutex_);
      settings_.ir_data_night = ir_data;
    }
    settings_store_.saveIrDataNight(ir_data);
    recorded_button = "常夜灯";
  }
  led_.blinkOnce(RgbLed::Color::kGreen, kIrResultIndicatorMs);
  showStatus(recorded_button + "ボタンの赤外線信号を記録しました。");
  return respondMutation(req);
}

esp_err_t SmartLightWeb::handleAction(httpd_req_t* req) {
  logRequest(req);
  const auto fields = parseFormBody(req);
  const std::string target = formValue(fields, "target");
  const std::string state = formValue(fields, "state");
  const bool enabled = state == "on";
  LOGI("[Web] action target='%s' state='%s'", target.c_str(), state.c_str());
  if (state != "on" && state != "off") {
    LOGW("[Web] action rejected: state must be on/off, got '%s'",
         state.c_str());
    showStatus("操作内容が不正です。", true);
    return respondMutation(req);
  }

  if (target == "light" || target == "switch" || target == "night") {
    {
      Lock lock(mutex_);
      if (action_in_progress_) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Previous action is still running");
      }
      action_in_progress_ = true;
      if (target == "light") {
        requested_light_state_.request(enabled);
      } else if (target == "switch") {
        requested_switch_state_.request(enabled);
      } else {
        requested_night_state_.request(enabled);
      }
    }
    // Do not return a page until the controller has committed IR output and
    // published all linked states. Never hold the mutex while sleeping.
    const int64_t deadline = esp_timer_get_time() + 5000000;
    while (true) {
      bool complete;
      {
        Lock lock(mutex_);
        complete = !action_in_progress_;
      }
      if (complete) {
        return respondMutation(req);
      }
      if (esp_timer_get_time() >= deadline) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Action result is not available yet");
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  if (target == "ambient") {
    {
      Lock lock(mutex_);
      settings_.ambient_light_mode_enabled = enabled;
    }
    settings_store_.saveAmbientLightModeEnabled(enabled);
    showStatus(std::string("明るさ連動を") +
               (enabled ? "オン" : "オフ") + "にしました。");
    return respondMutation(req);
  }
  if (target == "night_feature") {
    {
      Lock lock(mutex_);
      settings_.night_light_feature_enabled = enabled;
    }
    settings_store_.saveNightLightFeatureEnabled(enabled);
    showStatus(std::string("常夜灯エンドポイントを") +
               (enabled ? "有効" : "無効") +
               "にしました。再起動しています。");
    {
      Lock lock(mutex_);
      reboot_requested_ = true;
      reboot_after_us_ = esp_timer_get_time() + 500000;
    }
    return respondMutation(req);
  }
  showStatus("操作対象が不正です。", true);
  return respondMutation(req);
}

esp_err_t SmartLightWeb::handleMatter(httpd_req_t* req) {
  logRequest(req);
  const auto fields = parseFormBody(req);
  const auto action = formValue(fields, "action");
  std::string message;
  bool failed = false;
  // HTTP callbacks run outside the Matter task. Do not hold the settings
  // mutex while taking the stack lock or invoking fabric callbacks.
  {
    chip::DeviceLayer::StackLock lock;
    auto& server = chip::Server::GetInstance();
    auto& table = server.GetFabricTable();
    auto& window = server.GetCommissioningWindowManager();
    if (action == "commission") {
      if (window.IsCommissioningWindowOpen()) {
        message = "ペアリング受付はすでに開始されています。";
      } else {
        const auto err = window.OpenBasicCommissioningWindow(
            chip::System::Clock::Seconds32(300));
        failed = err != CHIP_NO_ERROR;
        message = failed ? "ペアリング受付を開始できませんでした。"
                         : "ペアリング受付を開始しました（最大5分間）。";
        if (failed) LOGE("[Web] Commissioning failed: %" CHIP_ERROR_FORMAT, err.Format());
      }
    } else if (action == "remove") {
      const auto index_text = formValue(fields, "index");
      const int index = index_text.size() <= 3 ? atoi(index_text.c_str()) : 0;
      const auto* fabric = index >= 1 && index <= 254 &&
                                   index_text == std::to_string(index)
                               ? table.FindFabricWithIndex(static_cast<chip::FabricIndex>(index))
                               : nullptr;
      char fabric_id[19] = {}, node_id[19] = {}, vendor_id[7] = {};
      if (fabric) {
        snprintf(fabric_id, sizeof(fabric_id), "0x%016" PRIX64, fabric->GetFabricId());
        snprintf(node_id, sizeof(node_id), "0x%016" PRIX64, fabric->GetNodeId());
        snprintf(vendor_id, sizeof(vendor_id), "0x%04X", static_cast<unsigned>(fabric->GetVendorId()));
      }
      // Reject stale pages if an index has since been reused by another fabric.
      if (!fabric || formValue(fields, "fabric_id") != fabric_id ||
          formValue(fields, "node_id") != node_id ||
          formValue(fields, "vendor_id") != vendor_id) {
        failed = true;
        message = "削除対象が見つからないか、登録情報が変わっています。一覧を確認してください。";
      } else {
        const auto err = table.Delete(static_cast<chip::FabricIndex>(index));
        failed = err != CHIP_NO_ERROR;
        message = failed ? "Fabricを削除できませんでした。"
                         : "Fabric #" + index_text + "を削除しました。Wi-Fi接続は維持されます。";
        if (!failed && table.FabricCount() == 0 && !window.IsCommissioningWindowOpen()) {
          message += "再登録するにはペアリング受付を開始してください。";
        }
        if (failed) LOGE("[Web] Fabric removal failed: %" CHIP_ERROR_FORMAT, err.Format());
      }
    } else {
      failed = true;
      message = "Matterの操作内容が不正です。";
    }
  }
  showStatus(message, failed);
  return respondMutation(req);
}

esp_err_t SmartLightWeb::respondMutation(httpd_req_t* req) {
  if (requestHeader(req, "Accept").find("application/json") != std::string::npos) {
    return sendState(req);
  }
  redirectRoot(req);
  return ESP_OK;
}

esp_err_t SmartLightWeb::sendPage(httpd_req_t* req) {
  // Compression is done at build time: send flash-resident bytes directly.
  const std::string accept = requestHeader(req, "Accept-Encoding");
  const bool gzip = web_asset::quality(accept, "gzip") > 0;
  if (!gzip && web_asset::quality(accept, "identity") == 0) {
    httpd_resp_set_status(req, "406 Not Acceptable");
    return httpd_resp_sendstr(req, "No supported content encoding");
  }
  const char* etag = gzip ? kWebGzipEtag : kWebIdentityEtag;
  const bool unchanged = web_asset::etagMatches(requestHeader(req, "If-None-Match"), etag);
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  // Revalidate the static shell after firmware updates; state is never cached.
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "ETag", etag);
  if (gzip) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  if (unchanged) {
    httpd_resp_set_status(req, "304 Not Modified");
    return httpd_resp_send(req, nullptr, 0);
  }
  return httpd_resp_send(req,
                         reinterpret_cast<const char*>(gzip ? kWebGzip : kWebIdentity),
                         gzip ? sizeof(kWebGzip) : sizeof(kWebIdentity));
}

esp_err_t SmartLightWeb::sendState(httpd_req_t* req) {
  cJSON* state = cJSON_CreateObject();
  if (!state) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  std::string message;
  bool ok = true;
  {
    Lock lock(mutex_);
    message = status_message_;
    ok &= cJSON_AddBoolToObject(state, "light", observed_light_state_) != nullptr;
    ok &= cJSON_AddBoolToObject(state, "switch", observed_switch_state_) != nullptr;
    ok &= cJSON_AddBoolToObject(state, "night", observed_night_state_) != nullptr;
    ok &= cJSON_AddBoolToObject(state, "ambient", settings_.ambient_light_mode_enabled) != nullptr;
    ok &= cJSON_AddBoolToObject(state, "night_feature", settings_.night_light_feature_enabled) != nullptr;
    ok &= cJSON_AddNumberToObject(state, "ambient_value", observed_ambient_light_percent_) != nullptr;
    ok &= cJSON_AddStringToObject(state, "device_name", settings_.device_name.c_str()) != nullptr;
    ok &= cJSON_AddStringToObject(state, "hostname", settings_.hostname.c_str()) != nullptr;
    ok &= cJSON_AddNumberToObject(state, "timeout", settings_.light_off_timeout_seconds) != nullptr;
    ok &= cJSON_AddNumberToObject(state, "ambient_threshold", settings_.ambient_light_threshold_percent) != nullptr;
    ok &= cJSON_AddStringToObject(state, "message", message.c_str()) != nullptr;
    ok &= cJSON_AddBoolToObject(state, "error", status_is_error_) != nullptr;
    ok &= cJSON_AddBoolToObject(state, "reboot", reboot_requested_) != nullptr;
  }
  char* json = ok ? cJSON_PrintUnformatted(state) : nullptr;
  cJSON_Delete(state);
  if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t result = httpd_resp_sendstr(req, json);
  cJSON_free(json);
  if (result == ESP_OK) {
    Lock lock(mutex_);
    if (status_message_ == message) {
      status_message_.clear();
      status_is_error_ = false;
    }
  }
  return result;
}

// Read Matter under its stack lock, independently of the settings mutex.
esp_err_t SmartLightWeb::sendDeviceInfo(httpd_req_t* req) {
  cJSON* info = cJSON_CreateObject();
  if (!info) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  bool ok = true;
  const auto* app = esp_app_get_description();
  ok &= cJSON_AddStringToObject(info, "version", app->version) != nullptr;
  ok &= cJSON_AddStringToObject(info, "idf_version", app->idf_ver) != nullptr;
  ok &= cJSON_AddNumberToObject(info, "uptime_seconds", esp_timer_get_time() / 1000000) != nullptr;
  wifi_ap_record_t ap{};
  const bool connected = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
  const std::string ssid(reinterpret_cast<const char*>(ap.ssid),
                         strnlen(reinterpret_cast<const char*>(ap.ssid), sizeof(ap.ssid)));
  ok &= cJSON_AddBoolToObject(info, "connected", connected) != nullptr;
  ok &= cJSON_AddStringToObject(info, "ssid", ssid.c_str()) != nullptr;
  ok &= cJSON_AddNumberToObject(info, "rssi", ap.rssi) != nullptr;
  auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip{};
  char address[48] = {};
  if (connected && netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr) {
    snprintf(address, sizeof(address), IPSTR, IP2STR(&ip.ip));
  }
  ok &= cJSON_AddStringToObject(info, "ipv4", address) != nullptr;
  auto* ipv6 = cJSON_AddArrayToObject(info, "ipv6");
  ok &= ipv6 != nullptr;
  if (connected && netif && ipv6) {
    esp_ip6_addr_t addresses[CONFIG_LWIP_IPV6_NUM_ADDRESSES]{};
    const int count = esp_netif_get_all_preferred_ip6(netif, addresses);
    for (int i = 0; i < count; ++i) {
      snprintf(address, sizeof(address), IPV6STR, IPV62STR(addresses[i]));
      auto* item = cJSON_CreateString(address);
      if (!item || !cJSON_AddItemToArray(ipv6, item)) {
        cJSON_Delete(item);
        ok = false;
      }
    }
  }
  auto* fabrics = cJSON_AddArrayToObject(info, "fabrics");
  ok &= fabrics != nullptr;
  if (fabrics) {
    chip::DeviceLayer::StackLock lock;
    ok &= cJSON_AddBoolToObject(info, "commissioning_open",
                                chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen()) != nullptr;
    for (const auto& fabric : chip::Server::GetInstance().GetFabricTable()) {
      auto* item = cJSON_CreateObject();
      if (!item) {
        ok = false;
        break;
      }
      const auto label = fabric.GetFabricLabel();
      const std::string label_text(label.data(), label.size());
      char node_id[19], fabric_id[19], vendor_id[7];
      snprintf(node_id, sizeof(node_id), "0x%016" PRIX64, fabric.GetNodeId());
      snprintf(fabric_id, sizeof(fabric_id), "0x%016" PRIX64, fabric.GetFabricId());
      snprintf(vendor_id, sizeof(vendor_id), "0x%04X", static_cast<unsigned>(fabric.GetVendorId()));
      ok &= cJSON_AddNumberToObject(item, "index", fabric.GetFabricIndex()) != nullptr;
      ok &= cJSON_AddStringToObject(item, "label", label_text.c_str()) != nullptr;
      // Keep 64-bit identifiers as strings to avoid JavaScript precision loss.
      ok &= cJSON_AddStringToObject(item, "node_id", node_id) != nullptr;
      ok &= cJSON_AddStringToObject(item, "fabric_id", fabric_id) != nullptr;
      ok &= cJSON_AddStringToObject(item, "vendor_id", vendor_id) != nullptr;
      if (!cJSON_AddItemToArray(fabrics, item)) {
        cJSON_Delete(item);
        ok = false;
      }
    }
  }
  char* json = ok ? cJSON_PrintUnformatted(info) : nullptr;
  cJSON_Delete(info);
  if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const auto result = httpd_resp_sendstr(req, json);
  cJSON_free(json);
  return result;
}
