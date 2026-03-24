/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */

#include "smart_light_commands.h"

#include <cstdlib>

bool SmartLightCommandHandler::handle() {
  command_parser_.update();
  if (!command_parser_.available()) return false;

  const auto tokens = command_parser_.get();
  if (tokens.empty()) return false;

  const auto& cmd = tokens[0];
  if (cmd == "help" || cmd == "h") {
    printHelp();
    return false;
  }
  if (cmd == "reboot" || cmd == "b") {
    ESP.restart();
    return false;
  }
  if (cmd == "info" || cmd == "i") {
    handleInfo();
    return false;
  }
  if (cmd == "hostname" || cmd == "n") {
    return handleHostname(tokens);
  }
  if (cmd == "record" || cmd == "r") {
    return handleRecord(tokens);
  }
  if (cmd == "timeout" || cmd == "t") {
    return handleTimeout(tokens);
  }
  if (cmd == "ambient" || cmd == "a") {
    return handleAmbient(tokens);
  }
  return false;
}

void SmartLightCommandHandler::printHelp() const {
  LOGI("Available Commands:");
  LOGI("- help              : Show this help");
  LOGI("- reboot            : Reboot the device");
  LOGI("- info              : Show device information");
  LOGI("- hostname <name>   : Set device hostname (current: %s)",
       settings_.hostname.c_str());
  LOGI("- record <on|off|night> : Record IR data for Light/Night actions");
  LOGI("- timeout <seconds> : Set light OFF timeout in seconds (current: %d)",
       settings_.light_off_timeout_seconds);
  LOGI("- ambient <on|off>  : Ambient Light Mode (current: %s)",
       settings_.ambient_light_mode_enabled ? "on" : "off");
}

void SmartLightCommandHandler::handleInfo() const {
  LOGI("Brightness Sensor Value: %f", brightness_sensor_.getNormalized());
}

bool SmartLightCommandHandler::handleHostname(
    const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    LOGE("Usage: hostname <name>");
    return false;
  }
  settings_.hostname = tokens[1];
  settings_store_.saveHostname(settings_.hostname);
  return true;
}

bool SmartLightCommandHandler::handleRecord(
    const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    LOGE("Usage: record <on|off|night>");
    return false;
  }

  ir_remote_.clear();
  LOGI("[IR] Receiver Listening ...");
  if (!ir_remote_.waitForAvailable(10000)) {
    LOGW("[IR] Timeout");
    return false;
  }

  const auto ir_data = ir_remote_.get();
  if (tokens[1] == "on") {
    settings_.ir_data_light_on = ir_data;
    settings_store_.saveIrDataLightOn(settings_.ir_data_light_on);
    return false;
  }
  if (tokens[1] == "off") {
    settings_.ir_data_light_off = ir_data;
    settings_store_.saveIrDataLightOff(settings_.ir_data_light_off);
    return false;
  }
  if (tokens[1] == "night") {
    settings_.ir_data_night = ir_data;
    settings_store_.saveIrDataNight(settings_.ir_data_night);
    return false;
  }

  LOGE("Usage: record <on|off|night>");
  return false;
}

bool SmartLightCommandHandler::handleTimeout(
    const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    LOGE("Usage: timeout <seconds>");
    return false;
  }

  const int seconds = atoi(tokens[1].c_str());
  if (seconds <= 0) {
    LOGE("timeout must be greater than 0");
    return false;
  }

  settings_.light_off_timeout_seconds = seconds;
  settings_store_.saveLightOffTimeoutSeconds(seconds);
  return false;
}

bool SmartLightCommandHandler::handleAmbient(
    const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    LOGE("Usage: ambient <on|off>");
    return false;
  }

  if (tokens[1] == "on") {
    settings_.ambient_light_mode_enabled = true;
  } else if (tokens[1] == "off") {
    settings_.ambient_light_mode_enabled = false;
  } else {
    LOGE("Usage: ambient <on|off>");
    return false;
  }

  settings_store_.saveAmbientLightModeEnabled(
      settings_.ambient_light_mode_enabled);
  return false;
}
