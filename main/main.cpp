#include <Matter.h>

#include "button.h"
#include "rgb_led.h"

Button btn(BOOT_PIN, /* long hold timeout [ms] */ 5000);
RgbLed led(PIN_RGB_LED);

MatterOccupancySensor matter_occupancy_sensor;

static constexpr const char* TAG = "main";
void matterEventCallback(matterEvent_t,
                         const chip::DeviceLayer::ChipDeviceEvent*);

void setup() {
  led.setBackground(RgbLed::Color::Green);

  /* Log Levels */
  esp_log_level_set("chip[IM]", ESP_LOG_WARN);
  esp_log_level_set("chip[EM]", ESP_LOG_WARN);
  esp_log_level_set("ROUTE_HOOK", ESP_LOG_WARN);

  /* Matter Endpoint */
  matter_occupancy_sensor.begin();

  /* Matter */
  Matter.onEvent(matterEventCallback);
  Matter.begin();

  /* Matter Commissioning */
  if (!Matter.isDeviceCommissioned()) {
    ESP_LOGI(TAG, "Matter pairing code: %s",
             Matter.getManualPairingCode().c_str());
    ESP_LOGI(TAG, "Matter QR code URL: %s",
             Matter.getOnboardingQRCodeUrl().c_str());
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
  led.update();
  btn.update();

  /* button */
  if (btn.pressed()) ESP_LOGI(TAG, "button pressed");
  if (btn.longPressed()) ESP_LOGI(TAG, "button long pressed");
  if (btn.longHoldStarted()) ESP_LOGI(TAG, "button long hold started");

  /* matter decommissioning */
  if (btn.longPressed()) {
    ESP_LOGI(TAG, "Decommissioning Matter Node...");
    led.setBackground(RgbLed::Color::Green);
    Matter.decommission();
  }

  /* oocupancy sensor (dummy) */
  static bool occupancyState = false;
  if (btn.pressed()) {
    occupancyState = !occupancyState;
    if (occupancyState)
      led.blinkOnce(RgbLed::Color::Cyan);
    else
      led.blinkOnce(RgbLed::Color::Magenta);
  }
  matter_occupancy_sensor.setOccupancy(occupancyState);

  /* wdt release */
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
          led.setBackground(RgbLed::Color::Red);
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
          led.setBackground(RgbLed::Color::Red);
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
          led.setBackground(RgbLed::Color::Red);
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
          led.setBackground(RgbLed::Color::Red);
          break;
        case chip::DeviceLayer::InterfaceIpChangeType::kIpV6_Assigned:
          ESP_LOGW(TAG, "MATTER_INTERFACE_IP_ADDRESS_CHANGED (IPv6 Assigned)");
          break;
        case chip::DeviceLayer::InterfaceIpChangeType::kIpV6_Lost:
          ESP_LOGW(TAG, "MATTER_INTERFACE_IP_ADDRESS_CHANGED (IPv6 Lost)");
          led.setBackground(RgbLed::Color::Red);
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
          led.setBackground(RgbLed::Color::Red);
          break;
        default:
          break;
      }
      break;
    case MATTER_SERVER_READY:
      ESP_LOGW(TAG, "MATTER_SERVER_READY");
      led.setBackground(RgbLed::Color::Blue);
      break;
    default:
      break;
  }
}
