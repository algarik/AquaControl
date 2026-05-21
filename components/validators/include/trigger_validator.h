// AquaControl — Trigger / Device cross-validator (Phase 3.5 Block C10).
//
// Runs the 10 checks defined in implementation_plan.md §6.4.1. Returns a
// list of warnings that the UI surfaces inline (yellow banner) and counts
// on the Triggers settings menu badge.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "device_manager.h"
#include "system_config.h"
#include "trigger_manager.h"

namespace aqua::triggers {

enum class WarningSeverity : uint8_t { INFO = 0, WARN = 1, ERROR_ = 2 };

struct ValidationWarning {
    // Which entity the warning is about. Exactly one of these is non-zero:
    uint8_t          trigger_id = 0;
    uint8_t          device_id  = 0;
    uint8_t          check      = 0;       // 1..10 per §6.4.1
    WarningSeverity  severity   = WarningSeverity::WARN;
    std::string      message;
};

class TriggerValidator {
public:
    // Build a complete warning list. `sensor_water_present` /
    // `sensor_ambient_present` reflect runtime detection state from the
    // sensor sampler (check #6). `has_rtc` / `has_wifi` feed check #9.
    static std::vector<ValidationWarning> validate_all(
        aqua::devices::DeviceManager&    dm,
        aqua::triggers::TriggerManager&  tm,
        const aqua::storage::SystemConfig& cfg,
        bool sensor_water_present,
        bool sensor_ambient_present,
        bool has_rtc,
        bool has_wifi);
};

}  // namespace aqua::triggers
