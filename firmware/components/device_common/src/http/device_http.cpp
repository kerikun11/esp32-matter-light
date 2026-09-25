#include "device_common/http/device_http.h"

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <platform/CHIPDeviceLayer.h>

#include <cinttypes>
#include <cstdlib>

#include "device_common/http/web_asset_http.h"
#include "device_common/http/web_utils.h"
namespace device_common {
namespace {
std::string requestHeader(httpd_req_t* req, const char* name) {
  const size_t length = httpd_req_get_hdr_value_len(req, name);
  if (length == 0) return {};
  std::string value(length + 1, '\0');
  if (httpd_req_get_hdr_value_str(req, name, value.data(), value.size()) != ESP_OK) return {};
  value.resize(length);
  return value;
}

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "電源投入";
    case ESP_RST_SW:
      return "ソフトウェア再起動";
    case ESP_RST_PANIC:
      return "例外（パニック）";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "ウォッチドッグ";
    case ESP_RST_BROWNOUT:
      return "電圧低下（ブラウンアウト）";
    case ESP_RST_DEEPSLEEP:
      return "ディープスリープ復帰";
    case ESP_RST_EXT:
      return "外部リセット";
    case ESP_RST_USB:
      return "USB";
    default:
      return "不明";
  }
}

bool addString(cJSON* object, const char* key, const char* value) {
  return cJSON_AddStringToObject(object, key, value) != nullptr;
}

bool addNumber(cJSON* object, const char* key, double value) {
  return cJSON_AddNumberToObject(object, key, value) != nullptr;
}

bool addBool(cJSON* object, const char* key, bool value) {
  return cJSON_AddBoolToObject(object, key, value) != nullptr;
}

esp_err_t sendJson(httpd_req_t* req, cJSON* root, bool ok) {
  char* json = ok ? cJSON_PrintUnformatted(root) : nullptr;
  cJSON_Delete(root);
  if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t result = httpd_resp_sendstr(req, json);
  cJSON_free(json);
  return result;
}
}  // namespace
httpd_config_t httpServerConfig() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // Fabric cleanup requires more stack than the HTTP server default.
  config.stack_size = 8192;
  config.max_uri_handlers = 12;
  config.lru_purge_enable = true;
  config.send_wait_timeout = 15;
  return config;
}
esp_err_t sendDeviceInfo(httpd_req_t* req, const char* manual_code, const char* qr_payload) {
  cJSON* info = cJSON_CreateObject();
  if (!info) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  bool ok = true;
  const auto* app = esp_app_get_description();
  char build_date[40];
  snprintf(build_date, sizeof(build_date), "%s %s", app->date, app->time);
  ok &= addString(info, "project_name", app->project_name);
  ok &= addString(info, "version", app->version);
  ok &= addString(info, "build_date", build_date);
  ok &= addString(info, "idf_version", app->idf_ver);
  ok &= addNumber(info, "uptime_seconds", static_cast<double>(esp_timer_get_time() / 1000000));
  ok &= addString(info, "reset_reason", resetReasonName(esp_reset_reason()));
  ok &= addNumber(info, "free_heap", esp_get_free_heap_size());
  ok &= addNumber(info, "min_free_heap", esp_get_minimum_free_heap_size());
  ok &= addNumber(info, "largest_free_block", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  wifi_ap_record_t ap{};
  const bool connected = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
  const std::string ssid(reinterpret_cast<const char*>(ap.ssid),
                         strnlen(reinterpret_cast<const char*>(ap.ssid), sizeof(ap.ssid)));
  ok &= addBool(info, "connected", connected);
  ok &= addString(info, "ssid", ssid.c_str());
  ok &= addNumber(info, "rssi", ap.rssi);
  ok &= addNumber(info, "channel", ap.primary);
  auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  uint8_t mac[6] = {};
  char address[48] = {};
  if (netif && esp_netif_get_mac(netif, mac) == ESP_OK) {
    snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  ok &= addString(info, "mac", address);
  address[0] = '\0';
  esp_netif_ip_info_t ip{};
  if (connected && netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr) {
    snprintf(address, sizeof(address), IPSTR, IP2STR(&ip.ip));
  }
  ok &= addString(info, "ipv4", address);
  auto* ipv6 = cJSON_AddArrayToObject(info, "ipv6");
  ok &= ipv6 != nullptr;
  if (connected && netif && ipv6) {
    esp_ip6_addr_t addresses[CONFIG_LWIP_IPV6_NUM_ADDRESSES]{};
    const int count = esp_netif_get_all_preferred_ip6(netif, addresses);
    for (int i = 0; i < count; ++i) {
      snprintf(address, sizeof(address), IPV6STR, IPV62STR(addresses[i]));
      auto* item = cJSON_CreateString(address);
      if (!item || !cJSON_AddItemToArray(ipv6, item)) {
        cJSON_Delete(item);
        ok = false;
      }
    }
  }
  ok &= addString(info, "manual_code", manual_code);
  ok &= addString(info, "qr_payload", qr_payload);
  auto* fabrics = cJSON_AddArrayToObject(info, "fabrics");
  ok &= fabrics != nullptr;
  if (fabrics) {
    chip::DeviceLayer::StackLock lock;
    ok &= addBool(info, "commissioning_open",
                  chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen());
    for (const auto& fabric : chip::Server::GetInstance().GetFabricTable()) {
      auto* item = cJSON_CreateObject();
      if (!item) {
        ok = false;
        break;
      }
      const auto label = fabric.GetFabricLabel();
      const std::string label_text(label.data(), label.size());
      char node_id[19], fabric_id[19], vendor_id[7];
      snprintf(node_id, sizeof(node_id), "0x%016" PRIX64, fabric.GetNodeId());
      snprintf(fabric_id, sizeof(fabric_id), "0x%016" PRIX64, fabric.GetFabricId());
      snprintf(vendor_id, sizeof(vendor_id), "0x%04X", static_cast<unsigned>(fabric.GetVendorId()));
      ok &= addNumber(item, "index", fabric.GetFabricIndex());
      ok &= addString(item, "label", label_text.c_str());
      // Keep 64-bit identifiers as strings to avoid JavaScript precision loss.
      ok &= addString(item, "node_id", node_id);
      ok &= addString(item, "fabric_id", fabric_id);
      ok &= addString(item, "vendor_id", vendor_id);
      if (!cJSON_AddItemToArray(fabrics, item)) {
        cJSON_Delete(item);
        ok = false;
      }
    }
  }
  return sendJson(req, info, ok);
}

MatterActionResult handleMatterAction(httpd_req_t* req) {
  logRequest(req);
  const auto fields = parseFormBody(req);
  const auto action = formValue(fields, "action");
  std::string message;
  bool failed = false;
  // HTTP callbacks run outside the Matter task. Do not hold the settings
  // mutex while taking the stack lock or invoking fabric callbacks.
  {
    chip::DeviceLayer::StackLock lock;
    auto& server = chip::Server::GetInstance();
    auto& table = server.GetFabricTable();
    auto& window = server.GetCommissioningWindowManager();
    if (action == "commission") {
      if (window.IsCommissioningWindowOpen()) {
        message = "ペアリング受付はすでに開始されています。";
      } else {
        const auto err = window.OpenBasicCommissioningWindow(
            chip::System::Clock::Seconds32(300));
        failed = err != CHIP_NO_ERROR;
        message = failed ? "ペアリング受付を開始できませんでした。"
                         : "ペアリング受付を開始しました（最大5分間）。";
        if (failed) LOGE("[Web] Commissioning failed: %" CHIP_ERROR_FORMAT, err.Format());
      }
    } else if (action == "remove") {
      const auto index_text = formValue(fields, "index");
      const int index = index_text.size() <= 3 ? atoi(index_text.c_str()) : 0;
      const auto* fabric = index >= 1 && index <= 254 &&
                                   index_text == std::to_string(index)
                               ? table.FindFabricWithIndex(static_cast<chip::FabricIndex>(index))
                               : nullptr;
      char fabric_id[19] = {}, node_id[19] = {}, vendor_id[7] = {};
      if (fabric) {
        snprintf(fabric_id, sizeof(fabric_id), "0x%016" PRIX64, fabric->GetFabricId());
        snprintf(node_id, sizeof(node_id), "0x%016" PRIX64, fabric->GetNodeId());
        snprintf(vendor_id, sizeof(vendor_id), "0x%04X", static_cast<unsigned>(fabric->GetVendorId()));
      }
      // Reject stale pages if an index has since been reused by another fabric.
      if (!fabric || formValue(fields, "fabric_id") != fabric_id ||
          formValue(fields, "node_id") != node_id ||
          formValue(fields, "vendor_id") != vendor_id) {
        failed = true;
        message = "削除対象が見つからないか、登録情報が変わっています。一覧を確認してください。";
      } else {
        const auto err = table.Delete(static_cast<chip::FabricIndex>(index));
        failed = err != CHIP_NO_ERROR;
        message = failed ? "Fabricを削除できませんでした。"
                         : "Fabric #" + index_text + "を削除しました。Wi-Fi接続は維持されます。";
        if (!failed && table.FabricCount() == 0 && !window.IsCommissioningWindowOpen()) {
          message += "再登録するにはペアリング受付を開始してください。";
        }
        if (failed) LOGE("[Web] Fabric removal failed: %" CHIP_ERROR_FORMAT, err.Format());
      }
    } else {
      failed = true;
      message = "Matterの操作内容が不正です。";
    }
  }
  return {message, failed};
}

esp_err_t sendWebPage(httpd_req_t* req, const WebAssets& assets) {
  // Compression is done at build time: send flash-resident bytes directly.
  const std::string accept = requestHeader(req, "Accept-Encoding");
  const bool gzip = web_asset::quality(accept, "gzip") > 0;
  if (!gzip && web_asset::quality(accept, "identity") == 0) {
    httpd_resp_set_status(req, "406 Not Acceptable");
    return httpd_resp_sendstr(req, "No supported content encoding");
  }
  const char* etag = gzip ? assets.gzip_etag : assets.identity_etag;
  const bool unchanged = web_asset::etagMatches(requestHeader(req, "If-None-Match"), etag);
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  // Revalidate the static shell after firmware updates; state is never cached.
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "ETag", etag);
  if (gzip) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  if (unchanged) {
    httpd_resp_set_status(req, "304 Not Modified");
    return httpd_resp_send(req, nullptr, 0);
  }
  return httpd_resp_send(req,
                         reinterpret_cast<const char*>(gzip ? assets.gzip : assets.identity),
                         gzip ? assets.gzip_size : assets.identity_size);
}
}  // namespace device_common
