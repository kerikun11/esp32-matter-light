/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <app-common/zap-generated/ids/Clusters.h>
#include <app/server/Server.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_attribute.h>
#include <esp_matter_cluster.h>
#include <esp_matter_core.h>
#include <esp_matter_endpoint.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <inttypes.h>
#include <platform/ConfigurationManager.h>
#include <system/SystemClock.h>

class MatterLight {
 public:
  enum class EventType : uint8_t {
    LightOn,
    LightOff,
    SwitchOn,
    SwitchOff,
    NightOn,
    NightOff,
  };

  struct Event {
    uint64_t timestamp_ms;
    EventType type;
    bool light_state;
    bool switch_state;
    bool night_state;
  };

  static constexpr const char *kManualCode = "34970112332";
  static constexpr const char *kQrUrl =
      "https://project-chip.github.io/connectedhomeip/"
      "qrcode.html?data=MT:Y.K9042C00KA0648G00";

  bool begin(bool initial_light_on, bool initial_switch_on,
             bool initial_night_on = false, bool enable_night_endpoint = true) {
    esp_matter::node::config_t node_cfg{};
    node_ = esp_matter::node::create(&node_cfg, &MatterLight::attrCb_, nullptr,
                                     this);
    if (!node_) {
      ESP_LOGE(TAG, "node::create failed");
      return false;
    }

    // Light endpoint
    {
      esp_matter::endpoint::on_off_light::config_t cfg{};
      cfg.on_off.on_off = initial_light_on;
      ep_light_ =
          esp_matter::endpoint::on_off_light::create(node_, &cfg, 0, this);
      if (!ep_light_ || !setOnOffAttr_(ep_light_, initial_light_on)) {
        ESP_LOGE(TAG, "light::create failed");
        return false;
      }
    }

    // Plugin endpoint (switch)
    {
      esp_matter::endpoint::on_off_plugin_unit::config_t cfg{};
      cfg.on_off.on_off = initial_switch_on;
      ep_plugin_ = esp_matter::endpoint::on_off_plugin_unit::create(node_, &cfg,
                                                                    0, this);
      if (!ep_plugin_ || !setOnOffAttr_(ep_plugin_, initial_switch_on)) {
        ESP_LOGE(TAG, "plugin::create failed");
        return false;
      }
    }

    // Plugin endpoint (night) - only when feature is enabled
    if (enable_night_endpoint) {
      esp_matter::endpoint::on_off_plugin_unit::config_t cfg{};
      cfg.on_off.on_off = initial_night_on;
      ep_night_ =
          esp_matter::endpoint::on_off_plugin_unit::create(node_, &cfg, 0, this);
      if (!ep_night_ || !setOnOffAttr_(ep_night_, initial_night_on)) {
        ESP_LOGE(TAG, "night::create failed");
        return false;
      }
    }

    if (!registerInstance_(this)) {
      ESP_LOGE(TAG, "instance registry full");
      return false;
    }

    queue_ = xQueueCreate(kQueueSize, sizeof(Event));
    if (!queue_) {
      ESP_LOGE(TAG, "xQueueCreate failed");
      return false;
    }

    if (esp_matter::start(nullptr) != ESP_OK) {
      ESP_LOGE(TAG, "esp_matter::start failed");
      return false;
    }

    if (ep_night_) {
      ESP_LOGI(TAG,
               "light_ep=0x%04x(%s) plugin_ep=0x%04x(%s) night_ep=0x%04x(%s)",
               esp_matter::endpoint::get_id(ep_light_),
               initial_light_on ? "ON" : "OFF",
               esp_matter::endpoint::get_id(ep_plugin_),
               initial_switch_on ? "ON" : "OFF",
               esp_matter::endpoint::get_id(ep_night_),
               initial_night_on ? "ON" : "OFF");
    } else {
      ESP_LOGI(TAG,
               "light_ep=0x%04x(%s) plugin_ep=0x%04x(%s) night_ep=disabled",
               esp_matter::endpoint::get_id(ep_light_),
               initial_light_on ? "ON" : "OFF",
               esp_matter::endpoint::get_id(ep_plugin_),
               initial_switch_on ? "ON" : "OFF");
    }
    printOnboarding();
    return true;
  }

  bool getEvent(Event &out, TickType_t ticks = portMAX_DELAY) {
    return queue_ && (xQueueReceive(queue_, &out, ticks) == pdTRUE);
  }

  void printOnboarding() const {
    ESP_LOGI(TAG, "Manual: %s", kManualCode);
    ESP_LOGI(TAG, "QR    : %s", kQrUrl);
  }

  bool isConnected() const {
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
  }
  bool isCommissioned() const {
    auto &srv = chip::Server::GetInstance();
    return (srv.GetFabricTable().FabricCount() > 0) &&
           !srv.GetCommissioningWindowManager().IsCommissioningWindowOpen();
  }

  bool setLightState(bool on) { return setOnOffAttr_(ep_light_, on); }
  bool setSwitchState(bool on) { return setOnOffAttr_(ep_plugin_, on); }
  bool setNightState(bool on) { return setOnOffAttr_(ep_night_, on); }

  bool openCommissioningWindow(uint16_t timeout_seconds = 300) {
    auto err = chip::Server::GetInstance().GetCommissioningWindowManager()
                   .OpenBasicCommissioningWindow(
                       chip::System::Clock::Seconds32(timeout_seconds));
    if (err != CHIP_NO_ERROR) {
      ESP_LOGE(TAG, "OpenBasicCommissioningWindow failed: %" CHIP_ERROR_FORMAT,
               err.Format());
      return false;
    }
    printOnboarding();
    return true;
  }

  void decommission() {
    ESP_LOGW(TAG, "Decommissioning device...");
    chip::Server::GetInstance().GetFabricTable().DeleteAllFabrics();
    chip::DeviceLayer::ConfigurationMgr().InitiateFactoryReset();
  }

 private:
  static constexpr const char *TAG = "MatterLight";
  static constexpr size_t kQueueSize = 8;
  static constexpr size_t kMaxInstances = 8;

  esp_matter::node_t *node_ = nullptr;
  esp_matter::endpoint_t *ep_light_ = nullptr;
  esp_matter::endpoint_t *ep_plugin_ = nullptr;
  esp_matter::endpoint_t *ep_night_ = nullptr;
  QueueHandle_t queue_ = nullptr;

  bool setOnOffAttr_(esp_matter::endpoint_t *ep, bool on) {
    if (!ep) return false;
    auto *cluster =
        esp_matter::cluster::get(ep, chip::app::Clusters::OnOff::Id);
    if (!cluster) return false;
    auto *attr = esp_matter::attribute::get(
        cluster, chip::app::Clusters::OnOff::Attributes::OnOff::Id);
    if (!attr) return false;
    esp_matter_attr_val_t v = esp_matter_bool(on);
    return esp_matter::attribute::set_val(attr, &v) == ESP_OK;
  }

  bool readOnAttr_(esp_matter::endpoint_t *ep, bool &out) const {
    out = false;
    if (!ep) return false;
    auto *cluster =
        esp_matter::cluster::get(ep, chip::app::Clusters::OnOff::Id);
    if (!cluster) return false;
    auto *attr = esp_matter::attribute::get(
        cluster, chip::app::Clusters::OnOff::Attributes::OnOff::Id);
    if (!attr) return false;
    esp_matter_attr_val_t v{};
    if (esp_matter::attribute::get_val(attr, &v) != ESP_OK) return false;
    out = v.val.b;
    return true;
  }

  static esp_err_t attrCb_(esp_matter::attribute::callback_type_t type,
                           uint16_t endpoint_id, uint32_t cluster_id,
                           uint32_t attribute_id, esp_matter_attr_val_t *val,
                           void *) {
    if (type != esp_matter::attribute::POST_UPDATE ||
        cluster_id != chip::app::Clusters::OnOff::Id ||
        attribute_id !=
            chip::app::Clusters::OnOff::Attributes::OnOff::Id ||
        !val) {
      return ESP_OK;
    }

    MatterLight *self = findOwnerByEndpoint_(endpoint_id);
    if (!self) return ESP_OK;

    const uint16_t ep_light = esp_matter::endpoint::get_id(self->ep_light_);
    const uint16_t ep_plugin = esp_matter::endpoint::get_id(self->ep_plugin_);
    const uint16_t ep_night = self->ep_night_
                                  ? esp_matter::endpoint::get_id(self->ep_night_)
                                  : 0xFFFF;
    if (endpoint_id != ep_light && endpoint_id != ep_plugin &&
        endpoint_id != ep_night) {
      return ESP_OK;
    }

    bool light_now = false, switch_now = false, night_now = false;
    (void)self->readOnAttr_(self->ep_light_, light_now);
    (void)self->readOnAttr_(self->ep_plugin_, switch_now);
    (void)self->readOnAttr_(self->ep_night_, night_now);
    const bool updated_state = val->val.b;
    if (endpoint_id == ep_light) {
      light_now = updated_state;
    } else if (endpoint_id == ep_plugin) {
      switch_now = updated_state;
    } else {
      night_now = updated_state;
    }

    Event ev{};
    ev.timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    ev.type = (endpoint_id == ep_light)
                  ? (light_now ? EventType::LightOn : EventType::LightOff)
              : (endpoint_id == ep_plugin)
                  ? (switch_now ? EventType::SwitchOn : EventType::SwitchOff)
                  : (night_now ? EventType::NightOn : EventType::NightOff);
    ev.light_state = light_now;
    ev.switch_state = switch_now;
    ev.night_state = night_now;

    ESP_LOGI(TAG, "OnOff update ep=0x%04x state=%s", endpoint_id,
             updated_state ? "ON" : "OFF");
    if (self->queue_) {
      if (xQueueSend(self->queue_, &ev, 0) != pdTRUE)
        ESP_LOGE(TAG, "xQueueSend failed");
    }
    return ESP_OK;
  }

  static MatterLight *&inst_(size_t i) {
    static MatterLight *s[kMaxInstances]{};
    return s[i];
  }
  static bool registerInstance_(MatterLight *self) {
    for (size_t i = 0; i < kMaxInstances; ++i)
      if (!inst_(i)) {
        inst_(i) = self;
        return true;
      }
    return false;
  }
  static MatterLight *findOwnerByEndpoint_(uint16_t ep) {
    for (size_t i = 0; i < kMaxInstances; ++i) {
      MatterLight *p = inst_(i);
      if (!p) continue;
      if (p->ep_light_ && esp_matter::endpoint::get_id(p->ep_light_) == ep)
        return p;
      if (p->ep_plugin_ && esp_matter::endpoint::get_id(p->ep_plugin_) == ep)
        return p;
      if (p->ep_night_ && esp_matter::endpoint::get_id(p->ep_night_) == ep)
        return p;
    }
    return nullptr;
  }
};
