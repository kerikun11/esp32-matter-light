/**
 * @copyright 2025 Ryotaro Onuki
 * SPDX-License-Identifier: LGPL-2.1
 */
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
Button btn_(PIN_BUTTON, /* long hold timeout [ms] */ 5000);
RgbLed led_(PIN_RGB_LED);
MotionSensor motion_sensor_(PIN_MOTION_SENSOR);
BrightnessSensor brightness_sensor_(PIN_LIGHT_SENSOR);
IRRemote ir_remote_;

/* Matter */
MatterOnOffLight matter_light_;
MatterOnOffPlugin matter_motion_switch_;

/* Preferences */
Preferences prefs_;
static const char* PREFERENCES_PARTITION_LABEL = "matter";
static const char* PREFERENCES_KEY_TIMEOUT = "timeout";
static const char* PREFERENCES_KEY_AMBIENT = "ambient";
static const char* PREFERENCES_KEY_IR_ON = "ir_on";
static const char* PREFERENCES_KEY_IR_OFF = "ir_off";
static int light_off_timeout_seconds_;
static bool ambient_light_mode_enabled_;
static std::vector<uint16_t> ir_data_light_on_;
static std::vector<uint16_t> ir_data_light_off_;

/* Serial */
CommandParser command_parser_(Serial);

static constexpr const char* TAG = "main";
void matterEventCallback(matterEvent_t,
                         const chip::DeviceLayer::ChipDeviceEvent*);

static void errorHandler(const char* file, int line, const char* func,
                         const char* message) {
  LOGE("Error in %s:%d (%s): %s", file, line, func, message);
  led_.setBackground(RgbLed::Color::Red);
  delay(1000);
  ESP.restart();
}

static void handle_commands() {
  if (!command_parser_.available()) return;
  const auto& tokens = command_parser_.get();
  if (tokens.empty()) return;
  const auto& command = tokens[0];
  LOGI("command: %s", command.c_str());
  if (command == "help" || command == "h") {
    LOGI("Available Commands:");
    LOGI("- help              : Show this help");
    LOGI("- info              : Show device information");
    LOGI("- record <on|off>   : Record IR data for Light ON/OFF");
    LOGI("- timeout <seconds> : Set light OFF timeout in seconds (current: %d)",
         light_off_timeout_seconds_);
    LOGI("- ambient <on|off>  : Ambient Light Mode (current: %s)",
         ambient_light_mode_enabled_ ? "on" : "off");
  } else if (command == "info" || command == "i") {
    LOGI("Brightness Sensor Value: %f", brightness_sensor_.getNormalized());
  } else if (command == "record" || command == "r") {
    if (tokens.size() < 2) {
      LOGE("Usage: record <on|off>");
      return;
    }
    ir_remote_.clear();
    while (!ir_remote_.available()) {
      led_.blinkOnce(RgbLed::Color::Green);
      ir_remote_.handle();
      delay(100);
    }
    const auto& ir_data = ir_remote_.get();
    if (tokens[1] == "on") {
      ir_data_light_on_ = ir_data;
      prefs_.putBytes(PREFERENCES_KEY_IR_ON, ir_data_light_on_.data(),
                      ir_data_light_on_.size() * sizeof(uint16_t));
      LOGI("Recorded IR data for Light ON (%zu)", ir_data_light_on_.size());
    } else if (tokens[1] == "off") {
      ir_data_light_off_ = ir_data;
      prefs_.putBytes(PREFERENCES_KEY_IR_OFF, ir_data_light_off_.data(),
                      ir_data_light_off_.size() * sizeof(uint16_t));
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

  /* Preferences (Light OFF Timeout) */
  light_off_timeout_seconds_ =
      prefs_.getInt(PREFERENCES_KEY_TIMEOUT, LIGHT_OFF_TIMEOUT_SECONDS_DEFAULT);
  ambient_light_mode_enabled_ = prefs_.getBool(PREFERENCES_KEY_AMBIENT, true);

  /* Preferences (IR Data) */
  ir_data_light_on_.resize(prefs_.getBytesLength(PREFERENCES_KEY_IR_ON));
  prefs_.getBytes(PREFERENCES_KEY_IR_ON, ir_data_light_on_.data(),
                  ir_data_light_on_.size() * sizeof(uint16_t));
  LOGI("IR Data Light ON: %zu", ir_data_light_on_.size());
  ir_data_light_off_.resize(prefs_.getBytesLength(PREFERENCES_KEY_IR_OFF));
  prefs_.getBytes(PREFERENCES_KEY_IR_OFF, ir_data_light_off_.data(),
                  ir_data_light_off_.size() * sizeof(uint16_t));
  LOGI("IR Data Light OFF: %zu", ir_data_light_off_.size());
}

void setup() {
  led_.setBackground(RgbLed::Color::Green);

  /* CommandReceiver */
  Serial.begin(CONFIG_MONITOR_BAUD);

  /* Preferences */
  loadPreferences();

  /* IR */
  ir_remote_.begin(PIN_IR_TRANSMITTER, PIN_IR_RECEIVER);

  /* Matter Endpoints */
  matter_light_.begin(true);
  matter_motion_switch_.begin(true);

  /* Matter */
  Matter.onEvent(matterEventCallback);
  Matter.begin();

  /* Matter Commissioning */
  if (!Matter.isDeviceCommissioned()) {
    LOGI("Matter pairing code: %s", Matter.getManualPairingCode().c_str());
    LOGI("Matter QR code URL: %s", Matter.getOnboardingQRCodeUrl().c_str());
    led_.setBackground(RgbLed::Color::Magenta);
    uint32_t timeCount = 0;
    while (!Matter.isDeviceCommissioned()) {
      delay(100);
      if ((timeCount++ % 50) == 0)
        LOGI("Matter commissioning is in progress ...");
    }
    LOGI("Matter Node is commissioned successfully.");
  }
}

void loop() {
  static bool last_matter_light = matter_light_;
  static bool light_state_last = !matter_light_;  //< update first time
  static uint64_t last_matter_light_change_ms = 0;
  static bool last_occupancy_state = false;

  /* update */
  led_.update();
  btn_.update();
  command_parser_.update();
  brightness_sensor_.update();

  /* button state */
  if (btn_.pressed()) LOGI("[Button] pressed");
  if (btn_.longPressed()) LOGI("[Button] long pressed");
  if (btn_.longHoldStarted()) LOGI("[Button] long hold started");

  /* button (long hold): matter decommissioning */
  if (btn_.longPressed()) {
    LOGI("Decommissioning Matter Node...");
    led_.setBackground(RgbLed::Color::Magenta);
    Matter.decommission();
    delay(1000);
    ESP.restart();
  }

  /* button pressed: toggle matter switch */
  if (btn_.pressed()) {
    matter_light_.toggle();
    LOGW("MatterLight: %d (Button)", matter_light_.getOnOff());
  }

  /* occupancy sensor */
  int seconds_since_last_motion = motion_sensor_.getSecondsSinceLastMotion();
  bool occupancy_state = seconds_since_last_motion < OCCUPANCY_TIMEOUT_SECONDS;

  /* matter light changed: sync matter switch */
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
  last_matter_light = matter_light_;

  /* MatterMotionSwitch: auto recovery */
  if (last_matter_light_change_ms >
      AUTO_RECOVERY_TIMEOUT_HOURS * 60 * 60 * 1000) {
    if (!matter_motion_switch_) {
      matter_motion_switch_ = true;
      LOGW("MatterMotionSwitch: %d (Auto ON after 10 hours)",
           matter_motion_switch_.getOnOff());
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

  /* IR transmitter */
  if (light_state_last != matter_light_) {
    light_state_last = matter_light_;
    last_matter_light_change_ms = millis();
    if (light_state_last) {
      LOGW("[IR] Light ON");
      led_.blinkOnce(RgbLed::Color::Green);
      ir_remote_.send(ir_data_light_on_);
    } else {
      LOGW("[IR] Light OFF");
      led_.blinkOnce(RgbLed::Color::Green);
      ir_remote_.send(ir_data_light_off_);
    }
  }

  /* Status LED */
  if (matter_motion_switch_) {
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

  /* Command Parser */
  handle_commands();

  /* WDT Yield */
  delay(1);
}

const char* matterEventToString(uint16_t eventType) {
  switch (eventType) {
    case MATTER_WIFI_CONNECTIVITY_CHANGE:
      return "MATTER_WIFI_CONNECTIVITY_CHANGE";
    case MATTER_THREAD_CONNECTIVITY_CHANGE:
      return "MATTER_THREAD_CONNECTIVITY_CHANGE";
    case MATTER_INTERNET_CONNECTIVITY_CHANGE:
      return "MATTER_INTERNET_CONNECTIVITY_CHANGE";
    case MATTER_SERVICE_CONNECTIVITY_CHANGE:
      return "MATTER_SERVICE_CONNECTIVITY_CHANGE";
    case MATTER_SERVICE_PROVISIONING_CHANGE:
      return "MATTER_SERVICE_PROVISIONING_CHANGE";
    case MATTER_TIME_SYNC_CHANGE:
      return "MATTER_TIME_SYNC_CHANGE";
    case MATTER_CHIPOBLE_CONNECTION_ESTABLISHED:
      return "MATTER_CHIPOBLE_CONNECTION_ESTABLISHED";
    case MATTER_CHIPOBLE_CONNECTION_CLOSED:
      return "MATTER_CHIPOBLE_CONNECTION_CLOSED";
    case MATTER_CLOSE_ALL_BLE_CONNECTIONS:
      return "MATTER_CLOSE_ALL_BLE_CONNECTIONS";
    case MATTER_WIFI_DEVICE_AVAILABLE:
      return "MATTER_WIFI_DEVICE_AVAILABLE";
    case MATTER_OPERATIONAL_NETWORK_STARTED:
      return "MATTER_OPERATIONAL_NETWORK_STARTED";
    case MATTER_THREAD_STATE_CHANGE:
      return "MATTER_THREAD_STATE_CHANGE";
    case MATTER_THREAD_INTERFACE_STATE_CHANGE:
      return "MATTER_THREAD_INTERFACE_STATE_CHANGE";
    case MATTER_CHIPOBLE_ADVERTISING_CHANGE:
      return "MATTER_CHIPOBLE_ADVERTISING_CHANGE";
    case MATTER_INTERFACE_IP_ADDRESS_CHANGED:
      return "MATTER_INTERFACE_IP_ADDRESS_CHANGED";
    case MATTER_COMMISSIONING_COMPLETE:
      return "MATTER_COMMISSIONING_COMPLETE";
    case MATTER_FAIL_SAFE_TIMER_EXPIRED:
      return "MATTER_FAIL_SAFE_TIMER_EXPIRED";
    case MATTER_OPERATIONAL_NETWORK_ENABLED:
      return "MATTER_OPERATIONAL_NETWORK_ENABLED";
    case MATTER_DNSSD_INITIALIZED:
      return "MATTER_DNSSD_INITIALIZED";
    case MATTER_DNSSD_RESTART_NEEDED:
      return "MATTER_DNSSD_RESTART_NEEDED";
    case MATTER_BINDINGS_CHANGED_VIA_CLUSTER:
      return "MATTER_BINDINGS_CHANGED_VIA_CLUSTER";
    case MATTER_OTA_STATE_CHANGED:
      return "MATTER_OTA_STATE_CHANGED";
    case MATTER_SERVER_READY:
      return "MATTER_SERVER_READY";
    case MATTER_BLE_DEINITIALIZED:
      return "MATTER_BLE_DEINITIALIZED";
    case MATTER_COMMISSIONING_SESSION_STARTED:
      return "MATTER_COMMISSIONING_SESSION_STARTED";
    case MATTER_COMMISSIONING_SESSION_STOPPED:
      return "MATTER_COMMISSIONING_SESSION_STOPPED";
    case MATTER_COMMISSIONING_WINDOW_OPEN:
      return "MATTER_COMMISSIONING_WINDOW_OPEN";
    case MATTER_COMMISSIONING_WINDOW_CLOSED:
      return "MATTER_COMMISSIONING_WINDOW_CLOSED";
    case MATTER_FABRIC_WILL_BE_REMOVED:
      return "MATTER_FABRIC_WILL_BE_REMOVED";
    case MATTER_FABRIC_REMOVED:
      return "MATTER_FABRIC_REMOVED";
    case MATTER_FABRIC_COMMITTED:
      return "MATTER_FABRIC_COMMITTED";
    case MATTER_FABRIC_UPDATED:
      return "MATTER_FABRIC_UPDATED";
    case MATTER_ESP32_PUBLIC_SPECIFIC_EVENT:
      return "MATTER_ESP32_PUBLIC_SPECIFIC_EVENT";
    default:
      return "UNKNOWN_EVENT";
  }
}

void matterEventCallback(matterEvent_t event,
                         const chip::DeviceLayer::ChipDeviceEvent* data) {
  LOGW("Matter Event: %s (%d)", matterEventToString(event), event);
  switch (event) {
    case MATTER_WIFI_CONNECTIVITY_CHANGE:
      switch (data->WiFiConnectivityChange.Result) {
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Established:
          LOGW("MATTER_WIFI_CONNECTIVITY_CHANGE (Est)");
          break;
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Lost:
          LOGE("MATTER_WIFI_CONNECTIVITY_CHANGE (Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      break;
    case MATTER_INTERNET_CONNECTIVITY_CHANGE:
      switch (data->InternetConnectivityChange.IPv4) {
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Established:
          LOGW("MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv4 Est)");
          break;
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Lost:
          LOGE("MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv4 Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      switch (data->InternetConnectivityChange.IPv6) {
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Established:
          LOGW("MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv6 Est)");
          break;
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Lost:
          LOGE("MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv6 Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      break;
    case MATTER_INTERFACE_IP_ADDRESS_CHANGED:
      switch (data->InterfaceIpAddressChanged.Type) {
        case chip::DeviceLayer::InterfaceIpChangeType::kIpV4_Assigned:
          LOGW("MATTER_INTERFACE_IP_ADDRESS_CHANGED (IPv4 Assigned)");
          break;
        case chip::DeviceLayer::InterfaceIpChangeType::kIpV4_Lost:
          LOGW("MATTER_INTERFACE_IP_ADDRESS_CHANGED (IPv4 Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        case chip::DeviceLayer::InterfaceIpChangeType::kIpV6_Assigned:
          LOGW("MATTER_INTERFACE_IP_ADDRESS_CHANGED (IPv6 Assigned)");
          break;
        case chip::DeviceLayer::InterfaceIpChangeType::kIpV6_Lost:
          LOGW("MATTER_INTERFACE_IP_ADDRESS_CHANGED (IPv6 Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      switch (data->InternetConnectivityChange.IPv6) {
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Established:
          LOGW("MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv6 Est)");
          break;
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Lost:
          LOGE("MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv6 Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      break;
    case MATTER_SERVER_READY:
      LOGW("MATTER_SERVER_READY");
      led_.setBackground(RgbLed::Color::White);
      break;
    default:
      break;
  }
}
