/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <vector>

#include "console/command_parser.h"
#include "device_common/system/app_log.h"
#include "drivers/brightness_sensor.h"
#include "drivers/ir_remote.h"
#include "matter/matter_light.h"
#include "settings/smart_light_settings.h"

class SmartLightCommandHandler {
 public:
  SmartLightCommandHandler(CommandParser& command_parser,
                           SmartLightSettings& settings,
                           SmartLightSettingsStore& settings_store,
                           IRRemote& ir_remote,
                           BrightnessSensor& brightness_sensor,
                           MatterLight& matter_light)
      : command_parser_(command_parser),
        settings_(settings),
        settings_store_(settings_store),
        ir_remote_(ir_remote),
        brightness_sensor_(brightness_sensor),
        matter_light_(matter_light) {}

  bool handle();

 private:
  CommandParser& command_parser_;
  SmartLightSettings& settings_;
  SmartLightSettingsStore& settings_store_;
  IRRemote& ir_remote_;
  BrightnessSensor& brightness_sensor_;
  MatterLight& matter_light_;

  void printHelp() const;
  void handleInfo() const;
  bool handleHostname(const std::vector<std::string>& tokens);
  bool handleRecord(const std::vector<std::string>& tokens);
  bool handleTimeout(const std::vector<std::string>& tokens);
  bool handleAmbient(const std::vector<std::string>& tokens);
  bool handleNightlight(const std::vector<std::string>& tokens);
  bool handleFabricRemove(const std::vector<std::string>& tokens);
};
