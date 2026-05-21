// AquaControl — unified time source.
//
// Hierarchy (most authoritative first):
//   1. NTP (Phase 5 — pushes time into system clock AND the DS1307).
//   2. DS1307 RTC (read once at boot, mirrored into system clock via settimeofday).
//   3. Build-time fallback (already applied in main.cpp if both above are
//      missing or wrong).
//
// Once seeded, the scheduler reads time via TimeManager::now_local() which
// returns the current local time as a struct tm. UTC offset comes from
// system config (Phase 4 UI; currently 0).
//
// All time arithmetic in triggers uses `minutes_since_midnight()` for
// schedule windows.
#pragma once

#include <atomic>
#include <ctime>

#include "esp_err.h"
#include "ds1307.h"

namespace aqua::time_mgr {

class TimeManager {
public:
    // Bind the DS1307 driver. If `rtc` is nullptr, only the system clock
    // and build-time fallback are available.
    static void bind_rtc(aqua::drivers::Ds1307* rtc);

    // Configure UTC offset in minutes (e.g. +180 for UTC+03:00). Default 0.
    static void set_utc_offset_minutes(int16_t offset);
    static int16_t utc_offset_minutes();

    // Read RTC at boot and seed the system clock via settimeofday. Safe to
    // call even if the RTC is nullptr / invalid (no-op).
    static esp_err_t sync_system_from_rtc();

    // Write a new local time into both the DS1307 (if bound) and the system
    // clock. `local_tm` is interpreted as local time using the current UTC
    // offset. Used by the first-run wizard, Screen 3b manual time set, and
    // NTP sync (Phase 5).
    static esp_err_t set_time(const struct tm& local_tm);

    // Current local time. Always returns something sensible: prefers system
    // clock once seeded, falls back to RTC, falls back to 1970-01-01.
    static struct tm now_local();

    // Convenience: minutes since local midnight (0..1439).
    static int minutes_since_midnight();

    // True if the system clock has been seeded from a real source this boot.
    static bool is_synced();

    // Called by the NTP layer after SNTP has already called settimeofday().
    // Marks the clock as synced and writes the correct local time to the RTC.
    // Must NOT call set_time() — that would re-apply the UTC offset on top of
    // the already-correct UTC epoch that SNTP placed in the system clock.
    static void after_ntp_sync();

private:
    static aqua::drivers::Ds1307* s_rtc;
    static int16_t                s_utc_offset_min;
    // Written by NTP callback (SNTP task) and by set_time() (LVGL task);
    // read by is_synced() from multiple tasks.  Must be atomic.
    static std::atomic<bool>      s_synced;
};

}  // namespace aqua::time_mgr
