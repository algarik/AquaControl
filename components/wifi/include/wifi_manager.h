// AquaControl — Wi-Fi manager (Phase 3.5 Block D12).
//
// Provides a thin wrapper over esp_wifi. Two operating modes:
//
//   1. Station   — connects to a saved SSID (set via Phase 4 wizard).
//   2. AP fallback — used when no SSID is configured or after repeated STA
//      failures. Generates a random 8-char password on first activation
//      (persisted to NVS under "ap_pw") and broadcasts SSID "AquaControl-XXXX"
//      where XXXX is the last four MAC nibbles.
//
// Connection state is observable via `is_connected()`. UI polls this from
// the dashboard header (Wi-Fi icon).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"

namespace aqua::wifi {

struct StationCfg {
    std::string ssid;
    std::string password;   // empty for open networks
};

// One-time bring-up of the netif + wifi driver. Safe to call repeatedly.
esp_err_t init();

// Start STA mode. Returns ESP_OK once esp_wifi_start succeeds; connection
// happens asynchronously. Call `is_connected()` to query.
esp_err_t start_station(const StationCfg& cfg);

// Start the AP fallback. Generates the password if not present in NVS.
esp_err_t start_ap_fallback();

// Persist/load station credentials to/from NVS.
// save_station_cfg stores ssid + password; password is stored as-is (the
// ESP32 NVS partition uses flash encryption if enabled).
esp_err_t save_station_cfg(const StationCfg& cfg);
esp_err_t load_station_cfg(StationCfg* out);  // ESP_ERR_NVS_NOT_FOUND if absent

// Stop both modes.
void stop();

bool         is_connected();
std::string  ip_string();        // "0.0.0.0" if not connected
std::string  ap_ssid();          // empty if AP not running
std::string  ap_password();      // empty if AP not running

// ---------------------------------------------------------------------------
// Network scan
// ---------------------------------------------------------------------------

struct ScanResult {
    std::string ssid;
    int8_t      rssi;   // dBm — higher is better
    bool        open;   // true if no authentication required
};

// Blocking scan — MUST be called from a background task, NOT from the LVGL
// thread.  Initialises WiFi if needed, runs the scan (≤ timeout_ms), then
// restores the previous WiFi mode.  Returns up to max_count networks sorted
// by descending RSSI.
std::vector<ScanResult> scan_networks_blocking(uint8_t  max_count  = 20,
                                               uint32_t timeout_ms = 8000);

// Register a callback invoked (from esp_timer task context, NOT LVGL thread)
// when STA mode permanently gives up after kMaxStaRetries failed attempts.
// The manager will have already switched to AP fallback before calling this.
// Use lv_async_call() or a FreeRTOS queue for any UI updates from here.
using sta_failure_fn = void (*)(void* arg);
void set_sta_failure_callback(sta_failure_fn fn, void* arg);

// Register a callback invoked (from the ESP event loop task) each time the
// station successfully receives an IP address. Useful for starting NTP,
// logging, etc. Use lv_async_call() for any LVGL updates from here.
using got_ip_fn = void (*)(void* arg);
void set_got_ip_callback(got_ip_fn fn, void* arg);

// Returns the current RSSI in dBm (0 if not connected or on failure).
int8_t sta_rssi();

// Returns true if the last STA start attempt exhausted all retries.
// Automatically cleared when start_station() is called again.
bool is_sta_exhausted();

}  // namespace aqua::wifi
