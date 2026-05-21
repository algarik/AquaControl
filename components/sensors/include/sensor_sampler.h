// AquaControl — Sensor sampler & cache (Phase 3.5 Block A1)
//
// Periodic FreeRTOS task that polls the SHT3x drivers and stores the latest
// reading in a thread-safe cache. All UI / trigger code reads from the cache
// rather than touching the I2C drivers directly. This decouples consumers
// from bus contention and makes "stale" detection trivial (uptime - ts > N).
//
// Two logical roles: WATER (safety-critical, always present) and AMBIENT
// (optional). main.cpp wires whichever physical SHT3x driver maps to each.
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "sht3x.h"

namespace aqua::sensors {

enum class Role : uint8_t {
    WATER   = 0,
    AMBIENT = 1,
    COUNT
};

struct Reading {
    float    temp_c   = 0.0f;
    float    humidity = 0.0f;   // % RH; 0 for sensor variants without humidity
    uint64_t ts_ms    = 0;      // esp_timer_get_time()/1000 at sample
    bool     valid    = false;  // false until first successful read
};

struct Config {
    // Drivers — may be nullptr if that role has no physical sensor.
    aqua::drivers::Sht3x* water   = nullptr;
    aqua::drivers::Sht3x* ambient = nullptr;
    uint32_t interval_ms          = 5000;  // sample period
    uint32_t stack_size           = 4096;
    UBaseType_t priority          = 2;
    BaseType_t  core_id           = 0;     // pin to Core 0 (LVGL on Core 1)
    // Optional: supply bus + addresses for automatic disconnect recovery.
    aqua::i2c::I2CBus* bus        = nullptr;
    uint8_t water_addr            = 0;
    uint8_t ambient_addr          = 0;
};

// Start the sampler task. Returns false if already running or no sensors.
bool start(const Config& cfg);

bool is_running();

// Thread-safe snapshot of the latest reading for a role.
Reading get(Role role);

// Age in milliseconds since the last successful sample (UINT64_MAX if never).
uint64_t age_ms(Role role);

// Force an immediate sample on the next loop iteration (wakes the task).
void wake_now();

// Apply per-role calibration offsets to future samples and immediately adjust
// the currently cached temperatures. Safe to call from any task.
void apply_calibration(float water_offset_c, float ambient_offset_c);

// Enable or disable sampling of a specific sensor role at runtime.
// Disabled sensors are skipped (no I2C traffic, reading stays valid but stale).
void set_enabled(Role role, bool enabled);

// ---------------------------------------------------------------------------
// 12-hour water temperature history (15-minute intervals, 48 samples max).
// ---------------------------------------------------------------------------
static constexpr int kHistorySlots = 48;

struct HistorySample {
    float    temp_c = 0.0f;
    uint32_t age_min = 0;  // minutes ago (0 = most recent)
    bool     valid  = false;
};

// Fill buf[0..kHistorySlots-1] with history in chronological order (oldest first).
// Returns number of valid samples written (0 if no history yet).
int get_water_history(HistorySample buf[kHistorySlots]);

}  // namespace aqua::sensors
