// AquaControl — Solar calculator (Phase 3.5 Block C9)
//
// NOAA-based sunrise / sunset computation expressed as minutes-from-local-
// midnight. Implementation follows the public NOAA Solar Calculator
// equations (Reda & Andreas simplified form sufficient for triggers).
//
// Used by SolarTrigger: scheduler invokes compute_for_today() once at boot
// and again at every local midnight (or when lat/lon/tz changes), pushing
// the result into each SolarTrigger's sunrise_min_today / sunset_min_today
// fields.
#pragma once

#include <cstdint>
#include <ctime>

namespace aqua::solar {

struct DayResult {
    int16_t sunrise_min = -1;   // -1 = no sunrise on this date (polar)
    int16_t sunset_min  = -1;   // -1 = no sunset on this date
    bool    valid       = false;
};

// Compute sunrise/sunset for a single date.
//   year:  full year (e.g. 2026)
//   month: 1..12
//   day:   1..31
//   lat/lon: decimal degrees (north/east positive)
//   utc_offset_min: timezone offset from UTC in minutes
// Returns sunrise/sunset as minutes-since-local-midnight (0..1439). Sets
// valid=false in polar conditions where the sun never rises/sets.
DayResult compute(int year, int month, int day,
                  float lat, float lon, int16_t utc_offset_min);

// Signal that solar times should be recomputed at the next scheduler tick
// (e.g. after a location/timezone change or an NTP sync that may shift the
// local date).  Thread-safe — may be called from any task.
void request_recalc();

// Returns true and clears the flag atomically.  Called by the scheduler
// pre_eval hook.
bool consume_recalc();

}  // namespace aqua::solar
