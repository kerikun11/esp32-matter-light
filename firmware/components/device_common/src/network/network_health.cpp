/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2026 Ryotaro Onuki
 */
#include "device_common/network/network_health.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <mdns.h>

#include <algorithm>

#include "device_common/system/app_log.h"

namespace {
constexpr const char* kStaNetifKey = "WIFI_STA_DEF";
constexpr int64_t kWifiPowerSaveIntervalMs = 1000;
constexpr int64_t kIpv4WatchdogIntervalMs = 15000;
constexpr int64_t kMdnsSyncIntervalMs = 1000;
constexpr int64_t kDiagLogIntervalMs = 5 * 60 * 1000;

int64_t nowMs() { return esp_timer_get_time() / 1000; }
}  // namespace

void NetworkHealth::begin(const std::string& hostname) {
  hostname_ = hostname;

  const esp_err_t err = esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_LOST_IP, &NetworkHealth::ipEventHandler, this);
  if (err != ESP_OK) {
    LOGW("[Net] Failed to register IPv4 recovery: %s", esp_err_to_name(err));
  }

  syncMdnsHostname(true);
}

void NetworkHealth::handle() {
  if (dhcp_restart_requested_.exchange(false)) {
    if (esp_netif_t* netif = esp_netif_get_handle_from_ifkey(kStaNetifKey)) {
      restartDhcpClient(netif);
    }
  }

  syncWifiPowerSave();
  ensureIpv4Address();
  syncMdnsHostname(false);
  logDiagnostics();
}

void NetworkHealth::setHostname(const std::string& hostname) {
  hostname_ = hostname;
  syncMdnsHostname(true);
}

void NetworkHealth::ipEventHandler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void*) {
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
    // Runs on the default event loop task: only flag the request here and
    // let the app task restart DHCP.
    static_cast<NetworkHealth*>(arg)->dhcp_restart_requested_ = true;
  }
}

void NetworkHealth::restartDhcpClient(esp_netif_t* netif) {
  const esp_err_t stop_err = esp_netif_dhcpc_stop(netif);
  if (stop_err != ESP_OK &&
      stop_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
    LOGW("[Net] Failed to stop DHCP client: %s", esp_err_to_name(stop_err));
    return;
  }

  const esp_err_t start_err = esp_netif_dhcpc_start(netif);
  if (start_err == ESP_OK ||
      start_err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
    LOGI("[Net] DHCP client restarted");
  } else {
    LOGW("[Net] Failed to restart DHCP client: %s",
         esp_err_to_name(start_err));
  }
}

// Matter starts Wi-Fi asynchronously (after begin()) and may change the power
// save mode later, so keep enforcing WIFI_PS_NONE: modem sleep makes the web
// UI and OTA uploads stall for seconds at a time.
void NetworkHealth::syncWifiPowerSave() {
  const int64_t now = nowMs();
  if (now - last_wifi_ps_attempt_ms_ < kWifiPowerSaveIntervalMs) return;
  last_wifi_ps_attempt_ms_ = now;

  // Only write when needed; setting the same value also emits a driver log.
  wifi_ps_type_t power_save;
  if (esp_wifi_get_ps(&power_save) == ESP_OK && power_save != WIFI_PS_NONE) {
    if (esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK) {
      LOGI("[Net] Wi-Fi power save disabled");
    }
  }
}

// Fallback for the case where a single DHCP restart (triggered by
// IP_EVENT_STA_LOST_IP) fails to obtain a new lease and no further lost-IP
// event ever fires again: periodically check whether the STA interface is up
// but still has no IPv4 address, and keep retrying DHCP until it succeeds.
// Without this, a one-shot failed renewal can leave the device reachable
// over Matter (IPv6 link-local) forever while the web server over IPv4 stays
// unreachable until a manual power cycle.
void NetworkHealth::ensureIpv4Address() {
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey(kStaNetifKey);
  esp_netif_ip_info_t ip_info{};
  has_ipv4_ = netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
              ip_info.ip.addr != 0;
  if (has_ipv4_ || !netif || !esp_netif_is_netif_up(netif)) return;

  const int64_t now = nowMs();
  if (now - last_ipv4_watchdog_attempt_ms_ < kIpv4WatchdogIntervalMs) return;
  last_ipv4_watchdog_attempt_ms_ = now;
  LOGW("[Net] Wi-Fi is up but IPv4 address is missing; retrying DHCP");
  restartDhcpClient(netif);
}

void NetworkHealth::syncMdnsHostname(bool force) {
  const int64_t now = nowMs();
  if (!force && now - last_mdns_sync_attempt_ms_ < kMdnsSyncIntervalMs) return;
  last_mdns_sync_attempt_ms_ = now;

  esp_netif_t* netif = esp_netif_get_handle_from_ifkey(kStaNetifKey);
  esp_netif_ip_info_t ip_info{};
  if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK ||
      ip_info.ip.addr == 0) {
    return;
  }

  if (!mdns_hostname_.empty() && mdns_hostname_ != hostname_) {
    const esp_err_t err = mdns_delegate_hostname_remove(mdns_hostname_.c_str());
    if (err != ESP_OK) {
      LOGW("[mDNS] Failed to remove %s.local: %s", mdns_hostname_.c_str(),
           esp_err_to_name(err));
      return;
    }
    LOGI("[mDNS] Removed additional hostname: %s.local",
         mdns_hostname_.c_str());
    mdns_hostname_.clear();
    mdns_ipv4_address_ = 0;
    mdns_ipv6_addresses_.clear();
  }

  // Publish only addresses that have completed duplicate-address detection.
  // IPv6 may become ready after IPv4, or change later after a router update.
  esp_ip6_addr_t ip6[CONFIG_LWIP_IPV6_NUM_ADDRESSES]{};
  const int ip6_count = esp_netif_get_all_preferred_ip6(netif, ip6);
  std::vector<std::array<uint32_t, 4>> ipv6_addresses;
  for (int i = 0; i < ip6_count; ++i) {
    ipv6_addresses.push_back(
        {ip6[i].addr[0], ip6[i].addr[1], ip6[i].addr[2], ip6[i].addr[3]});
  }
  // Compare address sets independently of their order in the interface.
  std::sort(ipv6_addresses.begin(), ipv6_addresses.end());

  mdns_ip_addr_t addresses[1 + CONFIG_LWIP_IPV6_NUM_ADDRESSES]{};
  addresses[0].addr.type = ESP_IPADDR_TYPE_V4;
  addresses[0].addr.u_addr.ip4 = ip_info.ip;
  for (size_t i = 0; i < ipv6_addresses.size(); ++i) {
    addresses[i].next = &addresses[i + 1];
    addresses[i + 1].addr.type = ESP_IPADDR_TYPE_V6;
    std::copy(ipv6_addresses[i].begin(), ipv6_addresses[i].end(),
              addresses[i + 1].addr.u_addr.ip6.addr);
  }

  if (mdns_hostname_.empty()) {
    const esp_err_t err =
        mdns_delegate_hostname_add(hostname_.c_str(), addresses);
    if (err != ESP_OK) {
      if (err != last_mdns_error_) {
        LOGW("[mDNS] Failed to add %s.local: %s", hostname_.c_str(),
             esp_err_to_name(err));
      }
      last_mdns_error_ = err;
      return;
    }
    last_mdns_error_ = ESP_OK;
    if (!mdns_hostname_exists(hostname_.c_str())) {
      char primary_hostname[MDNS_NAME_BUF_LEN] = {};
      const esp_err_t get_err = mdns_hostname_get(primary_hostname);
      LOGW("[mDNS] %s.local was not added (primary: %s)", hostname_.c_str(),
           get_err == ESP_OK ? primary_hostname : "unavailable");
      return;
    }
    mdns_hostname_ = hostname_;
    mdns_ipv4_address_ = ip_info.ip.addr;
    mdns_ipv6_addresses_ = ipv6_addresses;
    LOGI("[mDNS] Added additional hostname: %s.local -> " IPSTR
         " (%u IPv6 addresses)",
         mdns_hostname_.c_str(), IP2STR(&ip_info.ip),
         static_cast<unsigned>(ipv6_addresses.size()));
    return;
  }

  if (mdns_ipv4_address_ == ip_info.ip.addr &&
      mdns_ipv6_addresses_ == ipv6_addresses) return;
  const esp_err_t err =
      mdns_delegate_hostname_set_address(mdns_hostname_.c_str(), addresses);
  if (err != ESP_OK) {
    LOGW("[mDNS] Failed to update %s.local: %s", mdns_hostname_.c_str(),
         esp_err_to_name(err));
    return;
  }
  mdns_ipv4_address_ = ip_info.ip.addr;
  mdns_ipv6_addresses_ = ipv6_addresses;
  LOGI("[mDNS] Updated addresses: %s.local -> " IPSTR " (%u IPv6 addresses)",
       mdns_hostname_.c_str(), IP2STR(&ip_info.ip),
       static_cast<unsigned>(ipv6_addresses.size()));
}

void NetworkHealth::logDiagnostics() {
  const int64_t now = nowMs();
  if (last_diag_log_ms_ >= 0 && now - last_diag_log_ms_ < kDiagLogIntervalMs) {
    return;
  }
  last_diag_log_ms_ = now;

  esp_netif_t* netif = esp_netif_get_handle_from_ifkey(kStaNetifKey);
  esp_netif_ip_info_t ip_info{};
  char ipv4[16] = "NONE";
  if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
      ip_info.ip.addr != 0) {
    snprintf(ipv4, sizeof(ipv4), IPSTR, IP2STR(&ip_info.ip));
  }
  LOGI("[Diag] uptime=%llds heap=%u(min=%u, largest=%u) ipv4=%s", now / 1000,
       static_cast<unsigned>(esp_get_free_heap_size()),
       static_cast<unsigned>(esp_get_minimum_free_heap_size()),
       static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
       ipv4);
}
