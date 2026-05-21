// AquaControl — SolarTrigger (Phase 3 skeleton).
//
// Active around a solar event (sunrise/sunset) with offset and duration.
// The actual NOAA solar calculator implementation is deferred to a follow-
// up step; until then evaluate() returns false unless cached sunrise/sunset
// minutes have been pushed in via set_solar_times().
#pragma once

#include <cstdint>

#include "trigger_types.h"

namespace aqua::triggers {

enum class SolarEvent : uint8_t { SUNRISE = 0, SUNSET = 1 };

class SolarTrigger : public ITrigger {
public:
    SolarTrigger(uint8_t id, std::string name)
        : ITrigger(id, std::move(name)) {}

    SolarEvent event       = SolarEvent::SUNRISE;
    int16_t    offset_min  = 0;        // -360..+360
    uint16_t   duration_min = 60;

    // "End at solar event" mode — if true, ignore duration_min.
    bool       use_end_event   = false;
    SolarEvent end_event       = SolarEvent::SUNRISE;
    int16_t    end_offset_min  = 0;

    // Pre-computed for today (set by SolarCalculator once per day).
    int16_t sunrise_min_today = -1;    // -1 = not computed
    int16_t sunset_min_today  = -1;
    bool    valid             = false; // false in polar regions

    bool evaluate() override;
    TriggerType get_type() const override { return TriggerType::SOLAR; }
};

}  // namespace aqua::triggers
