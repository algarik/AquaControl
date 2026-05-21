#include "solar_trigger.h"

#include "time_manager.h"

namespace aqua::triggers {

bool SolarTrigger::evaluate() {
    if (!enabled || !valid) return false;

    const int base_start = (event == SolarEvent::SUNRISE) ? sunrise_min_today : sunset_min_today;
    if (base_start < 0) return false;

    const int start = base_start + offset_min;
    if (start < 0)    return false;  // offset pushed start before midnight — no wrap
    if (start >= 1440) return false;  // offset pushed start past midnight — no wrap

    const int now = aqua::time_mgr::TimeManager::minutes_since_midnight();

    int stop;
    if (use_end_event) {
        const int base_end = (end_event == SolarEvent::SUNRISE) ? sunrise_min_today : sunset_min_today;
        if (base_end < 0) return false;
        stop = base_end + end_offset_min;
        // Handle case where end is on the next day (e.g. sunset → next sunrise).
        if (stop <= start) stop += 1440;
    } else {
        stop = start + static_cast<int>(duration_min);
        if (stop > 1440) {
            // Duration runs past midnight — clamped to 23:59 (validator warns user).
            return now >= start && now <= 1439;
        }
    }

    // Handle wrap past midnight for end_event mode.
    if (stop > 1440) {
        return now >= start || now < (stop - 1440);
    }
    return now >= start && now < stop;
}

}  // namespace aqua::triggers
