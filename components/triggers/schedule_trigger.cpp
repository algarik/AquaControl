#include "schedule_trigger.h"

#include "time_manager.h"

namespace aqua::triggers {

bool ScheduleTrigger::evaluate() {
    if (!enabled) return false;

    struct tm t = aqua::time_mgr::TimeManager::now_local();
    const int now_min   = t.tm_hour * 60 + t.tm_min;
    const int now_sec   = now_min * 60 + t.tm_sec;
    const int today_dow = t.tm_wday;                       // 0=Sun..6=Sat
    const int yest_dow  = (today_dow + 6) % 7;

    if (use_interval) {
        if (daily_at) {
            // Once per day: ON from daily_at_min*60 for on_duration_sec.
            // Correctly handles windows that wrap past midnight.
            const int start_sec = (int)daily_at_min * 60;
            const int stop_sec  = start_sec + (int)on_duration_sec;
            if (stop_sec <= 86400) {
                // Window stays within the same calendar day.
                return days[today_dow] && now_sec >= start_sec && now_sec < stop_sec;
            }
            // Window wraps past midnight: pre-midnight governed by today's day
            // flag, post-midnight portion governed by yesterday's day flag.
            const int wrap_sec = stop_sec - 86400;
            if (days[today_dow] && now_sec >= start_sec) return true;
            if (days[yest_dow]  && now_sec <  wrap_sec)  return true;
            return false;
        }

        // Cyclic: ON for on_duration_sec out of every interval_sec.
        if (interval_sec == 0) return false;

        // Primary check: today's active cycle phase.
        if (days[today_dow] && (now_sec % (int)interval_sec) < (int)on_duration_sec)
            return true;

        // Spillover check: did yesterday's last cycle cross midnight and is it
        // still within its ON window?
        // midnight_phase = position within the cycle at the moment midnight struck.
        // If > 0, a cycle was mid-run at midnight; the remainder extends into today.
        const int midnight_phase = 86400 % (int)interval_sec;
        if (midnight_phase > 0 && midnight_phase < (int)on_duration_sec) {
            const int remain = (int)on_duration_sec - midnight_phase;
            if (days[yest_dow] && now_sec < remain) return true;
        }
        return false;
    }

    // Time-window mode (original logic).
    if (start_min < stop_min) {
        // Same-day window.
        return days[today_dow] && now_min >= start_min && now_min < stop_min;
    }
    if (start_min > stop_min) {
        // Wrap-past-midnight window.
        if (days[today_dow] && now_min >= start_min) return true;
        if (days[yest_dow]  && now_min <  stop_min)  return true;
        return false;
    }
    // start == stop => disabled / zero-length window.
    return false;
}

}  // namespace aqua::triggers
