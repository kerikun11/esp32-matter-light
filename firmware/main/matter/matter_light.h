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
#include <esp_matter_feature.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <inttypes.h>
#include <platform/ConfigurationManager.h>
#include <platform/PlatformManager.h>
#include <system/SystemClock.h>

#include <algorithm>
#include <cstring>

#include "device_common/matter/matter_service.h"

class MatterLight {
 public:
  enum class EventType : uint8_t {
    kLightOn,
    kLightOff,
    kSwitchOn,
    kSwitchOff,
    kNightOn,
    kNightOff,
  };

  struct Event {
    uint64_t timestamp_ms;
    EventType type;
    bool light_state;
    bool switch_state;
    bool night_state;
  };

  static constexpr const char* kManualCode = "34970112332";
  static constexpr const char* kQrPayload = "MT:Y.K9042C00KA0648G00";
  static constexpr const char* kQrUrl =
      "https://project-chip.github.io/connectedhomeip/"
      "qrcode.html?data=MT:Y.K9042C00KA0648G00";

  bool begin(bool initial_light_on, bool initial_switch_on,
             bool initial_night_on = false, bool enable_night_endpoint = true) {
    esp_matter::node::config_t node_cfg{};
    node_ = esp_matter::node::create(&node_cfg, &MatterLight::attrCb, nullptr,
                                     this);
    if (!node_) {
      ESP_LOGE(kTag, "node::create failed");
      return false;
    }

    // Light endpoint
    {
      esp_matter::endpoint::on_off_light::config_t cfg{};
      cfg.on_off.on_off = initial_light_on;
      ep_light_ =
          esp_matter::endpoint::on_off_light::create(node_, &cfg, 0, this);
      if (!ep_light_ || !setEndpointLabel(ep_light_, "照明") ||
          !setOnOffAttr(ep_light_, initial_light_on)) {
        ESP_LOGE(kTag, "light::create failed");
        return false;
      }
    }

    // Plugin endpoint (switch)
    {
      esp_matter::endpoint::on_off_plug_in_unit::config_t cfg{};
      cfg.on_off.on_off = initial_switch_on;
      ep_plugin_ = esp_matter::endpoint::on_off_plug_in_unit::create(
          node_, &cfg, 0, this);
      if (!ep_plugin_ || !setEndpointLabel(ep_plugin_, "人感") ||
          !setOnOffAttr(ep_plugin_, initial_switch_on)) {
        ESP_LOGE(kTag, "plugin::create failed");
        return false;
      }
    }

    // Plugin endpoint (night) - only when feature is enabled
    if (enable_night_endpoint) {
      esp_matter::endpoint::on_off_plug_in_unit::config_t cfg{};
      cfg.on_off.on_off = initial_night_on;
      ep_night_ = esp_matter::endpoint::on_off_plug_in_unit::create(node_, &cfg,
                                                                    0, this);
      if (!ep_night_ || !setEndpointLabel(ep_night_, "常夜灯") ||
          !setOnOffAttr(ep_night_, initial_night_on)) {
        ESP_LOGE(kTag, "night::create failed");
        return false;
      }
    }

    if (!registerInstance(this)) {
      ESP_LOGE(kTag, "instance registry full");
      return false;
    }

    queue_ = xQueueCreate(kQueueSize, sizeof(Event));
    if (!queue_) {
      ESP_LOGE(kTag, "xQueueCreate failed");
      return false;
    }

    if (esp_matter::start(nullptr) != ESP_OK) {
      ESP_LOGE(kTag, "esp_matter::start failed");
      return false;
    }

    started_ = true;
    if (ep_night_) {
      ESP_LOGI(kTag,
               "light_ep=0x%04x(%s) plugin_ep=0x%04x(%s) night_ep=0x%04x(%s)",
               esp_matter::endpoint::get_id(ep_light_),
               initial_light_on ? "ON" : "OFF",
               esp_matter::endpoint::get_id(ep_plugin_),
               initial_switch_on ? "ON" : "OFF",
               esp_matter::endpoint::get_id(ep_night_),
               initial_night_on ? "ON" : "OFF");
    } else {
      ESP_LOGI(kTag,
               "light_ep=0x%04x(%s) plugin_ep=0x%04x(%s) night_ep=disabled",
               esp_matter::endpoint::get_id(ep_light_),
               initial_light_on ? "ON" : "OFF",
               esp_matter::endpoint::get_id(ep_plugin_),
               initial_switch_on ? "ON" : "OFF");
    }
    printOnboarding();
    return true;
  }

  bool getEvent(Event& out, TickType_t ticks = portMAX_DELAY) {
    return queue_ && (xQueueReceive(queue_, &out, ticks) == pdTRUE);
  }

  void printOnboarding() const {
    ESP_LOGI(kTag, "Manual: %s", kManualCode);
    ESP_LOGI(kTag, "QR    : %s", kQrUrl);
  }

  device_common::MatterStatus getStatus() const { return device_common::matterStatus(); }
  bool isCommissioned() const { return getStatus().commissioned; }

  bool setLightState(bool on) { return setOnOffAttr(ep_light_, on); }
  bool setSwitchState(bool on) { return setOnOffAttr(ep_plugin_, on); }
  bool setNightState(bool on) { return setOnOffAttr(ep_night_, on); }

  bool openCommissioningWindow(uint16_t timeout_seconds = 300) {
    if (!device_common::openCommissioningWindow(timeout_seconds)) return false;
    printOnboarding();
    return true;
  }

  void decommission() {
    device_common::factoryReset();
  }

  // Lists commissioned fabrics without touching any of them, so a stale
  // entry (e.g. from a controller that was removed/replaced without first
  // decommissioning it here) can be identified before removing just that
  // one with removeFabric().
  void listFabrics() const {
    ChipStackLock lock;
    auto& table = chip::Server::GetInstance().GetFabricTable();
    ESP_LOGI(kTag, "Fabrics (%u):", static_cast<unsigned>(table.FabricCount()));
    for (const auto& fabric : table) {
      const auto label_span = fabric.GetFabricLabel();
      char label[34] = {};
      const size_t len = std::min(label_span.size(), sizeof(label) - 1);
      memcpy(label, label_span.data(), len);
      ESP_LOGI(kTag,
               "  index=%u nodeId=0x%016" PRIX64 " fabricId=0x%016" PRIX64
               " vendorId=0x%04X label='%s'",
               fabric.GetFabricIndex(), fabric.GetNodeId(),
               fabric.GetFabricId(), fabric.GetVendorId(), label);
    }
  }

  // Removes a single fabric by index (as printed by listFabrics()), leaving
  // every other fabric's commissioning intact -- unlike decommission(),
  // which wipes all of them plus does a full factory reset.
  bool removeFabric(uint8_t fabric_index) {
    return device_common::removeFabric(fabric_index);
  }

 private:
  static constexpr const char* kTag = "MatterLight";
  static constexpr size_t kQueueSize = 8;
  static constexpr size_t kMaxInstances = 8;

  // chip::DeviceLayer::StackLock (CHIP's own RAII PlatformMgr lock guard):
  // most CHIP APIs -- including FabricTable mutations, which emit a
  // reporting-engine event -- assert that this is held when called from any
  // task other than the Matter event loop. decommission()/listFabrics()/
  // removeFabric() run on the app's own task (button handler / USB
  // console), not the Matter task, so they must take it explicitly: without
  // this, removeFabric() aborted the device with "Chip stack locking error"
  // at EventManagement.cpp:414.
  using ChipStackLock = chip::DeviceLayer::StackLock;

  esp_matter::node_t* node_ = nullptr;
  esp_matter::endpoint_t* ep_light_ = nullptr;
  esp_matter::endpoint_t* ep_plugin_ = nullptr;
  esp_matter::endpoint_t* ep_night_ = nullptr;
  QueueHandle_t queue_ = nullptr;

  bool started_ = false;

  // The SDK copies the tag but retains its label span. Call with string
  // literals so the UTF-8 text remains valid for the endpoint's lifetime.
  static bool setEndpointLabel(esp_matter::endpoint_t* ep, const char* label) {
    auto* descriptor =
        esp_matter::cluster::get(ep, chip::app::Clusters::Descriptor::Id);
    if (esp_matter::cluster::descriptor::feature::tag_list::add(descriptor) !=
        ESP_OK) {
      return false;
    }
    chip::app::DataModel::Provider::SemanticTag tag{};
    tag.mfgCode.SetNull();
    tag.namespaceID = 0x43;  // Switches namespace
    tag.tag = 0x08;          // Custom (requires a label)
    tag.label.Emplace().SetNonNull(chip::CharSpan::fromCharString(label));
    return esp_matter::endpoint::set_semantic_tags(ep, &tag, 1) == ESP_OK;
  }

  bool setOnOffAttr(esp_matter::endpoint_t* ep, bool on) {
    if (!ep) return false;
    if (started_) return device_common::reportOnOff(esp_matter::endpoint::get_id(ep), on);
    auto* cluster = esp_matter::cluster::get(ep, chip::app::Clusters::OnOff::Id);
    if (!cluster) return false;
    auto* attr = esp_matter::attribute::get(cluster, chip::app::Clusters::OnOff::Attributes::OnOff::Id);
    if (!attr) return false;
    auto value = esp_matter_bool(on);
    const auto err = esp_matter::attribute::set_val(attr, &value, false);
    return err == ESP_OK || err == ESP_ERR_NOT_FINISHED;
  }

  bool readOnAttr(esp_matter::endpoint_t* ep, bool& out) const {
    out = false;
    if (!ep) return false;
    auto* cluster =
        esp_matter::cluster::get(ep, chip::app::Clusters::OnOff::Id);
    if (!cluster) return false;
    auto* attr = esp_matter::attribute::get(
        cluster, chip::app::Clusters::OnOff::Attributes::OnOff::Id);
    if (!attr) return false;
    esp_matter_attr_val_t v{};
    if (esp_matter::attribute::get_val(attr, &v) != ESP_OK) return false;
    out = v.val.b;
    return true;
  }

  static esp_err_t attrCb(esp_matter::attribute::callback_type_t type,
                          uint16_t endpoint_id, uint32_t cluster_id,
                          uint32_t attribute_id, esp_matter_attr_val_t* val,
                          void*) {
    if (type != esp_matter::attribute::POST_UPDATE ||
        cluster_id != chip::app::Clusters::OnOff::Id ||
        attribute_id != chip::app::Clusters::OnOff::Attributes::OnOff::Id ||
        !val) {
      return ESP_OK;
    }

    MatterLight* self = findOwnerByEndpoint(endpoint_id);
    if (!self) return ESP_OK;

    const uint16_t ep_light = esp_matter::endpoint::get_id(self->ep_light_);
    const uint16_t ep_plugin = esp_matter::endpoint::get_id(self->ep_plugin_);
    const uint16_t ep_night =
        self->ep_night_ ? esp_matter::endpoint::get_id(self->ep_night_)
                        : 0xFFFF;
    if (endpoint_id != ep_light && endpoint_id != ep_plugin &&
        endpoint_id != ep_night) {
      return ESP_OK;
    }

    bool light_now = false, switch_now = false, night_now = false;
    (void)self->readOnAttr(self->ep_light_, light_now);
    (void)self->readOnAttr(self->ep_plugin_, switch_now);
    (void)self->readOnAttr(self->ep_night_, night_now);
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
                  ? (light_now ? EventType::kLightOn : EventType::kLightOff)
              : (endpoint_id == ep_plugin)
                  ? (switch_now ? EventType::kSwitchOn : EventType::kSwitchOff)
                  : (night_now ? EventType::kNightOn : EventType::kNightOff);
    ev.light_state = light_now;
    ev.switch_state = switch_now;
    ev.night_state = night_now;

    ESP_LOGI(kTag, "OnOff update ep=0x%04x state=%s", endpoint_id,
             updated_state ? "ON" : "OFF");
    if (self->queue_) {
      if (xQueueSend(self->queue_, &ev, 0) != pdTRUE)
        ESP_LOGE(kTag, "xQueueSend failed");
    }
    return ESP_OK;
  }

  static MatterLight*& inst(size_t i) {
    static MatterLight* s[kMaxInstances]{};
    return s[i];
  }
  static bool registerInstance(MatterLight* self) {
    for (size_t i = 0; i < kMaxInstances; ++i)
      if (!inst(i)) {
        inst(i) = self;
        return true;
      }
    return false;
  }
  static MatterLight* findOwnerByEndpoint(uint16_t ep) {
    for (size_t i = 0; i < kMaxInstances; ++i) {
      MatterLight* p = inst(i);
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
