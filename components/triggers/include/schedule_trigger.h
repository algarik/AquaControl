// AquaControl — ScheduleTrigger (Phase 3).
//
// Active when current local minutes-of-day is within [start_min, stop_min)
// on a day enabled in `days[]` (0=Sun..6=Sat per struct tm.tm_wday).
//
// If stop_min <= start_min the window wraps past midnight: it is active
// when minutes >= start_min OR minutes < stop_min, and we also enable the
// previous day's flag for the post-midnight half automatically.
#pragma once

#include <cstdint>

#include "trigger_types.h"

namespace aqua::triggers {

class ScheduleTrigger : public ITrigger {
public:
    ScheduleTrigger(uint8_t id, std::string name)
        : ITrigger(id, std::move(name)) {}

    uint16_t start_min = 0;     // minutes from midnight (0..1439)
    uint16_t stop_min  = 0;
    bool     days[7] = {true, true, true, true, true, true, true};  // Sun..Sat

    // "Every N seconds" interval mode (values in seconds, not minutes).
    bool     use_interval    = false;   // if true, ignore start/stop window
    uint16_t interval_sec    = 3600;    // cycle period in seconds (default 1 h)
    uint16_t on_duration_sec = 300;     // ON duration per cycle in seconds (default 5 min)
    bool     daily_at        = false;   // if true (sub-mode): once per day at a fixed time
    uint16_t daily_at_min    = 480;     // time of day for daily_at (08:00 default)

    bool evaluate() override;
    TriggerType get_type() const override { return TriggerType::SCHEDULE; }
};

}  // namespace aqua::triggers
