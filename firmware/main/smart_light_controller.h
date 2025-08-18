/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

#include <string>

#include "app_config.h"
#include "app_log.h"
#include "brightness_sensor.h"
#include "button.h"
#include "command_parser.h"
#include "ir_remote.h"
#include "matter_light.h"
#include "motion_sensor.h"
#include "rgb_led.h"

class SmartLightController {
 public:
  SmartLightController() = default;

  void begin();
  void handle();

 private:
  Button btn_{CONFIG_APP_PIN_BUTTON};
  RgbLed led_{CONFIG_APP_PIN_RGB_LED};
  MotionSensor motion_sensor_{CONFIG_APP_PIN_MOTION_SENSOR};
  BrightnessSensor brightness_sensor_{CONFIG_APP_PIN_LIGHT_SENSOR};
  IRRemote ir_remote_;
  CommandParser command_parser_{Serial};

  Preferences prefs_;
  std::string hostname_;
  int light_off_timeout_seconds_ = 0;
  bool ambient_light_mode_enabled_ = true;
  IRRemote::IRData ir_data_light_on_;
  IRRemote::IRData ir_data_light_off_;

  MatterLight matter_light_;

  bool last_light_state_ = false;
  bool last_switch_state_ = false;
  bool last_occupancy_state_ = false;

  static constexpr const char* TAG = "SmartLightController";
  static constexpr const char* PREF_PARTITION = "matter";
  static constexpr const char* PREF_HOSTNAME = "hostname";
  static constexpr const char* PREF_TIMEOUT = "timeout";
  static constexpr const char* PREF_AMBIENT = "ambient";
  static constexpr const char* PREF_IR_ON = "ir_on";
  static constexpr const char* PREF_IR_OFF = "ir_off";

  void loadPreferences();
  void setupOta();

  void handleCommands();
  void handleCommissioning();
};

////////////////////////////////////////////////////////////////////////////////

void SmartLightController::begin() {
  led_.setBackground(RgbLed::Color::Green);

  loadPreferences();
  ir_remote_.begin(CONFIG_APP_PIN_IR_TRANSMITTER, CONFIG_APP_PIN_IR_RECEIVER);

  matter_light_.begin(true, true);
  last_light_state_ = !true;  // force trigger

  setupOta();
}

void SmartLightController::handle() {
  ArduinoOTA.handle();
  handleCommands();
  handleCommissioning();

  btn_.update();
  ir_remote_.handle();
  brightness_sensor_.update();

  /* matter event */
  bool light_state = last_light_state_;
  bool switch_state = last_switch_state_;
  MatterLight::Event event;
  if (matter_light_.getEvent(event, 0)) {
    light_state = event.light_state;
    switch_state = event.switch_state;
    switch (event.type) {
      case MatterLight::EventType::LightOn:
        LOGW("[Event] Light ON");
        last_light_state_ = !light_state;  //< force trigger
        break;
      case MatterLight::EventType::LightOff:
        LOGW("[Event] Light OFF");
        last_light_state_ = !light_state;  //< force trigger
        break;
      case MatterLight::EventType::SwitchOn:
        LOGW("[Event] Switch ON");
        break;
      case MatterLight::EventType::SwitchOff:
        LOGW("[Event] Switch OFF");
        break;
    }
  }

  /* button pressed: toggle matter switch */
  if (btn_.pressed()) {
    light_state = !light_state;
    LOGW("[LightState] %d (Button)", light_state);
  }

  /* occupancy sensor */
  int seconds_since_last_motion = motion_sensor_.getSecondsSinceLastMotion();
  bool occupancy_state =
      seconds_since_last_motion < CONFIG_APP_OCCUPANCY_TIMEOUT_SECONDS;

  /* LightState: sync matter switch */
  if (last_light_state_ != light_state) {
    if (occupancy_state) {
      switch_state = light_state;
      LOGW("[SwitchState] %d (LightState)", switch_state);
    } else {
      switch_state = !light_state;
      LOGW("[SwitchState] %d (LightState and No Motion)", switch_state);
    }
  }

  /* SwitchState sync matter light to occupancy sensor */
  if (switch_state) {
    /* Light ON */
    if (!light_state && occupancy_state &&
        (!ambient_light_mode_enabled_ || !brightness_sensor_.isBright())) {
      light_state = true;
      LOGW("[LightState] %d (Occupancy Sensor)", light_state);
    }
    /* Light OFF */
    if (light_state && seconds_since_last_motion > light_off_timeout_seconds_) {
      light_state = false;
      LOGW("[LightState] %d (Occupancy Sensor)", light_state);
    }
  }

  /* IR Receiver */
  if (ir_remote_.available()) {
    const auto ir_data = ir_remote_.get();
    ir_remote_.clear();
    if (IRRemote::isIrDataEqual(ir_data, ir_data_light_on_)) {
      LOGI("[IR] Light ON Signal Received");
      if (!light_state) {
        light_state = true;
        switch_state = true;
      } else {
        switch_state = !switch_state;
      }
      LOGW("[SwitchState] %d (IR)", switch_state);
      led_.blinkOnce(RgbLed::Color::Cyan);
    } else if (IRRemote::isIrDataEqual(ir_data, ir_data_light_off_)) {
      LOGI("[IR] Light OFF Signal Received");
      if (light_state) {
        light_state = false;
        switch_state = false;
      } else {
        switch_state = !switch_state;
      }
      LOGW("[SwitchState] %d (IR)", switch_state);
      led_.blinkOnce(RgbLed::Color::Cyan);
    } else {
      LOGW("[IR] Unknown Signal Received");
      IRRemote::print(ir_data);
    }
  }

  /* update light state */
  bool light_state_changed = false;
  if (last_light_state_ != light_state) {
    last_light_state_ = light_state;
    matter_light_.setLightState(light_state);
    light_state_changed = true;
  }

  /* update switch state */
  if (last_switch_state_ != switch_state) {
    last_switch_state_ = switch_state;
    matter_light_.setSwitchState(switch_state);
  }

  /* IR Transmitter */
  if (light_state_changed) {
    if (light_state) {
      LOGW("[IR] Light ON (size: %zu)", ir_data_light_on_.size());
      led_.blinkOnce(RgbLed::Color::Green);
      ir_remote_.send(ir_data_light_on_);
    } else {
      LOGW("[IR] Light OFF (size: %zu)", ir_data_light_off_.size());
      led_.blinkOnce(RgbLed::Color::Green);
      ir_remote_.send(ir_data_light_off_);
    }
    delay(100);
  }

  /* show occupancy sensor state */
  if (last_occupancy_state_ != occupancy_state) {
    last_occupancy_state_ = occupancy_state;
    if (occupancy_state) {
      LOGW("[PIR] Motion Detected");
    } else {
      LOGW("[PIR] No Motion Timeout");
    }
  }

  /* Status LED */
  if (!matter_light_.isCommissioned()) {
    led_.setBackground(RgbLed::Color::Magenta);
  } else if (!matter_light_.isConnected()) {
    led_.setBackground(RgbLed::Color::Red);
  } else if (switch_state) {
    if (!light_state && ambient_light_mode_enabled_ &&
        brightness_sensor_.isBright()) {
      led_.setBackground(RgbLed::Color::Yellow);
    } else if (occupancy_state) {
      led_.setBackground(RgbLed::Color::Blue);
    } else {
      led_.setBackground(RgbLed::Color::White);
    }
  } else {
    led_.setBackground(RgbLed::Color::Off);
  }
  led_.update();
}

void SmartLightController::loadPreferences() {
  prefs_.begin(PREF_PARTITION);
  hostname_ =
      prefs_.getString(PREF_HOSTNAME, CONFIG_APP_HOSTNAME_DEFAULT).c_str();
  light_off_timeout_seconds_ =
      prefs_.getInt(PREF_TIMEOUT, CONFIG_APP_LIGHT_OFF_TIMEOUT_SECONDS_DEFAULT);
  ambient_light_mode_enabled_ = prefs_.getBool(PREF_AMBIENT, true);
  IRRemote::loadFromPreferences(prefs_, PREF_IR_ON, ir_data_light_on_);
  IRRemote::loadFromPreferences(prefs_, PREF_IR_OFF, ir_data_light_off_);
  LOGI("[%s] IR ON size: %zu", TAG, ir_data_light_on_.size());
  LOGI("[%s] IR OFF size: %zu", TAG, ir_data_light_off_.size());
}

void SmartLightController::setupOta() {
  ArduinoOTA.setHostname(hostname_.c_str());
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([]() {
    auto cmd = ArduinoOTA.getCommand();
    LOGI("[OTA] Start updating %s",
         cmd == U_FLASH ? "sketch"
                        : (cmd == U_SPIFFS ? "filesystem" : "unknown"));
  });
  ArduinoOTA.onEnd([]() { LOGI("[OTA] End"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    LOGI("[OTA] Progress: %u%%", 100 * progress / total);
  });
  ArduinoOTA.onError([](ota_error_t error) { LOGI("[OTA] Error: %d", error); });
  ArduinoOTA.begin();
}

void SmartLightController::handleCommands() {
  command_parser_.update();
  if (!command_parser_.available()) return;
  const auto& tokens = command_parser_.get();
  if (tokens.empty()) return;
  const auto& cmd = tokens[0];

  if (cmd == "help" || cmd == "h") {
    LOGI("Available Commands:");
    LOGI("- help              : Show this help");
    LOGI("- reboot            : Reboot the device");
    LOGI("- info              : Show device information");
    LOGI("- hostname <name>   : Set device hostname (current: %s)",
         hostname_.c_str());
    LOGI("- record <on|off>   : Record IR data for Light ON/OFF");
    LOGI("- timeout <seconds> : Set light OFF timeout in seconds (current: %d)",
         light_off_timeout_seconds_);
    LOGI("- ambient <on|off>  : Ambient Light Mode (current: %s)",
         ambient_light_mode_enabled_ ? "on" : "off");
    return;
  }
  if (cmd == "reboot" || cmd == "b") {
    ESP.restart();
    return;
  }
  if (cmd == "info" || cmd == "i") {
    LOGI("Brightness Sensor Value: %f", brightness_sensor_.getNormalized());
    return;
  }
  if (cmd == "hostname" || cmd == "n") {
    if (tokens.size() < 2) {
      LOGE("Usage: hostname <name>");
      return;
    }
    hostname_ = tokens[1];
    prefs_.putString(PREF_HOSTNAME, hostname_.c_str());
    ArduinoOTA.setHostname(hostname_.c_str());
    return;
  }
  if (cmd == "record" || cmd == "r") {
    if (tokens.size() < 2) {
      LOGE("Usage: record <on|off>");
      return;
    }
    ir_remote_.clear();
    LOGI("[IR] Receiver Listening ...");
    if (!ir_remote_.waitForAvailable(10000)) {
      LOGW("[IR] Timeout");
      return;
    }
    const auto ir_data = ir_remote_.get();
    if (tokens[1] == "on") {
      ir_data_light_on_ = ir_data;
      IRRemote::saveToPreferences(prefs_, PREF_IR_ON, ir_data_light_on_);
    } else if (tokens[1] == "off") {
      ir_data_light_off_ = ir_data;
      IRRemote::saveToPreferences(prefs_, PREF_IR_OFF, ir_data_light_off_);
    }
    return;
  }
  if (cmd == "timeout" || cmd == "t") {
    if (tokens.size() < 2) return;
    int seconds = atoi(tokens[1].c_str());
    if (seconds > 0) {
      light_off_timeout_seconds_ = seconds;
      prefs_.putInt(PREF_TIMEOUT, light_off_timeout_seconds_);
    }
    return;
  }
  if (cmd == "ambient" || cmd == "a") {
    if (tokens.size() < 2) return;
    if (tokens[1] == "on")
      ambient_light_mode_enabled_ = true;
    else if (tokens[1] == "off")
      ambient_light_mode_enabled_ = false;
    prefs_.putBool(PREF_AMBIENT, ambient_light_mode_enabled_);
    return;
  }
}

void SmartLightController::handleCommissioning() {
  if (btn_.longHoldStarted()) led_.blinkOnce(RgbLed::Color::Magenta);
  if (btn_.longPressed()) {
    matter_light_.decommission();
  }
  if (!matter_light_.isCommissioned()) {
    static long last_pairing_log_ms_ = 0;
    long now = millis();
    if (now - last_pairing_log_ms_ > 10000) {
      last_pairing_log_ms_ = now;
      matter_light_.printOnboarding();
    }
  }
}
