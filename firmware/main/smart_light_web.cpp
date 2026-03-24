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
  server_.begin();
  LOGI("[Web] HTTP server started on port 80");
}

void SmartLightWeb::handle() { server_.handleClient(); }

void SmartLightWeb::setObservedStates(bool light_state, bool switch_state,
                                      int ambient_light_percent) {
  observed_light_state_ = light_state;
  observed_switch_state_ = switch_state;
  observed_ambient_light_percent_ = ambient_light_percent;
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
  const String hostname = server_.arg("hostname");
  const int timeout_seconds = server_.arg("timeout").toInt();
  const int ambient_threshold = server_.arg("ambient_threshold").toInt();
  const bool ambient_enabled = server_.hasArg("ambient");
  settings_.hostname = hostname.length() ? hostname.c_str() : settings_.hostname;
  settings_.light_off_timeout_seconds =
      timeout_seconds > 0 ? timeout_seconds : settings_.light_off_timeout_seconds;
  settings_.ambient_light_mode_enabled = ambient_enabled;
  if (ambient_threshold >= 0 && ambient_threshold <= 100) {
    settings_.ambient_light_threshold_percent = ambient_threshold;
  }

  settings_store_.saveHostname(settings_.hostname);
  settings_store_.saveLightOffTimeoutSeconds(settings_.light_off_timeout_seconds);
  settings_store_.saveAmbientLightModeEnabled(ambient_enabled);
  settings_store_.saveAmbientLightThresholdPercent(
      settings_.ambient_light_threshold_percent);
  hostname_updated_ = true;
  redirectRoot();
}

void SmartLightWeb::handleRecord() {
  const String target = server_.arg("target");
  if (target != "on" && target != "off") return redirectRoot();

  ir_remote_.clear();
  if (!ir_remote_.waitForAvailable(10000)) return redirectRoot();

  const auto ir_data = ir_remote_.get();
  if (target == "on") {
    settings_.ir_data_light_on = ir_data;
    settings_store_.saveIrDataLightOn(settings_.ir_data_light_on);
  } else {
    settings_.ir_data_light_off = ir_data;
    settings_store_.saveIrDataLightOff(settings_.ir_data_light_off);
  }
  redirectRoot();
}

void SmartLightWeb::handleAction() {
  const String target = server_.arg("target");
  const String state = server_.arg("state");
  const bool enabled = state == "on";
  if (state != "on" && state != "off") return redirectRoot();

  if (target == "light") {
    requested_light_state_ = enabled;
    requested_light_state_pending_ = true;
    return redirectRoot();
  }
  if (target == "switch") {
    requested_switch_state_ = enabled;
    requested_switch_state_pending_ = true;
    return redirectRoot();
  }
  if (target == "ambient") {
    settings_.ambient_light_mode_enabled = enabled;
    settings_store_.saveAmbientLightModeEnabled(enabled);
    return redirectRoot();
  }
  redirectRoot();
}

void SmartLightWeb::redirectRoot() {
  server_.sendHeader("Location", "/", true);
  server_.send(303, "text/plain", "");
}

void SmartLightWeb::sendPage() { server_.send(200, "text/html", buildPage()); }

String SmartLightWeb::buildPage() const {
  String html;
  html.reserve(4096);

  const String ambient_threshold = String(settings_.ambient_light_threshold_percent);
  const String light_state = observed_light_state_ ? "ON" : "OFF";
  const String switch_state = observed_switch_state_ ? "ON" : "OFF";
  const String light_class = observed_light_state_ ? "on" : "off";
  const String switch_class = observed_switch_state_ ? "on" : "off";
  const String ambient_value = String(observed_ambient_light_percent_);
  const String ambient_state =
      settings_.ambient_light_mode_enabled ? "Enabled" : "Disabled";
  const String ambient_class =
      settings_.ambient_light_mode_enabled ? "on" : "off";

  html += "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Smart Light Controller</title>";
  html += "<style>:root{--bg:#f3f6fb;--panel:#ffffff;--line:#dbe3ef;--ink:#17212b;--muted:#64748b;--accent:#2563eb;--accent-soft:#e8f0ff;--good:#15803d;--good-soft:#e8f7ee;--bad:#b42318;--bad-soft:#fdecec;--shadow:0 10px 30px rgba(15,23,42,.08)}*{box-sizing:border-box}body{margin:0;background:linear-gradient(180deg,#f8fbff 0,#eef3f9 100%);color:var(--ink);font:15px/1.5 Inter,Segoe UI,Helvetica,sans-serif}main{max-width:880px;margin:0 auto;padding:24px 16px 40px}.hero,.card,.fold{background:var(--panel);border:1px solid var(--line);border-radius:20px;box-shadow:var(--shadow)}.hero{padding:24px}.hero h1{margin:0 0 6px;font-size:32px;line-height:1.05}.sub{margin:0;color:var(--muted)}.topbar,.stats,.control-grid,.two-col{display:grid;gap:14px}.topbar{grid-template-columns:1.25fr .75fr;margin-top:18px}.stats{grid-template-columns:repeat(2,1fr)}.stat,.card{padding:18px}.label{display:block;margin-bottom:8px;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.value{font-size:24px;font-weight:700}.meta{margin-top:10px;color:var(--muted);font-size:14px}.card h2{margin:0 0 14px;font-size:20px}.control-grid{grid-template-columns:repeat(3,1fr)}.control-item{display:grid;gap:10px}.toggle-form{margin:0}.toggle-btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;min-width:116px;padding:10px 14px;border:none;border-radius:999px;font-size:14px;font-weight:700;cursor:pointer;transition:transform .16s ease,box-shadow .16s ease,filter .16s ease}.toggle-btn:before{content:'';width:8px;height:8px;border-radius:50%;background:currentColor}.toggle-btn.on{background:var(--good-soft);color:var(--good)}.toggle-btn.off{background:var(--bad-soft);color:var(--bad)}.toggle-btn:hover{transform:translateY(-1px);box-shadow:0 8px 18px rgba(15,23,42,.08);filter:brightness(.96)}.two-col{grid-template-columns:1fr;margin-top:14px}.stack{display:grid;gap:14px}form{margin:0}.field{display:grid;gap:6px}.field span{color:var(--muted);font-size:13px}.row{display:flex;align-items:center;justify-content:space-between;gap:12px}.range-row{display:grid;grid-template-columns:1fr auto;gap:12px;align-items:center}input[type=text],input[type=number]{width:100%;padding:12px 13px;border:1px solid var(--line);border-radius:12px;background:#fff;color:var(--ink);font:inherit}input[type=range]{width:100%;accent-color:var(--accent)}button{appearance:none;border:none;border-radius:12px;padding:11px 14px;background:var(--accent);color:#fff;font:700 14px/1 Inter,Segoe UI,sans-serif;cursor:pointer;transition:background-color .16s ease,transform .16s ease,box-shadow .16s ease}button:hover{background:#1d4ed8;box-shadow:0 8px 18px rgba(37,99,235,.18)}button.warn{background:#0f172a;color:#fff}button.warn:hover{background:#1e293b}.mini{font-size:13px;color:var(--muted)}details.fold{overflow:hidden;margin-top:14px}details.fold summary{list-style:none;cursor:pointer;padding:18px 20px;font-weight:700}details.fold summary::-webkit-details-marker{display:none}details.fold summary span{color:var(--muted);font-weight:600;margin-left:8px}details.fold .body{padding:0 20px 20px}@media (max-width:760px){.topbar,.stats,.control-grid,.two-col{grid-template-columns:1fr}.hero h1{font-size:28px}}</style></head><body><main>";

  html += "<section class=hero><h1><a href=/ style='color:inherit;text-decoration:none'>Smart Light Controller</a></h1><div class=topbar><section class=card><div class=control-grid><div class=control-item><span class=label>Light</span><form class=toggle-form method=post action=/action><input type=hidden name=target value=light><input type=hidden name=state value='";
  html += observed_light_state_ ? "off" : "on";
  html += "'><button class='toggle-btn ";
  html += light_class;
  html += "'>";
  html += light_state;
  html += "</button></form></div><div class=control-item><span class=label>Motion Link</span><form class=toggle-form method=post action=/action><input type=hidden name=target value=switch><input type=hidden name=state value='";
  html += observed_switch_state_ ? "off" : "on";
  html += "'><button class='toggle-btn ";
  html += switch_class;
  html += "'>";
  html += switch_state;
  html += "</button></form></div><div class=control-item><span class=label>Ambient Mode</span><form class=toggle-form method=post action=/action><input type=hidden name=target value=ambient><input type=hidden name=state value='";
  html += settings_.ambient_light_mode_enabled ? "off" : "on";
  html += "'><button class='toggle-btn ";
  html += ambient_class;
  html += "'>";
  html += ambient_state;
  html += "</button></form></div></div></section><div class=stats>";
  html += "<div class=stat><span class=label>Ambient Light</span><div class=value>";
  html += ambient_value;
  html += "%</div></div></div></div></section>";

  html += "<div class=two-col><details class=fold><summary>Settings<span>Click to expand</span></summary><div class=body><form class=stack method=post action=/settings><label class=field><span>Hostname</span><input name=hostname value='";
  html += settings_.hostname.c_str();
  html += "'></label><div class=row><label class=field style='flex:1'><span>Light Off Timeout</span><input name=timeout type=number min=1 value='";
  html += String(settings_.light_off_timeout_seconds);
  html += "'></label></div><div class=field><span>Ambient Mode Threshold</span><div class=range-row><input id=t name=ambient_threshold type=range min=0 max=100 value='";
  html += ambient_threshold;
  html += "' oninput='tv.textContent=this.value'><strong><span id=tv>";
  html += ambient_threshold;
  html += "</span>%</strong></div></div><div class=row><p class=mini>Changes are stored immediately after save.</p><button>Save Settings</button></div></form></div></details><details class=fold><summary>Learn IR<span>Click to expand</span></summary><div class=body><p class=mini>Press a record button, then send the remote signal within 10 seconds.</p><form class=group method=post action=/record><button class=warn name=target value=on>Record ON</button><button class=warn name=target value=off>Record OFF</button></form></div></details></div>";

  html += "</main></body></html>";

  return html;
}
