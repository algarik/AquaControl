// AquaControl — Global fault registry (Phase 3.5 Block A5)
//
// Anything that can fail at runtime (I2C devices via the watchdog, sensor
// staleness, WiFi/MQTT loss, OTA errors, ...) feeds entries here. The UI
// pulls a snapshot to drive the dashboard fault banner and the Screen 3f
// status table. Each fault is keyed by a 16-bit `code` chosen by the
// originator so that `set(code, ..., true/false)` toggles a single entry.
//
// All public operations are mutex-protected and safe to call from any task.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aqua::faults {

enum class Source : uint8_t {
    I2C     = 0,
    SENSOR  = 1,
    NET     = 2,
    TRIGGER = 3,
    OTHER   = 4,
};

struct Fault {
    uint16_t    code     = 0;
    Source      source   = Source::OTHER;
    std::string label;
    uint64_t    since_ms = 0;   // first time set active
    uint64_t    last_ms  = 0;   // last update
    bool        active   = false;
};

// Mark `code` active or recovered. Inserts on first call.
void set(uint16_t code, Source src, const char* label, bool active);

// Convenience helpers.
inline void raise(uint16_t code, Source src, const char* label) {
    set(code, src, label, true);
}
inline void clear(uint16_t code, Source src, const char* label = nullptr) {
    set(code, src, label ? label : "", false);
}

// Snapshot of all known faults (active and recovered).
std::vector<Fault> snapshot();

// Snapshot of currently-active faults only.
std::vector<Fault> active();

// Quick counter.
uint16_t active_count();

}  // namespace aqua::faults
