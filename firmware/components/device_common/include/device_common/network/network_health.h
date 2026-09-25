/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2026 Ryotaro Onuki
 */
#pragma once

#include <esp_event.h>
#include <esp_netif.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Owns everything needed to keep the Wi-Fi STA's IP connectivity and its
// additional mDNS hostname alias healthy, independently of Matter (which
// mostly relies on IPv6 link-local and is not affected by IPv4 issues).
// All methods except the event handler run on the app task.
class NetworkHealth {
 public:
  // Registers for IPv4-loss recovery and publishes the initial mDNS
  // hostname alias (once an address is available).
  void begin(const std::string& hostname);

  // Call once per loop iteration.
  void handle();

  // Call whenever the desired hostname changes (e.g. saved from the web UI)
  // to republish the mDNS alias immediately instead of waiting for the next
  // periodic sync.
  void setHostname(const std::string& hostname);

  // True while the STA interface has an IPv4 address.
  bool hasIpv4() const { return has_ipv4_; }

 private:
  static void ipEventHandler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data);
  static void restartDhcpClient(esp_netif_t* netif);
  void syncWifiPowerSave();
  void ensureIpv4Address();
  void syncMdnsHostname(bool force);
  void logDiagnostics();

  std::string hostname_;

  std::atomic<bool> dhcp_restart_requested_{false};
  bool has_ipv4_ = false;
  int64_t last_wifi_ps_attempt_ms_ = 0;
  int64_t last_ipv4_watchdog_attempt_ms_ = 0;
  int64_t last_diag_log_ms_ = -1;

  std::string mdns_hostname_;
  uint32_t mdns_ipv4_address_ = 0;
  std::vector<std::array<uint32_t, 4>> mdns_ipv6_addresses_;
  int64_t last_mdns_sync_attempt_ms_ = 0;
  esp_err_t last_mdns_error_ = ESP_OK;
};
