/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#pragma once

#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteCommandPath.h>
#include <app/server/Server.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_attribute.h>
#include <esp_matter_cluster.h>
#include <esp_matter_command.h>
#include <esp_matter_core.h>
#include <esp_matter_endpoint.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <inttypes.h>
#include <lib/core/TLV.h>
#include <platform/ConfigurationManager.h>

class MatterSwitch {
 public:
  enum class EventType : uint8_t {
    SwitchOn,
    SwitchOff,
  };

  struct Event {
    uint64_t timestamp_ms;
    EventType type;
    bool switch_state;
  };

  static constexpr const char *kManualCode = "34970112332";
  static constexpr const char *kQrUrl =
      "https://project-chip.github.io/connectedhomeip/"
      "qrcode.html?data=MT:Y.K9042C00KA0648G00";

  bool begin(bool initial_switch_on = false) {
    esp_matter::node::config_t node_cfg{};
    node_ = esp_matter::node::create(&node_cfg, nullptr, nullptr, this);
    if (!node_) {
      ESP_LOGE(TAG, "node::create failed");
      return false;
    }

    // Switch endpoint (plugin unit)
    {
      esp_matter::endpoint::on_off_plugin_unit::config_t cfg{};
      cfg.on_off.on_off = initial_switch_on;
      ep_plugin_ = esp_matter::endpoint::on_off_plugin_unit::create(node_, &cfg,
                                                                    0, this);
      if (!ep_plugin_ || !registerOnOffCbs_(ep_plugin_) ||
          !setOnOffAttr_(ep_plugin_, initial_switch_on)) {
        ESP_LOGE(TAG, "plugin::create failed");
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

    ESP_LOGI(TAG, "plugin_ep=0x%04x(%s)",
             esp_matter::endpoint::get_id(ep_plugin_),
             initial_switch_on ? "ON" : "OFF");
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

  bool setSwitchState(bool on) { return setOnOffAttr_(ep_plugin_, on); }

  void decommission() {
    ESP_LOGW(TAG, "Decommissioning device...");
    chip::Server::GetInstance().GetFabricTable().DeleteAllFabrics();
    chip::DeviceLayer::ConfigurationMgr().InitiateFactoryReset();
  }

 private:
  static constexpr const char *TAG = "MatterSwitch";
  static constexpr size_t kQueueSize = 8;
  static constexpr size_t kMaxInstances = 8;

  esp_matter::node_t *node_ = nullptr;
  esp_matter::endpoint_t *ep_plugin_ = nullptr;
  QueueHandle_t queue_ = nullptr;

  bool registerOnOffCbs_(esp_matter::endpoint_t *ep) {
    auto *onoff = esp_matter::cluster::get(ep, chip::app::Clusters::OnOff::Id);
    if (!onoff) return false;
    auto *c_on = esp_matter::cluster::on_off::command::create_on(onoff);
    auto *c_off = esp_matter::cluster::on_off::command::create_off(onoff);
    auto *c_toggle = esp_matter::cluster::on_off::command::create_toggle(onoff);
    if (!c_on || !c_off || !c_toggle) return false;
    esp_matter::command::set_user_callback(c_on, &MatterSwitch::cmdCb_);
    esp_matter::command::set_user_callback(c_off, &MatterSwitch::cmdCb_);
    esp_matter::command::set_user_callback(c_toggle, &MatterSwitch::cmdCb_);
    return true;
  }

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

  static esp_err_t cmdCb_(const chip::app::ConcreteCommandPath &path,
                          chip::TLV::TLVReader &, void *) {
    MatterSwitch *self = findOwnerByEndpoint_(path.mEndpointId);
    if (!self || path.mClusterId != chip::app::Clusters::OnOff::Id)
      return ESP_OK;

    const uint16_t ep_plugin = esp_matter::endpoint::get_id(self->ep_plugin_);

    bool switch_now = false;
    (void)self->readOnAttr_(self->ep_plugin_, switch_now);

    if (path.mEndpointId == ep_plugin) {
      if (path.mCommandId == chip::app::Clusters::OnOff::Commands::On::Id)
        switch_now = true;
      else if (path.mCommandId == chip::app::Clusters::OnOff::Commands::Off::Id)
        switch_now = false;
      else if (path.mCommandId ==
               chip::app::Clusters::OnOff::Commands::Toggle::Id)
        switch_now = !switch_now;
    } else {
      return ESP_OK;
    }

    Event ev{};
    ev.timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    ev.type = switch_now ? EventType::SwitchOn : EventType::SwitchOff;
    ev.switch_state = switch_now;

    if (self->queue_) {
      if (xQueueSend(self->queue_, &ev, 0) != pdTRUE)
        ESP_LOGE(TAG, "xQueueSend failed");
    }
    return ESP_OK;
  }

  static MatterSwitch *&inst_(size_t i) {
    static MatterSwitch *s[kMaxInstances]{};
    return s[i];
  }
  static bool registerInstance_(MatterSwitch *self) {
    for (size_t i = 0; i < kMaxInstances; ++i)
      if (!inst_(i)) {
        inst_(i) = self;
        return true;
      }
    return false;
  }
  static MatterSwitch *findOwnerByEndpoint_(uint16_t ep) {
    for (size_t i = 0; i < kMaxInstances; ++i) {
      MatterSwitch *p = inst_(i);
      if (!p) continue;
      if (p->ep_plugin_ && esp_matter::endpoint::get_id(p->ep_plugin_) == ep)
        return p;
    }
    return nullptr;
  }
};
