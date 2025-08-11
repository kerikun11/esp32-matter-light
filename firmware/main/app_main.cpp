/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Matter.h>
#include <Preferences.h>

#include "app_config.h"
#include "app_log.h"
#include "brightness_sensor.h"
#include "button.h"
#include "command_parser.h"
#include "ir_remote.h"
#include "motion_sensor.h"
#include "rgb_led.h"

/* Device */
Button btn_(CONFIG_APP_PIN_BUTTON);
RgbLed led_(CONFIG_APP_PIN_RGB_LED);
MotionSensor motion_sensor_(CONFIG_APP_PIN_MOTION_SENSOR);
BrightnessSensor brightness_sensor_(CONFIG_APP_PIN_LIGHT_SENSOR);
IRRemote ir_remote_;

/* Matter */
MatterOnOffLight matter_light_;
MatterOnOffPlugin matter_motion_switch_;

/* Console */
CommandParser command_parser_(Serial);

/* Preferences */
Preferences prefs_;
static const char* PREFERENCES_PARTITION_LABEL = "matter";
static const char* PREFERENCES_KEY_HOSTNAME = "hostname";
static const char* PREFERENCES_KEY_TIMEOUT = "timeout";
static const char* PREFERENCES_KEY_AMBIENT = "ambient";
static const char* PREFERENCES_KEY_IR_ON = "ir_on";
static const char* PREFERENCES_KEY_IR_OFF = "ir_off";
static std::string hostname_;
static int light_off_timeout_seconds_;
static bool ambient_light_mode_enabled_;
static IRRemote::IRData ir_data_light_on_;
static IRRemote::IRData ir_data_light_off_;

static constexpr const char* TAG = "main";

static void handle_commands() {
  if (!command_parser_.available()) return;
  const auto& tokens = command_parser_.get();
  if (tokens.empty()) return;
  const auto& command = tokens[0];
  LOGI("command: %s", command.c_str());
  if (command == "help" || command == "h") {
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
  } else if (command == "restart" || command == "b") {
    LOGI("Restarting the device ...");
    ESP.restart();
  } else if (command == "info" || command == "i") {
    LOGI("Brightness Sensor Value: %f", brightness_sensor_.getNormalized());
  } else if (command == "hostname" || command == "n") {
    if (tokens.size() < 2) {
      LOGE("Usage: hostname <name>");
      return;
    }
    hostname_ = tokens[1];
    prefs_.putString(PREFERENCES_KEY_HOSTNAME, hostname_.c_str());
    LOGI("Hostname set to: %s", hostname_.c_str());
  } else if (command == "record" || command == "r") {
    if (tokens.size() < 2) {
      LOGE("Usage: record <on|off>");
      return;
    }
    ir_remote_.clear();
    LOGI("[IR] Receiver Listening ...");
    if (!ir_remote_.waitForAvailable(10'000)) {
      LOGW("[IR] Receiver timeout, please try again.");
      return;
    }
    const auto ir_data = ir_remote_.get();
    if (tokens[1] == "on") {
      ir_data_light_on_ = ir_data;
      IRRemote::saveToPreferences(prefs_, PREFERENCES_KEY_IR_ON,
                                  ir_data_light_on_);
      LOGI("Recorded IR data for Light ON (%zu)", ir_data_light_on_.size());
    } else if (tokens[1] == "off") {
      ir_data_light_off_ = ir_data;
      IRRemote::saveToPreferences(prefs_, PREFERENCES_KEY_IR_OFF,
                                  ir_data_light_off_);
      LOGI("Recorded IR data for Light OFF (%zu)", ir_data_light_off_.size());
    } else {
      LOGE("Invalid argument: %s", tokens[1].c_str());
    }
  } else if (command == "timeout" || command == "t") {
    if (tokens.size() < 2) {
      LOGE("Usage: timeout <seconds>");
      return;
    }
    int seconds = atoi(tokens[1].c_str());
    if (seconds <= 0) {
      LOGE("Invalid timeout value: %d", seconds);
      return;
    }
    light_off_timeout_seconds_ = seconds;
    prefs_.putInt(PREFERENCES_KEY_TIMEOUT, light_off_timeout_seconds_);
    LOGW("Light OFF Timeout set to %d seconds", light_off_timeout_seconds_);
  } else if (command == "ambient" || command == "a") {
    if (tokens.size() < 2) {
      LOGE("Usage: ambient <on|off>");
      return;
    }
    if (tokens[1] == "on") {
      ambient_light_mode_enabled_ = true;
      LOGW("Ambient Light Mode Enabled");
    } else if (tokens[1] == "off") {
      ambient_light_mode_enabled_ = false;
      LOGW("Ambient Light Mode Disabled");
    } else {
      LOGE("Invalid argument: %s", tokens[1].c_str());
      return;
    }
    prefs_.putBool(PREFERENCES_KEY_AMBIENT, ambient_light_mode_enabled_);
  } else {
    LOGE("Unknown command: %s", command.c_str());
  }
}

static void loadPreferences() {
  prefs_.begin(PREFERENCES_PARTITION_LABEL);

  /* hostname */
  hostname_ =
      prefs_.getString(PREFERENCES_KEY_HOSTNAME, CONFIG_APP_HOSTNAME_DEFAULT)
          .c_str();

  /* Preferences (Light OFF Timeout) */
  light_off_timeout_seconds_ = prefs_.getInt(
      PREFERENCES_KEY_TIMEOUT, CONFIG_APP_LIGHT_OFF_TIMEOUT_SECONDS_DEFAULT);
  ambient_light_mode_enabled_ = prefs_.getBool(PREFERENCES_KEY_AMBIENT, true);

  /* Preferences (IR Data) */
  IRRemote::loadFromPreferences(prefs_, PREFERENCES_KEY_IR_ON,
                                ir_data_light_on_);
  IRRemote::loadFromPreferences(prefs_, PREFERENCES_KEY_IR_OFF,
                                ir_data_light_off_);
  LOGI("IR Data Light ON: %zu", ir_data_light_on_.size());
  LOGI("IR Data Light OFF: %zu", ir_data_light_off_.size());
}

static void setupOTA() {
  ArduinoOTA.setHostname(hostname_.c_str());
  ArduinoOTA.setMdnsEnabled(false);  // to avoid mDNS conflicts with Matter
  ArduinoOTA
      .onStart([]() {
        auto cmd = ArduinoOTA.getCommand();
        LOGI("[OTA] Start updating %s", cmd == U_FLASH    ? "sketch"
                                        : cmd == U_SPIFFS ? "filesystem"
                                                          : "unknown");
      })
      .onEnd([]() { LOGI("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total) {
        LOGI("[OTA] Progress: %u%%", 100 * progress / total);
      })
      .onError([](ota_error_t error) { LOGI("[OTA] Error: %d", error); });

  ArduinoOTA.begin();
}

void setup() {
  led_.setBackground(RgbLed::Color::Green);

  /* CommandReceiver */
  Serial.begin(CONFIG_MONITOR_BAUD);

  /* Preferences */
  loadPreferences();

  /* IR */
  ir_remote_.begin(CONFIG_APP_PIN_IR_TRANSMITTER, CONFIG_APP_PIN_IR_RECEIVER);

  /* Matter */
  matter_light_.begin(true);
  matter_motion_switch_.begin(true);
  Matter.begin();

  /* OTA */
  setupOTA();
}

void loop() {
  static bool last_matter_light = matter_light_;
  static bool light_state_last = !matter_light_;  //< update first time
  static bool last_occupancy_state = false;

  /* update */
  led_.update();
  btn_.update();
  command_parser_.update();
  brightness_sensor_.update();
  ir_remote_.handle();
  ArduinoOTA.handle();

  /* button state */
  if (btn_.pressed()) LOGI("[Button] pressed");
  if (btn_.longPressed()) LOGI("[Button] long pressed");
  if (btn_.longHoldStarted()) LOGI("[Button] long hold started");

  /* button pressed: toggle matter switch */
  if (btn_.pressed()) {
    matter_light_.toggle();
    LOGW("MatterLight: %d (Button)", matter_light_.getOnOff());
  }

  /* occupancy sensor */
  int seconds_since_last_motion = motion_sensor_.getSecondsSinceLastMotion();
  bool occupancy_state =
      seconds_since_last_motion < CONFIG_APP_OCCUPANCY_TIMEOUT_SECONDS;

  /* MatterLight: sync matter switch */
  if (last_matter_light != matter_light_) {
    matter_motion_switch_ = matter_light_;
    LOGW("MatterMotionSwitch: %d (MatterLight)",
         matter_motion_switch_.getOnOff());
    if (!occupancy_state) {
      matter_motion_switch_ = !matter_light_;
      LOGW("MatterMotionSwitch: %d (No Motion)",
           matter_motion_switch_.getOnOff());
    }
  }

  /* MatterMotionSwitch: sync matter light to occupancy sensor */
  if (matter_motion_switch_) {
    /* Light ON */
    if (!matter_light_ && occupancy_state &&
        (!ambient_light_mode_enabled_ || !brightness_sensor_.isBright())) {
      matter_light_ = true;
      LOGW("MatterLight: %d (Occupancy Sensor)", matter_light_.getOnOff());
    }
    /* Light OFF */
    if (matter_light_ &&
        seconds_since_last_motion > light_off_timeout_seconds_) {
      matter_light_ = false;
      LOGW("MatterLight: %d (Occupancy Sensor)", matter_light_.getOnOff());
    }
  }

  /* matter occupancy sensor */
  if (last_occupancy_state != occupancy_state) {
    last_occupancy_state = occupancy_state;
    if (occupancy_state) {
      LOGW("[PIR] Motion Detected");
      if (matter_motion_switch_) led_.blinkOnce(RgbLed::Color::Blue);
    } else {
      LOGW("[PIR] No Motion Timeout");
      if (matter_motion_switch_) led_.blinkOnce(RgbLed::Color::Blue);
    }
  }

  /* IR Receiver */
  if (ir_remote_.available()) {
    const auto ir_data = ir_remote_.get();
    ir_remote_.clear();
    if (IRRemote::isIrDataEqual(ir_data, ir_data_light_on_)) {
      LOGI("[IR] Light ON Signal Received");
      if (!matter_light_) {
        matter_light_ = true;
        matter_motion_switch_ = true;
      } else {
        matter_motion_switch_.toggle();
      }
      LOGW("MatterMotionSwitch: %d (IR)", matter_motion_switch_.getOnOff());
      led_.blinkOnce(RgbLed::Color::Cyan);
    } else if (IRRemote::isIrDataEqual(ir_data, ir_data_light_off_)) {
      LOGI("[IR] Light OFF Signal Received");
      if (matter_light_) {
        matter_light_ = false;
        matter_motion_switch_ = false;
      } else {
        matter_motion_switch_.toggle();
      }
      LOGW("MatterMotionSwitch: %d (IR)", matter_motion_switch_.getOnOff());
      led_.blinkOnce(RgbLed::Color::Cyan);
    } else {
      LOGW("[IR] Unknown Signal Received");
    }
  }

  /* IR transmitter */
  if (light_state_last != matter_light_) {
    light_state_last = matter_light_;
    if (light_state_last) {
      LOGW("[IR] Light ON");
      led_.blinkOnce(RgbLed::Color::Green);
      ir_remote_.send(ir_data_light_on_);
    } else {
      LOGW("[IR] Light OFF");
      led_.blinkOnce(RgbLed::Color::Green);
      ir_remote_.send(ir_data_light_off_);
    }
    delay(200);
  }

  /* Status LED */
  if (!Matter.isDeviceCommissioned()) {
    led_.setBackground(RgbLed::Color::Magenta);
  } else if (!Matter.isDeviceConnected()) {
    led_.setBackground(RgbLed::Color::Red);
  } else if (matter_motion_switch_) {
    if (!matter_light_ && ambient_light_mode_enabled_ &&
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

  /* Matter Commissioning */
  if (btn_.longHoldStarted()) led_.blinkOnce(RgbLed::Color::Magenta);
  if (btn_.longPressed()) {
    LOGI("Decommissioning Matter Node...");
    led_.setBackground(RgbLed::Color::Magenta);
    Matter.decommission();
    delay(1000);
    ESP.restart();
  }
  if (!Matter.isDeviceCommissioned()) {
    static long last_millis = 0;
    if (millis() - last_millis > 10000) {
      last_millis = millis();
      LOGI("Matter QR Code URL: %s", Matter.getOnboardingQRCodeUrl().c_str());
      LOGI("Matter Pairing Code: %s", Matter.getManualPairingCode().c_str());
    }
  }

  /* Command Parser */
  handle_commands();

  /* Update Vars */
  last_matter_light = matter_light_;

  /* WDT Yield */
  delay(1);
}
