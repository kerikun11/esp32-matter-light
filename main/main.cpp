/**
 * @copyright 2025 Ryotaro Onuki
 * @license LGPL-2.1
 */
#include <Matter.h>

#include "brightness_sensor.h"
#include "button.h"
#include "ir_transmitter.h"
#include "pir_sensor.h"
#include "rgb_led.h"

/* Pins */
#define PIN_BUTTON BOOT_PIN
#define PIN_PIR_SENSOR 1
#define PIN_LIGHT_SENSOR 2
#define PIN_IR_TRANSMITTER 4
// #define PIN_IR_RECEIVER 5

/* Device */
Button btn_(PIN_BUTTON, /* long hold timeout [ms] */ 5000);
RgbLed led_(PIN_RGB_LED);
PirSensor pir_sensor_(PIN_PIR_SENSOR, /* clear delay [ms] */ 30'000);
BrightnessSensor brightness_sensor_(PIN_LIGHT_SENSOR);
IRTransmitter ir_transmitter_(PIN_IR_TRANSMITTER);

/* Matter */
MatterOnOffLight matter_light_;
MatterOnOffPlugin matter_switch_;
MatterOccupancySensor matter_occupancy_sensor_;

/* IR Data (Tentative) */
static constexpr uint16_t kIrRawDataLightON1[] = {
    3519, 1707, 471,  400,  468, 402,  468, 1271, 470, 1271, 471, 400,
    469,  1271, 470,  401,  469, 400,  468, 400,  469, 1272, 470, 400,
    470,  399,  469,  1272, 470, 401,  468, 1272, 471, 400,  469, 1272,
    470,  400,  469,  400,  469, 1272, 470, 401,  469, 400,  469, 400,
    468,  401,  469,  1272, 470, 401,  468, 1272, 470, 1272, 471, 400,
    469,  1272, 471,  400,  469, 400,  468, 401,  468, 401,  469, 1272,
    470,  400,  469,  400,  469, 1272, 471, 400,  469, 401,  469, 65535,
    0,    9453, 3522, 1709, 470, 401,  469, 400,  469, 1271, 472, 1271,
    471,  401,  468,  1272, 470, 402,  509, 361,  468, 401,  469, 1272,
    471,  401,  468,  401,  469, 1272, 470, 401,  469, 1272, 471, 401,
    469,  1272, 470,  401,  469, 401,  469, 1272, 470, 401,  468, 401,
    469,  401,  468,  401,  469, 1271, 471, 401,  469, 1273, 470, 1272,
    470,  402,  468,  1272, 470, 401,  469, 402,  468, 401,  469, 400,
    469,  1272, 471,  400,  469, 401,  468, 1273, 470, 401,  469, 402,
    469};
static constexpr uint16_t kIrRawDataLightOFF1[] = {
    3520, 1708, 470,  401,  468, 401,  468, 1273, 469, 1272, 470, 401,
    468,  1271, 471,  400,  469, 400,  468, 401,  468, 1272, 470, 400,
    469,  401,  468,  1272, 470, 400,  469, 1272, 470, 400,  469, 1273,
    469,  401,  469,  400,  468, 1273, 469, 401,  468, 401,  468, 401,
    468,  400,  469,  1272, 470, 1272, 470, 1272, 470, 1272, 470, 400,
    469,  1271, 470,  400,  469, 400,  469, 400,  469, 1271, 470, 1272,
    469,  401,  469,  400,  468, 1272, 470, 401,  468, 401,  469, 65535,
    0,    9427, 3523, 1707, 472, 400,  469, 399,  470, 1271, 470, 1270,
    472,  399,  470,  1271, 470, 400,  469, 400,  469, 399,  470, 1271,
    471,  399,  469,  395,  474, 1271, 471, 399,  469, 1272, 470, 401,
    468,  1271, 471,  399,  470, 399,  470, 1271, 471, 399,  470, 399,
    469,  400,  469,  400,  469, 1272, 470, 1272, 469, 1272, 471, 1271,
    471,  400,  469,  1273, 469, 401,  469, 401,  469, 400,  469, 1272,
    470,  1272, 470,  400,  470, 400,  469, 1270, 472, 400,  468, 402,
    469};

static constexpr const char* TAG = "main";
void matterEventCallback(matterEvent_t,
                         const chip::DeviceLayer::ChipDeviceEvent*);

void setup() {
  led_.setBackground(RgbLed::Color::Green);

  /* Log Levels */
  esp_log_level_set("chip[IM]", ESP_LOG_WARN);
  esp_log_level_set("chip[EM]", ESP_LOG_WARN);
  esp_log_level_set("ROUTE_HOOK", ESP_LOG_WARN);

  /* Matter Endpoint */
  matter_light_.begin(false);
  matter_switch_.begin(true);
  matter_occupancy_sensor_.begin();

  /* Matter */
  Matter.onEvent(matterEventCallback);
  Matter.begin();

  /* Matter Commissioning */
  if (!Matter.isDeviceCommissioned()) {
    ESP_LOGI(TAG, "Matter pairing code: %s",
             Matter.getManualPairingCode().c_str());
    ESP_LOGI(TAG, "Matter QR code URL: %s",
             Matter.getOnboardingQRCodeUrl().c_str());
    led_.setBackground(RgbLed::Color::Magenta);
    uint32_t timeCount = 0;
    while (!Matter.isDeviceCommissioned()) {
      delay(100);
      if ((timeCount++ % 50) == 0)
        ESP_LOGI(TAG, "Matter commissioning is in progress ...");
    }
    ESP_LOGI(TAG, "Matter Node is commissioned successfully.");
  }
}

void loop() {
  /* update */
  led_.update();
  btn_.update();
  pir_sensor_.update();
  brightness_sensor_.update();

  /* button state */
  if (btn_.pressed()) ESP_LOGI(TAG, "button pressed");
  if (btn_.longPressed()) ESP_LOGI(TAG, "button long pressed");
  if (btn_.longHoldStarted()) ESP_LOGI(TAG, "button long hold started");

  /* button (long hold): matter decommissioning */
  if (btn_.longPressed()) {
    ESP_LOGI(TAG, "Decommissioning Matter Node...");
    led_.setBackground(RgbLed::Color::Magenta);
    Matter.decommission();
  }

  /* button: toggle matter switch */
  if (btn_.pressed()) {
    matter_switch_.toggle();
    ESP_LOGW(TAG, "Enabled: %d (Button Pressed)", matter_switch_.getOnOff());
  }

  /* matter light: matter switch (sync) */
  static bool last_matter_light = false;
  if (last_matter_light != matter_light_) {
    last_matter_light = matter_light_;
    matter_switch_ = matter_light_;
    ESP_LOGW(TAG, "Enabled: %d (Matter Light)", matter_switch_.getOnOff());
  }

  /* brightness sensor ON: matter switch ON */
  // if (brightness_sensor_.getElapsedSinceChange() > 5000 &&
  //     brightness_sensor_.isBright() && !matter_light_ && !matter_switch_) {
  //   matter_switch_ = true;
  //   ESP_LOGW(TAG, "Enabled: %d (Brightness Sensor ON)",
  //            matter_switch_.getOnOff());
  // }

  /* occupancy sensor: matter switch (sync) */
  bool occupancy_sensor_state = pir_sensor_.motionDetected();
  if (matter_occupancy_sensor_ != occupancy_sensor_state) {
    matter_occupancy_sensor_ = occupancy_sensor_state;
    if (occupancy_sensor_state) {
      ESP_LOGW(TAG, "[PIR] motion detected");
      led_.blinkOnce(RgbLed::Color::Yellow);
    } else {
      ESP_LOGW(TAG, "[PIR] no motion timeouted");
      led_.blinkOnce(RgbLed::Color::Cyan);
    }
  }
  if (matter_switch_) matter_light_ = occupancy_sensor_state;

  /* Status LED */
  led_.setBackground(matter_switch_ ? RgbLed::Color::White
                                    : RgbLed::Color::Off);

  /* IR transmitter */
  static bool light_state_last = false;
  if (light_state_last != matter_light_) {
    light_state_last = matter_light_;
    if (light_state_last) {
      ESP_LOGW(TAG, "Light ON");
      led_.blinkOnce(RgbLed::Color::Green);
      ir_transmitter_.sendRaw(std::vector<uint16_t>(
          std::begin(kIrRawDataLightON1), std::end(kIrRawDataLightON1)));
    } else {
      ESP_LOGW(TAG, "Light OFF");
      led_.blinkOnce(RgbLed::Color::Green);
      ir_transmitter_.sendRaw(std::vector<uint16_t>(
          std::begin(kIrRawDataLightOFF1), std::end(kIrRawDataLightOFF1)));
    }
  }

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
  ESP_LOGW(TAG, "Matter Event: %s (%d)", matterEventToString(event), event);
  switch (event) {
    case MATTER_WIFI_CONNECTIVITY_CHANGE:
      switch (data->WiFiConnectivityChange.Result) {
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Established:
          ESP_LOGW(TAG, "MATTER_WIFI_CONNECTIVITY_CHANGE (Est)");
          break;
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Lost:
          ESP_LOGE(TAG, "MATTER_WIFI_CONNECTIVITY_CHANGE (Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      break;
    case MATTER_INTERNET_CONNECTIVITY_CHANGE:
      switch (data->InternetConnectivityChange.IPv4) {
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Established:
          ESP_LOGW(TAG, "MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv4 Est)");
          break;
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Lost:
          ESP_LOGE(TAG, "MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv4 Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      switch (data->InternetConnectivityChange.IPv6) {
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Established:
          ESP_LOGW(TAG, "MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv6 Est)");
          break;
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Lost:
          ESP_LOGE(TAG, "MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv6 Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      break;
    case MATTER_INTERFACE_IP_ADDRESS_CHANGED:
      switch (data->InterfaceIpAddressChanged.Type) {
        case chip::DeviceLayer::InterfaceIpChangeType::kIpV4_Assigned:
          ESP_LOGW(TAG, "MATTER_INTERFACE_IP_ADDRESS_CHANGED (IPv4 Assigned)");
          break;
        case chip::DeviceLayer::InterfaceIpChangeType::kIpV4_Lost:
          ESP_LOGW(TAG, "MATTER_INTERFACE_IP_ADDRESS_CHANGED (IPv4 Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        case chip::DeviceLayer::InterfaceIpChangeType::kIpV6_Assigned:
          ESP_LOGW(TAG, "MATTER_INTERFACE_IP_ADDRESS_CHANGED (IPv6 Assigned)");
          break;
        case chip::DeviceLayer::InterfaceIpChangeType::kIpV6_Lost:
          ESP_LOGW(TAG, "MATTER_INTERFACE_IP_ADDRESS_CHANGED (IPv6 Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      switch (data->InternetConnectivityChange.IPv6) {
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Established:
          ESP_LOGW(TAG, "MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv6 Est)");
          break;
        case chip::DeviceLayer::ConnectivityChange::kConnectivity_Lost:
          ESP_LOGE(TAG, "MATTER_INTERNET_CONNECTIVITY_CHANGE (IPv6 Lost)");
          led_.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      break;
    case MATTER_SERVER_READY:
      ESP_LOGW(TAG, "MATTER_SERVER_READY");
      led_.setBackground(RgbLed::Color::Blue);
      break;
    default:
      break;
  }
}
