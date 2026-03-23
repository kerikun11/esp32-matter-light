/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "smart_light_web.h"

#include "app_log.h"

void SmartLightWeb::begin() {
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/settings", HTTP_POST, [this]() { handleSaveSettings(); });
  server_.on("/record", HTTP_POST, [this]() { handleRecord(); });
  server_.on("/action", HTTP_POST, [this]() { handleAction(); });
  server_.on("/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_.begin();
  LOGI("[Web] HTTP server started on port 80");
}

void SmartLightWeb::handle() { server_.handleClient(); }

void SmartLightWeb::setObservedStates(bool light_state, bool switch_state) {
  observed_light_state_ = light_state;
  observed_switch_state_ = switch_state;
}

bool SmartLightWeb::consumeRequestedLightState(bool& light_state) {
  if (!requested_light_state_pending_) return false;
  light_state = requested_light_state_;
  requested_light_state_pending_ = false;
  return true;
}

bool SmartLightWeb::consumeRequestedSwitchState(bool& switch_state) {
  if (!requested_switch_state_pending_) return false;
  switch_state = requested_switch_state_;
  requested_switch_state_pending_ = false;
  return true;
}

void SmartLightWeb::handleRoot() { sendPage(); }

void SmartLightWeb::handleSaveSettings() {
  if (!server_.hasArg("hostname") || !server_.hasArg("timeout")) {
    sendPage("hostname and timeout are required", true);
    return;
  }

  const String hostname = server_.arg("hostname");
  const int timeout_seconds = server_.arg("timeout").toInt();
  const bool ambient_enabled = server_.hasArg("ambient");

  if (hostname.isEmpty()) {
    sendPage("hostname must not be empty", true);
    return;
  }
  if (timeout_seconds <= 0) {
    sendPage("timeout must be greater than 0", true);
    return;
  }

  settings_.hostname = hostname.c_str();
  settings_.light_off_timeout_seconds = timeout_seconds;
  settings_.ambient_light_mode_enabled = ambient_enabled;

  settings_store_.saveHostname(settings_.hostname);
  settings_store_.saveLightOffTimeoutSeconds(timeout_seconds);
  settings_store_.saveAmbientLightModeEnabled(ambient_enabled);
  hostname_updated_ = true;

  LOGI("[Web] Settings updated");
  sendPage("settings saved");
}

void SmartLightWeb::handleRecord() {
  if (!server_.hasArg("target")) {
    sendPage("target must be on or off", true);
    return;
  }

  const String target = server_.arg("target");
  if (target != "on" && target != "off") {
    sendPage("target must be on or off", true);
    return;
  }

  ir_remote_.clear();
  LOGI("[Web] IR receiver listening for %s", target.c_str());
  if (!ir_remote_.waitForAvailable(10000)) {
    LOGW("[Web] IR record timeout");
    sendPage("IR recording timed out", true);
    return;
  }

  const auto ir_data = ir_remote_.get();
  if (target == "on") {
    settings_.ir_data_light_on = ir_data;
    settings_store_.saveIrDataLightOn(settings_.ir_data_light_on);
  } else {
    settings_.ir_data_light_off = ir_data;
    settings_store_.saveIrDataLightOff(settings_.ir_data_light_off);
  }

  sendPage(String("IR data recorded for ") + target);
}

void SmartLightWeb::handleAction() {
  if (!server_.hasArg("target") || !server_.hasArg("state")) {
    sendPage("target and state are required", true);
    return;
  }

  const String target = server_.arg("target");
  const String state = server_.arg("state");
  const bool enabled = state == "on";
  if (state != "on" && state != "off") {
    sendPage("state must be on or off", true);
    return;
  }

  if (target == "light") {
    requested_light_state_ = enabled;
    requested_light_state_pending_ = true;
    sendPage(String("light turned ") + state);
    return;
  }
  if (target == "switch") {
    requested_switch_state_ = enabled;
    requested_switch_state_pending_ = true;
    sendPage(String("switch turned ") + state);
    return;
  }

  sendPage("target must be light or switch", true);
}

void SmartLightWeb::handleReboot() {
  server_.send(200, "text/html; charset=utf-8",
               "<!doctype html><html><body><p>Rebooting...</p></body></html>");
  delay(100);
  ESP.restart();
}

void SmartLightWeb::sendPage(const String& message, bool is_error) {
  server_.send(200, "text/html; charset=utf-8", buildPage(message, is_error));
}

String SmartLightWeb::buildPage(const String& message, bool is_error) const {
  String html;
  html.reserve(4096);

  const String ip = WiFi.isConnected() ? WiFi.localIP().toString() : "not connected";
  const String brightness = String(brightness_sensor_.getNormalized(), 3);
  const String ambient_checked =
      settings_.ambient_light_mode_enabled ? "checked" : "";
  const String commissioned = matter_light_.isCommissioned() ? "yes" : "no";
  const String connected = matter_light_.isConnected() ? "yes" : "no";
  const String alert_class = is_error ? "error" : "ok";
  const String light_state = observed_light_state_ ? "ON" : "OFF";
  const String switch_state = observed_switch_state_ ? "ON" : "OFF";

  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Smart Light Settings</title>";
  html +=
      "<style>"
      ":root{color-scheme:light;background:#f4efe7;color:#1f2933;font-family:"
      "\"Avenir Next\",sans-serif;}"
      "body{margin:0;background:linear-gradient(160deg,#f4efe7,#e6f0ea);}"
      ".wrap{max-width:820px;margin:0 auto;padding:24px;}"
      ".card{background:rgba(255,255,255,.88);backdrop-filter:blur(10px);"
      "border:1px solid rgba(31,41,51,.08);border-radius:20px;padding:20px;"
      "box-shadow:0 20px 50px rgba(31,41,51,.08);margin-bottom:18px;}"
      "h1,h2{margin:0 0 12px 0;letter-spacing:.02em;}"
      "p{margin:0 0 10px 0;line-height:1.5;}"
      ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;}"
      "label{display:block;font-size:14px;margin-bottom:6px;font-weight:600;}"
      "input[type=text],input[type=number]{width:100%;box-sizing:border-box;"
      "padding:12px 14px;border-radius:12px;border:1px solid #cdd7df;background:#fff;}"
      ".row{display:flex;gap:10px;flex-wrap:wrap;align-items:center;}"
      "button{border:0;border-radius:999px;padding:11px 16px;font-weight:700;"
      "background:#0f766e;color:#fff;cursor:pointer;}"
      "button.alt{background:#334155;}"
      "button.warn{background:#b45309;}"
      ".muted{color:#52606d;font-size:14px;}"
      ".pill{display:inline-block;padding:4px 10px;border-radius:999px;"
      "background:#e8f4ef;font-size:13px;margin-right:6px;margin-bottom:6px;}"
      ".alert{padding:12px 14px;border-radius:12px;margin-bottom:16px;font-weight:600;}"
      ".alert.ok{background:#d1fae5;color:#065f46;}"
      ".alert.error{background:#fee2e2;color:#991b1b;}"
      "</style></head><body><div class='wrap'>";

  html += "<div class='card'><h1>Smart Light Control</h1>";
  html += "<p class='muted'>Serial console settings are available from the browser.</p>";
  if (message.length() > 0) {
    html += "<div class='alert ";
    html += alert_class;
    html += "'>";
    html += htmlEscape(message);
    html += "</div>";
  }
  html += "<div class='row'>";
  html += "<span class='pill'>IP: " + htmlEscape(ip) + "</span>";
  html += "<span class='pill'>Matter connected: " + connected + "</span>";
  html += "<span class='pill'>Commissioned: " + commissioned + "</span>";
  html += "<span class='pill'>Light: " + light_state + "</span>";
  html += "<span class='pill'>Switch: " + switch_state + "</span>";
  html += "</div></div>";

  html += "<div class='card'><h2>Manual Control</h2>";
  html += "<p class='muted'>These buttons request the same internal states that Matter events update.</p>";
  html += "<div class='row'>";
  html += "<form method='post' action='/action'><input type='hidden' name='target' value='light'><input type='hidden' name='state' value='on'><button type='submit'>Light ON</button></form>";
  html += "<form method='post' action='/action'><input type='hidden' name='target' value='light'><input type='hidden' name='state' value='off'><button class='alt' type='submit'>Light OFF</button></form>";
  html += "<form method='post' action='/action'><input type='hidden' name='target' value='switch'><input type='hidden' name='state' value='on'><button type='submit'>Switch ON</button></form>";
  html += "<form method='post' action='/action'><input type='hidden' name='target' value='switch'><input type='hidden' name='state' value='off'><button class='alt' type='submit'>Switch OFF</button></form>";
  html += "</div></div>";

  html += "<div class='card'><h2>Settings</h2>";
  html += "<form method='post' action='/settings'>";
  html += "<div class='grid'>";
  html += "<div><label for='hostname'>Hostname</label>";
  html += "<input id='hostname' name='hostname' type='text' value='";
  html += htmlEscape(settings_.hostname.c_str());
  html += "'></div>";
  html += "<div><label for='timeout'>Light Off Timeout (seconds)</label>";
  html += "<input id='timeout' name='timeout' type='number' min='1' value='";
  html += String(settings_.light_off_timeout_seconds);
  html += "'></div></div>";
  html += "<p><label><input type='checkbox' name='ambient' ";
  html += ambient_checked;
  html += "> Ambient Light Mode</label></p>";
  html += "<div class='row'><button type='submit'>Save Settings</button></div>";
  html += "</form></div>";

  html += "<div class='card'><h2>Info</h2>";
  html += "<p>Brightness sensor value: " + brightness + "</p>";
  html += "<p>IR ON data size: " + String(settings_.ir_data_light_on.size()) + "</p>";
  html += "<p>IR OFF data size: " + String(settings_.ir_data_light_off.size()) + "</p>";
  html += "</div>";

  html += "<div class='card'><h2>IR Recording</h2>";
  html += "<p class='muted'>Press a button on the remote within 10 seconds after starting recording.</p>";
  html += "<div class='row'>";
  html += "<form method='post' action='/record'><input type='hidden' name='target' value='on'><button class='alt' type='submit'>Record ON</button></form>";
  html += "<form method='post' action='/record'><input type='hidden' name='target' value='off'><button class='alt' type='submit'>Record OFF</button></form>";
  html += "</div></div>";

  html += "<div class='card'><h2>Device</h2>";
  html += "<div class='row'><form method='post' action='/reboot'><button class='warn' type='submit'>Reboot</button></form></div>";
  html += "</div></div></body></html>";

  return html;
}

String SmartLightWeb::htmlEscape(const String& input) const {
  String out;
  out.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#39;";
        break;
      default:
        out += input[i];
        break;
    }
  }
  return out;
}
