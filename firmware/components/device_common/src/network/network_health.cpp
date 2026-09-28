/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2026 Ryotaro Onuki
 */
#include "device_common/network/network_health.h"

#include <esp_heap_caps.h>
#include <esp_netif_net_stack.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <lwip/dhcp.h>
#include <lwip/netif.h>
#include <lwip/prot/dhcp.h>
#include <lwip/udp.h>
#include <mdns.h>

#include <algorithm>
#include <atomic>

#include "device_common/system/app_log.h"

namespace {
constexpr const char* kStaNetifKey = "WIFI_STA_DEF";
constexpr int64_t kWifiPowerSaveIntervalMs = 1000;
// Last resort if DHCP still cannot get through (see prepareWifi()); a fresh
// association restores the driver's DHCP transmit path.
constexpr int64_t kWifiReconnectAfterMs = 10 * 60 * 1000;
// While the lease is still valid: lwIP retransmits every few seconds once it
// is rebinding (past T2), so minutes without an answer mean the DHCP path is
// stuck. Reassociating then keeps IPv4 up instead of waiting for the lease to
// expire. Renewing alone (T1..T2) is left alone: a single lost renewal is
// normal on a weak link and lwIP retries it by itself.
constexpr int64_t kRebindReconnectAfterMs = 3 * 60 * 1000;
constexpr int64_t kDhcpCheckIntervalMs = 15000;
constexpr int64_t kMdnsSyncIntervalMs = 1000;
constexpr int64_t kDiagLogIntervalMs = 5 * 60 * 1000;

int64_t nowMs() { return esp_timer_get_time() / 1000; }

// Written by the app task, read by diagnostics() from the HTTP task. A device
// has a single STA interface, so these are shared by all instances.
std::atomic<uint32_t> wifi_reconnects{0};
std::atomic<uint32_t> ipv4_lost_count{0};
std::atomic<int64_t> ipv4_missing_since_ms{-1};

const char* dhcpStateName(int state) {
  switch (state) {
    case -1:
      return "none";
    case DHCP_STATE_OFF:
      return "off";
    case DHCP_STATE_REQUESTING:
      return "requesting";
    case DHCP_STATE_INIT:
      return "init";
    case DHCP_STATE_REBOOTING:
      return "rebooting";
    case DHCP_STATE_REBINDING:
      return "rebinding";
    case DHCP_STATE_RENEWING:
      return "renewing";
    case DHCP_STATE_SELECTING:
      return "selecting";
    case DHCP_STATE_CHECKING:
      return "checking";
    case DHCP_STATE_BOUND:
      return "bound";
    case DHCP_STATE_BACKING_OFF:
      return "backing_off";
    default:
      return "unknown";
  }
}

// lwIP state must be accessed on the TCP/IP task (esp_netif_tcpip_exec()).
struct DhcpProbe {
  esp_netif_t* netif = nullptr;
  int state = -1;  // -1: no DHCP client attached
  int tries = 0;
};

esp_err_t probeDhcp(void* ctx) {
  auto* probe = static_cast<DhcpProbe*>(ctx);
  auto* lwip_netif =
      static_cast<struct netif*>(esp_netif_get_netif_impl(probe->netif));
  if (!lwip_netif) return ESP_ERR_INVALID_STATE;
  if (const dhcp* client = netif_dhcp_data(lwip_netif)) {
    probe->state = client->state;
    probe->tries = client->tries;
  }
  return ESP_OK;
}

// Bound UDP PCBs only; lwIP allocates PCBs from the heap
// (MEMP_MEM_MALLOC), so there is no fixed pool to report.
struct UdpPortProbe {
  std::array<uint16_t, 32> ports{};
  size_t port_count = 0;
};

esp_err_t probeUdpPorts(void* ctx) {
  auto* probe = static_cast<UdpPortProbe*>(ctx);
  for (udp_pcb* pcb = udp_pcbs; pcb && probe->port_count < probe->ports.size();
       pcb = pcb->next) {
    probe->ports[probe->port_count++] = pcb->local_port;
  }
  return ESP_OK;
}
}  // namespace

esp_err_t NetworkHealth::prepareWifi() {
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK) return err;
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  if (!esp_netif_get_handle_from_ifkey(kStaNetifKey) &&
      !esp_netif_create_default_wifi_sta()) {
    return ESP_FAIL;
  }
  const wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&config);
  if (err != ESP_OK) return err;
  // The driver sends DHCP/DNS frames through a dedicated path that, on a weak
  // link with 11b rates enabled, stops transmitting some time after
  // association: every lease renewal then fails while other traffic keeps
  // flowing. Measured on ESP32-C6 (IDF 5.5.5): disabling 11b rates avoids it.
  err = esp_wifi_config_11b_rate(WIFI_IF_STA, true);
  if (err != ESP_OK) {
    LOGW("[Net] Failed to disable 11b rates: %s", esp_err_to_name(err));
  }
  return ESP_OK;
}

void NetworkHealth::begin(const std::string& hostname) {
  hostname_ = hostname;
  syncMdnsHostname(true);
}

void NetworkHealth::handle() {
  syncWifiPowerSave();
  ensureIpv4Address();
  syncMdnsHostname(false);
  logDiagnostics();
}

void NetworkHealth::setHostname(const std::string& hostname) {
  hostname_ = hostname;
  syncMdnsHostname(true);
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

// lwIP keeps retrying DHCP on its own; reassociate with the AP only when the
// lease is about to run out unanswered or has been missing for a long time.
// Never reboot: without a lease the device stays reachable over IPv6 and
// Matter.
void NetworkHealth::ensureIpv4Address() {
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey(kStaNetifKey);
  esp_netif_ip_info_t ip_info{};
  const bool had_ipv4 = has_ipv4_;
  has_ipv4_ = netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
              ip_info.ip.addr != 0;
  const int64_t now = nowMs();
  if (has_ipv4_) {
    ipv4_missing_since_ms = -1;
    ipv4_missing_link_up_since_ms_ = -1;
    checkLeaseRebinding(netif, now);
    return;
  }
  if (had_ipv4) {
    ++ipv4_lost_count;
    LOGW("[Net] IPv4 address lost");
  }
  if (ipv4_missing_since_ms < 0) ipv4_missing_since_ms = now;
  if (!netif || !esp_netif_is_netif_up(netif)) {
    ipv4_missing_link_up_since_ms_ = -1;
    return;
  }
  if (ipv4_missing_link_up_since_ms_ < 0) ipv4_missing_link_up_since_ms_ = now;

  if (now - ipv4_missing_link_up_since_ms_ >= kWifiReconnectAfterMs) {
    ipv4_missing_link_up_since_ms_ = -1;
    reconnectWifi("no IPv4 address for 10 min");
  }
}

void NetworkHealth::checkLeaseRebinding(esp_netif_t* netif, int64_t now) {
  if (now - last_dhcp_check_ms_ < kDhcpCheckIntervalMs) return;
  last_dhcp_check_ms_ = now;

  DhcpProbe probe;
  probe.netif = netif;
  if (esp_netif_tcpip_exec(probeDhcp, &probe) != ESP_OK) return;
  if (probe.state != DHCP_STATE_REBINDING) {
    rebinding_since_ms_ = -1;
    return;
  }
  if (rebinding_since_ms_ < 0) rebinding_since_ms_ = now;
  if (now - rebinding_since_ms_ < kRebindReconnectAfterMs) return;
  rebinding_since_ms_ = -1;
  reconnectWifi("DHCP rebinding unanswered for 3 min");
}

// Matter's connectivity manager reconnects right after the disconnect event,
// so a disconnect is enough and keeps its station state consistent.
void NetworkHealth::reconnectWifi(const char* reason) {
  const esp_err_t err = esp_wifi_disconnect();
  ++wifi_reconnects;
  LOGW("[Net] Reconnecting Wi-Fi (%s): %s", reason, esp_err_to_name(err));
}

NetworkDiagnostics NetworkHealth::diagnostics() {
  NetworkDiagnostics diag;
  diag.wifi_reconnects = wifi_reconnects;
  diag.ipv4_lost_count = ipv4_lost_count;
  const int64_t missing_since = ipv4_missing_since_ms;
  if (missing_since >= 0) {
    diag.ipv4_missing_seconds = (nowMs() - missing_since) / 1000;
  }

  if (esp_netif_t* netif = esp_netif_get_handle_from_ifkey(kStaNetifKey)) {
    DhcpProbe probe;
    probe.netif = netif;
    if (esp_netif_tcpip_exec(probeDhcp, &probe) == ESP_OK) {
      diag.dhcp_state = dhcpStateName(probe.state);
      diag.dhcp_tries = probe.tries;
    }
  }

  UdpPortProbe udp;
  if (esp_netif_tcpip_exec(probeUdpPorts, &udp) == ESP_OK) {
    diag.udp_ports.assign(udp.ports.begin(), udp.ports.begin() + udp.port_count);
  }
  return diag;
}

void NetworkHealth::syncMdnsHostname(bool force) {
  const int64_t now = nowMs();
  if (!force && now - last_mdns_sync_attempt_ms_ < kMdnsSyncIntervalMs) return;
  last_mdns_sync_attempt_ms_ = now;

  esp_netif_t* netif = esp_netif_get_handle_from_ifkey(kStaNetifKey);
  if (!netif) return;
  // Without an IPv4 lease, advertise IPv6 only instead of keeping a stale
  // A record that makes clients try an unreachable address.
  esp_netif_ip_info_t ip_info{};
  if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) ip_info.ip.addr = 0;

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
  size_t address_count = 0;
  if (ip_info.ip.addr != 0) {
    addresses[address_count].addr.type = ESP_IPADDR_TYPE_V4;
    addresses[address_count++].addr.u_addr.ip4 = ip_info.ip;
  }
  for (const auto& ipv6 : ipv6_addresses) {
    addresses[address_count].addr.type = ESP_IPADDR_TYPE_V6;
    std::copy(ipv6.begin(), ipv6.end(),
              addresses[address_count++].addr.u_addr.ip6.addr);
  }
  if (address_count == 0) return;
  for (size_t i = 0; i + 1 < address_count; ++i) {
    addresses[i].next = &addresses[i + 1];
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
