/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "smart_light_web.h"

#include "web_utils.h"

namespace {

constexpr char kWebPageTemplate[] =
#include "smart_light_web_page.inc"
    ;

constexpr uint16_t kIrRecordTimeoutMs = 10000;
constexpr uint16_t kIrResultIndicatorMs = 500;

void replaceToggleValues(String& html, const char* action_key,
                         const char* class_key, const char* state_key,
                         bool enabled, const char* on_text = "オン",
                         const char* off_text = "オフ") {
  replaceTemplateValue(html, action_key, enabled ? "off" : "on");
  replaceTemplateValue(html, class_key, enabled ? "on" : "off");
  replaceTemplateValue(html, state_key, enabled ? on_text : off_text);
}

String buildNightControl(bool enabled) {
  String html =
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
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/settings", HTTP_POST, [this]() { handleSaveSettings(); });
  server_.on("/record", HTTP_POST, [this]() { handleRecord(); });
  server_.on("/action", HTTP_POST, [this]() { handleAction(); });
  server_.begin();
  LOGI("[Web] HTTP server started on port 80");
}

void SmartLightWeb::handle() { server_.handleClient(); }

void SmartLightWeb::setObservedStates(bool light_state, bool switch_state,
                                      bool night_state,
                                      int ambient_light_percent) {
  observed_light_state_ = light_state;
  observed_switch_state_ = switch_state;
  observed_night_state_ = night_state;
  observed_ambient_light_percent_ = ambient_light_percent;
}

bool SmartLightWeb::consumeRequestedLightState(bool& light_state) {
  return requested_light_state_.consume(light_state);
}

bool SmartLightWeb::consumeRequestedSwitchState(bool& switch_state) {
  return requested_switch_state_.consume(switch_state);
}

bool SmartLightWeb::consumeRequestedNightState(bool& night_state) {
  return requested_night_state_.consume(night_state);
}

bool SmartLightWeb::consumeRebootRequested() {
  if (!reboot_requested_) return false;
  reboot_requested_ = false;
  return true;
}

void SmartLightWeb::showStatus(const String& message, bool is_error) {
  status_message_ = message;
  status_is_error_ = is_error;
}

void SmartLightWeb::handleRoot() {
  logRequest(server_);
  sendPage();
}

void SmartLightWeb::handleSaveSettings() {
  logRequest(server_);
  String device_name = server_.arg("device_name");
  device_name.trim();
  String hostname = server_.arg("hostname");
  hostname.trim();
  const int timeout_seconds = server_.arg("timeout").toInt();
  const int ambient_threshold = server_.arg("ambient_threshold").toInt();
  if (device_name.isEmpty() || device_name.length() > 64 ||
      !hostname.length() || timeout_seconds <= 0 || ambient_threshold < 0 ||
      ambient_threshold > 100) {
    showStatus("入力内容を確認してください。設定は保存されませんでした。",
               true);
    return redirectRoot(server_);
  }

  settings_.device_name = device_name.c_str();
  settings_.hostname = hostname.c_str();
  settings_.light_off_timeout_seconds = timeout_seconds;
  settings_.ambient_light_threshold_percent = ambient_threshold;

  settings_store_.saveDeviceName(settings_.device_name);
  settings_store_.saveHostname(settings_.hostname);
  settings_store_.saveLightOffTimeoutSeconds(settings_.light_off_timeout_seconds);
  settings_store_.saveAmbientLightThresholdPercent(
      settings_.ambient_light_threshold_percent);
  hostname_updated_ = true;
  showStatus("基本設定を保存しました。");
  redirectRoot(server_);
}

void SmartLightWeb::handleRecord() {
  logRequest(server_);
  const String target = server_.arg("target");
  if (target != "on" && target != "off" && target != "night") {
    showStatus("赤外線リモコンの記録対象が不正です。", true);
    return redirectRoot(server_);
  }

  ir_remote_.clear();
  led_.blinkOnce(RgbLed::Color::Green, kIrRecordTimeoutMs + 1000);
  if (!ir_remote_.waitForAvailable(kIrRecordTimeoutMs)) {
    led_.blinkOnce(RgbLed::Color::Red, kIrResultIndicatorMs);
    showStatus("赤外線信号を受信できませんでした。もう一度お試しください。",
               true);
    return redirectRoot(server_);
  }

  const auto ir_data = ir_remote_.get();
  String recorded_button;
  if (target == "on") {
    settings_.ir_data_light_on = ir_data;
    settings_store_.saveIrDataLightOn(settings_.ir_data_light_on);
    recorded_button = "点灯";
  } else if (target == "off") {
    settings_.ir_data_light_off = ir_data;
    settings_store_.saveIrDataLightOff(settings_.ir_data_light_off);
    recorded_button = "消灯";
  } else {
    settings_.ir_data_night = ir_data;
    settings_store_.saveIrDataNight(settings_.ir_data_night);
    recorded_button = "常夜灯";
  }
  led_.blinkOnce(RgbLed::Color::Green, kIrResultIndicatorMs);
  showStatus(recorded_button + "ボタンの赤外線信号を記録しました。");
  redirectRoot(server_);
}

void SmartLightWeb::handleAction() {
  logRequest(server_);
  const String target = server_.arg("target");
  const String state = server_.arg("state");
  const bool enabled = state == "on";
  if (state != "on" && state != "off") return redirectRoot(server_);

  if (target == "light") {
    requested_light_state_.request(enabled);
    return redirectRoot(server_);
  }
  if (target == "switch") {
    requested_switch_state_.request(enabled);
    return redirectRoot(server_);
  }
  if (target == "night") {
    requested_night_state_.request(enabled);
    return redirectRoot(server_);
  }
  if (target == "ambient") {
    settings_.ambient_light_mode_enabled = enabled;
    settings_store_.saveAmbientLightModeEnabled(enabled);
    return redirectRoot(server_);
  }
  if (target == "night_feature") {
    settings_.night_light_feature_enabled = enabled;
    settings_store_.saveNightLightFeatureEnabled(enabled);
    showStatus(String("常夜灯エンドポイントを") +
               (enabled ? "有効" : "無効") +
               "にしました。再起動しています。");
    reboot_requested_ = true;
    return sendPage();
  }
  redirectRoot(server_);
}

void SmartLightWeb::sendPage() {
  server_.send(200, "text/html", buildPage());
  status_message_ = "";
  status_is_error_ = false;
}

String SmartLightWeb::buildPage() const {
  String html(kWebPageTemplate);
  html.reserve(html.length() + 512);

  replaceTemplateValue(html, "{{PREVIEW_NOTICE}}", "");
  replaceTemplateValue(html, "{{SETTINGS_OPEN}}", "");

  String status_notice;
  if (status_message_.length()) {
    status_notice = "<div class=\"status ";
    status_notice += status_is_error_ ? "error" : "success";
    status_notice += "\">";
    status_notice += status_message_;
    status_notice += "</div>";
  }
  replaceTemplateValue(html, "{{STATUS_NOTICE}}", status_notice);

  replaceToggleValues(html, "{{LIGHT_ACTION}}", "{{LIGHT_CLASS}}",
                      "{{LIGHT_STATE}}", observed_light_state_);
  replaceToggleValues(html, "{{SWITCH_ACTION}}", "{{SWITCH_CLASS}}",
                      "{{SWITCH_STATE}}", observed_switch_state_);

  if (settings_.night_light_feature_enabled) {
    replaceTemplateValue(html, "{{NIGHT_CONTROL}}",
                         buildNightControl(observed_night_state_));
    replaceTemplateValue(
        html, "{{NIGHT_RECORD_BUTTON}}",
        "<button class=\"warn\" name=\"target\" value=\"night\">常夜灯ボタンを記録</button>");
  } else {
    replaceTemplateValue(html, "{{NIGHT_CONTROL}}", "");
    replaceTemplateValue(html, "{{NIGHT_RECORD_BUTTON}}", "");
  }

  replaceTemplateValue(html, "{{AMBIENT_VALUE}}",
                       String(observed_ambient_light_percent_));
  replaceToggleValues(html, "{{AMBIENT_ACTION}}",
                      "{{AMBIENT_STATUS_CLASS}}",
                      "{{AMBIENT_STATUS_STATE}}",
                      settings_.ambient_light_mode_enabled);
  replaceTemplateValue(html, "{{REBOOT_NOTICE}}",
                       reboot_requested_
                           ? "<div class=\"notice\">再起動しています。数秒待ってからページを再読み込みしてください。</div>"
                           : "");
  replaceTemplateValue(html, "{{DEVICE_NAME}}",
                       escapeHtml(settings_.device_name.c_str()));
  replaceTemplateValue(html, "{{HOSTNAME}}",
                       escapeHtml(settings_.hostname.c_str()));
  replaceTemplateValue(html, "{{TIMEOUT}}",
                       String(settings_.light_off_timeout_seconds));
  replaceTemplateValue(html, "{{AMBIENT_THRESHOLD}}",
                       String(settings_.ambient_light_threshold_percent));
  replaceToggleValues(html, "{{NIGHT_FEATURE_ACTION}}",
                      "{{NIGHT_FEATURE_CLASS}}",
                      "{{NIGHT_FEATURE_STATE}}",
                      settings_.night_light_feature_enabled, "有効", "無効");

  return html;
}
