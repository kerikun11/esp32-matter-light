/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <vector>

#include "app_log.h"
#include "brightness_sensor.h"
#include "command_parser.h"
#include "ir_remote.h"
#include "smart_light_settings.h"

class SmartLightCommandHandler {
 public:
  SmartLightCommandHandler(CommandParser& command_parser,
                           SmartLightSettings& settings,
                           SmartLightSettingsStore& settings_store,
                           IRRemote& ir_remote,
                           BrightnessSensor& brightness_sensor)
      : command_parser_(command_parser),
        settings_(settings),
        settings_store_(settings_store),
        ir_remote_(ir_remote),
        brightness_sensor_(brightness_sensor) {}

  bool handle();

 private:
  CommandParser& command_parser_;
  SmartLightSettings& settings_;
  SmartLightSettingsStore& settings_store_;
  IRRemote& ir_remote_;
  BrightnessSensor& brightness_sensor_;

  void printHelp() const;
  void handleInfo() const;
  bool handleHostname(const std::vector<std::string>& tokens);
  bool handleRecord(const std::vector<std::string>& tokens);
  bool handleTimeout(const std::vector<std::string>& tokens);
  bool handleAmbient(const std::vector<std::string>& tokens);
};
