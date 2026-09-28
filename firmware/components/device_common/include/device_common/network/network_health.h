/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2026 Ryotaro Onuki
 */
#pragma once

#include <esp_netif.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Snapshot of the STA IPv4/DHCP state, readable over IPv6 even while IPv4 is
// down so that DHCP problems can be diagnosed without a serial console.
struct NetworkDiagnostics {
  const char* dhcp_state = "none";  // lwIP DHCP client state
  int dhcp_tries = 0;               // lwIP retransmission counter
  uint32_t wifi_reconnects = 0;     // Wi-Fi reconnects issued by the watchdog
  std::vector<uint16_t> udp_ports;  // local ports of bound UDP PCBs
  uint32_t ipv4_lost_count = 0;
  int64_t ipv4_missing_seconds = -1;  // -1 while an IPv4 address is assigned
};

// Owns everything needed to keep the Wi-Fi STA's IP connectivity and its
// additional mDNS hostname alias healthy, independently of Matter (which
// mostly relies on IPv6 link-local and is not affected by IPv4 issues).
// All methods except diagnostics() run on the app task.
class NetworkHealth {
 public:
  // Call once before esp_matter::start(). Initializes the Wi-Fi STA the same
  // way Matter does (Matter tolerates it being done already) so that driver
  // options that must be set before esp_wifi_start() can be applied.
  static esp_err_t prepareWifi();

  // Publishes the initial mDNS hostname alias (once an address is
  // available).
  void begin(const std::string& hostname);

  // Call once per loop iteration.
  void handle();

  // Call whenever the desired hostname changes (e.g. saved from the web UI)
  // to republish the mDNS alias immediately instead of waiting for the next
  // periodic sync.
  void setHostname(const std::string& hostname);

  // True while the STA interface has an IPv4 address.
  bool hasIpv4() const { return has_ipv4_; }

  // Thread-safe; may be called from the HTTP task.
  static NetworkDiagnostics diagnostics();

 private:
  void syncWifiPowerSave();
  void ensureIpv4Address();
  void checkLeaseRebinding(esp_netif_t* netif, int64_t now);
  static void reconnectWifi(const char* reason);
  void syncMdnsHostname(bool force);
  void logDiagnostics();

  std::string hostname_;

  bool has_ipv4_ = false;
  int64_t last_wifi_ps_attempt_ms_ = 0;
  // Start of the current period with the Wi-Fi link up but no IPv4 address;
  // -1 while IPv4 is present or the link is down.
  int64_t ipv4_missing_link_up_since_ms_ = -1;
  int64_t last_dhcp_check_ms_ = 0;
  int64_t rebinding_since_ms_ = -1;  // -1 unless the DHCP client is rebinding
  int64_t last_diag_log_ms_ = -1;

  std::string mdns_hostname_;
  uint32_t mdns_ipv4_address_ = 0;
  std::vector<std::array<uint32_t, 4>> mdns_ipv6_addresses_;
  int64_t last_mdns_sync_attempt_ms_ = 0;
  esp_err_t last_mdns_error_ = ESP_OK;
};
