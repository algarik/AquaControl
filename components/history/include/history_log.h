// AquaControl — Persistent event history (Phase 3.5 Block C11).
//
// Append-only circular log on SPIFFS. Used by Screen 3f and the Recent
// Activity card on the dashboard. Events are short single-line JSON
// records; the current file rotates to `.bak` once it exceeds
// `kFileMaxBytes`, keeping roughly the last 2× that many events available.
//
// Append API is thread-safe (FreeRTOS mutex). Recent-read API copies into
// the caller's vector and is safe to call from any task.
#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "esp_err.h"

namespace aqua::history {

enum class EventType : uint8_t {
    TRIG_EDGE     = 0,   // trigger state change
    DEV_OVERRIDE  = 1,   // manual override applied/cleared
    FAULT_RAISE   = 2,
    FAULT_CLEAR   = 3,
    BOOT          = 4,
    NTP_SYNC      = 5,
    WIFI_CONNECT  = 6,
    WIFI_LOST     = 7,
    SENSOR        = 8,
    OTHER         = 9,
};

struct Event {
    time_t      ts;          // local epoch (system time)
    EventType   type;
    uint8_t     dev_id;      // 0 = N/A
    uint8_t     trig_id;     // 0 = N/A
    std::string msg;         // human-readable; <= 80 chars
};

// Mount SPIFFS (idempotent). Safe to call multiple times.
esp_err_t init();

// Append a new event. Writes immediately. Returns ESP_OK on success.
esp_err_t append(const Event& ev);

// Convenience overload — fills in ts = time(nullptr).
esp_err_t append(EventType type, uint8_t dev_id, uint8_t trig_id,
                 const char* msg);

// Read up to `max_events` most-recent events (newest first).
std::vector<Event> recent(size_t max_events = 50);

// Erase all history (used by factory-reset workflow).
esp_err_t clear_all();

}  // namespace aqua::history
