/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "smart_light_web.h"

#include <cstdlib>

#include "web_utils.h"

namespace {

constexpr char kWebPageTemplate[] =
#include "smart_light_web_page.inc"
    ;

constexpr uint16_t kIrRecordTimeoutMs = 10000;
constexpr uint16_t kIrResultIndicatorMs = 500;

void replaceToggleValues(std::string& html, const char* action_key,
                         const char* class_key, const char* state_key,
                         bool enabled, const char* on_text = "オン",
                         const char* off_text = "オフ") {
  replaceTemplateValue(html, action_key, enabled ? "off" : "on");
  replaceTemplateValue(html, class_key, enabled ? "on" : "off");
  replaceTemplateValue(html, state_key, enabled ? on_text : off_text);
}

std::string buildNightControl(bool enabled) {
  std::string html =
      "<div class=\"control-item\"><span class=\"label\">常夜灯</span>"
      "<form class=\"toggle-form\" method=\"post\" action=\"/action\">"
      "<input type=\"hidden\" name=\"target\" value=\"night\">"
      "<input type=\"hidden\" name=\"state\" value=\"";
  html += enabled ? "off" : "on";
  html += "\"><button class=\"toggle-btn ";
  html += enabled ? "on" : "off";
  html += "\">";
  html += enabled ? "オン" : "オフ";
  html += "</button></form></div>";
  return html;
}

}  // namespace

void SmartLightWeb::begin() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 8;
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
      .uri = "/", .method = HTTP_GET,
      .handler = &handleRootTrampoline, .user_ctx = this};
  const httpd_uri_t settings_uri = {
      .uri = "/settings", .method = HTTP_POST,
      .handler = &handleSaveSettingsTrampoline, .user_ctx = this};
  const httpd_uri_t record_uri = {
      .uri = "/record", .method = HTTP_POST,
      .handler = &handleRecordTrampoline, .user_ctx = this};
  const httpd_uri_t action_uri = {
      .uri = "/action", .method = HTTP_POST,
      .handler = &handleActionTrampoline, .user_ctx = this};
  httpd_register_uri_handler(server_, &root_uri);
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
  if (!reboot_requested_) return false;
  reboot_requested_ = false;
  return true;
}

void SmartLightWeb::showStatus(const std::string& message, bool is_error) {
  Lock lock(mutex_);
  status_message_ = message;
  status_is_error_ = is_error;
}

esp_err_t SmartLightWeb::handleRoot(httpd_req_t* req) {
  logRequest(req);
  sendPage(req);
  return ESP_OK;
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
    redirectRoot(req);
    return ESP_OK;
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
  redirectRoot(req);
  return ESP_OK;
}

esp_err_t SmartLightWeb::handleRecord(httpd_req_t* req) {
  logRequest(req);
  const auto fields = parseFormBody(req);
  const std::string target = formValue(fields, "target");
  if (target != "on" && target != "off" && target != "night") {
    showStatus("赤外線リモコンの記録対象が不正です。", true);
    redirectRoot(req);
    return ESP_OK;
  }

  ir_remote_.clear();
  led_.blinkOnce(RgbLed::Color::Green, kIrRecordTimeoutMs + 1000);
  if (!ir_remote_.waitForAvailable(kIrRecordTimeoutMs)) {
    led_.blinkOnce(RgbLed::Color::Red, kIrResultIndicatorMs);
    showStatus("赤外線信号を受信できませんでした。もう一度お試しください。",
               true);
    redirectRoot(req);
    return ESP_OK;
  }

  const auto ir_data = ir_remote_.get();
  std::string recorded_button;
  if (target == "on") {
    { Lock lock(mutex_); settings_.ir_data_light_on = ir_data; }
    settings_store_.saveIrDataLightOn(ir_data);
    recorded_button = "点灯";
  } else if (target == "off") {
    { Lock lock(mutex_); settings_.ir_data_light_off = ir_data; }
    settings_store_.saveIrDataLightOff(ir_data);
    recorded_button = "消灯";
  } else {
    { Lock lock(mutex_); settings_.ir_data_night = ir_data; }
    settings_store_.saveIrDataNight(ir_data);
    recorded_button = "常夜灯";
  }
  led_.blinkOnce(RgbLed::Color::Green, kIrResultIndicatorMs);
  showStatus(recorded_button + "ボタンの赤外線信号を記録しました。");
  redirectRoot(req);
  return ESP_OK;
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
    redirectRoot(req);
    return ESP_OK;
  }

  if (target == "light") {
    requested_light_state_.request(enabled);
    redirectRoot(req);
    return ESP_OK;
  }
  if (target == "switch") {
    requested_switch_state_.request(enabled);
    redirectRoot(req);
    return ESP_OK;
  }
  if (target == "night") {
    requested_night_state_.request(enabled);
    redirectRoot(req);
    return ESP_OK;
  }
  if (target == "ambient") {
    { Lock lock(mutex_); settings_.ambient_light_mode_enabled = enabled; }
    settings_store_.saveAmbientLightModeEnabled(enabled);
    showStatus(std::string("明るさ連動を") +
               (enabled ? "オン" : "オフ") + "にしました。");
    redirectRoot(req);
    return ESP_OK;
  }
  if (target == "night_feature") {
    { Lock lock(mutex_); settings_.night_light_feature_enabled = enabled; }
    settings_store_.saveNightLightFeatureEnabled(enabled);
    showStatus(std::string("常夜灯エンドポイントを") +
               (enabled ? "有効" : "無効") +
               "にしました。再起動しています。");
    {
      Lock lock(mutex_);
      reboot_requested_ = true;
    }
    sendPage(req);
    return ESP_OK;
  }
  redirectRoot(req);
  return ESP_OK;
}

void SmartLightWeb::sendPage(httpd_req_t* req) {
  const std::string page = buildPage();
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, page.c_str(), page.length());
  Lock lock(mutex_);
  status_message_.clear();
  status_is_error_ = false;
}

std::string SmartLightWeb::buildPage() const {
  std::string html(kWebPageTemplate);
  html.reserve(html.length() + 512);

  replaceTemplateValue(html, "{{PREVIEW_NOTICE}}", "");
  replaceTemplateValue(html, "{{SETTINGS_OPEN}}", "");

  bool observed_light_state, observed_switch_state, observed_night_state,
      reboot_requested;
  int observed_ambient_light_percent;
  std::string status_message;
  bool status_is_error;
  // Snapshot settings_ here too: it's shared with the main app task, and
  // this whole method otherwise reads it without synchronization.
  SmartLightSettings settings;
  {
    Lock lock(mutex_);
    observed_light_state = observed_light_state_;
    observed_switch_state = observed_switch_state_;
    observed_night_state = observed_night_state_;
    reboot_requested = reboot_requested_;
    observed_ambient_light_percent = observed_ambient_light_percent_;
    status_message = status_message_;
    status_is_error = status_is_error_;
    settings = settings_;
  }

  std::string status_notice;
  if (!status_message.empty()) {
    status_notice = "<div class=\"status ";
    status_notice += status_is_error ? "error" : "success";
    status_notice += "\">";
    status_notice += status_message;
    status_notice += "</div>";
  }
  replaceTemplateValue(html, "{{STATUS_NOTICE}}", status_notice);

  replaceToggleValues(html, "{{LIGHT_ACTION}}", "{{LIGHT_CLASS}}",
                      "{{LIGHT_STATE}}", observed_light_state);
  replaceToggleValues(html, "{{SWITCH_ACTION}}", "{{SWITCH_CLASS}}",
                      "{{SWITCH_STATE}}", observed_switch_state);

  if (settings.night_light_feature_enabled) {
    replaceTemplateValue(html, "{{NIGHT_CONTROL}}",
                         buildNightControl(observed_night_state));
    replaceTemplateValue(
        html, "{{NIGHT_RECORD_BUTTON}}",
        "<button class=\"warn\" name=\"target\" value=\"night\">常夜灯ボタンを記録</button>");
  } else {
    replaceTemplateValue(html, "{{NIGHT_CONTROL}}", "");
    replaceTemplateValue(html, "{{NIGHT_RECORD_BUTTON}}", "");
  }

  replaceTemplateValue(html, "{{AMBIENT_VALUE}}",
                       std::to_string(observed_ambient_light_percent));
  replaceToggleValues(html, "{{AMBIENT_ACTION}}",
                      "{{AMBIENT_STATUS_CLASS}}",
                      "{{AMBIENT_STATUS_STATE}}",
                      settings.ambient_light_mode_enabled);
  replaceTemplateValue(html, "{{REBOOT_NOTICE}}",
                       reboot_requested
                           ? "<div class=\"notice\">再起動しています。数秒待ってからページを再読み込みしてください。</div>"
                           : "");
  replaceTemplateValue(html, "{{DEVICE_NAME}}",
                       escapeHtml(settings.device_name.c_str()));
  replaceTemplateValue(html, "{{HOSTNAME}}",
                       escapeHtml(settings.hostname.c_str()));
  replaceTemplateValue(html, "{{TIMEOUT}}",
                       std::to_string(settings.light_off_timeout_seconds));
  replaceTemplateValue(
      html, "{{AMBIENT_THRESHOLD}}",
      std::to_string(settings.ambient_light_threshold_percent));
  replaceToggleValues(html, "{{NIGHT_FEATURE_ACTION}}",
                      "{{NIGHT_FEATURE_CLASS}}",
                      "{{NIGHT_FEATURE_STATE}}",
                      settings.night_light_feature_enabled, "有効", "無効");

  return html;
}
