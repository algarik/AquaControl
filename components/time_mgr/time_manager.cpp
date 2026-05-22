#include "time_manager.h"

#include <atomic>
#include <sys/time.h>

#include "ac_logger.h"
#include "esp_timer.h"
#include "faults.h"
#include "solar_calc.h"

namespace aqua::time_mgr {

static const char* TAG = "TimeMgr";

aqua::drivers::Ds1307* TimeManager::s_rtc             = nullptr;
int16_t                TimeManager::s_utc_offset_min  = 0;
std::atomic<bool>      TimeManager::s_synced{false};

// M-1: track last successful NTP sync for fault surfacing.
static uint64_t s_last_ntp_sync_ms = 0;
static bool     s_ever_synced      = false;

void TimeManager::bind_rtc(aqua::drivers::Ds1307* rtc) { s_rtc = rtc; }

void TimeManager::set_utc_offset_minutes(int16_t offset) { s_utc_offset_min = offset; }
int16_t TimeManager::utc_offset_minutes() { return s_utc_offset_min; }

esp_err_t TimeManager::sync_system_from_rtc() {
    if (s_rtc == nullptr || !s_rtc->initialized()) {
        AC_LOGW(TAG, "No RTC bound; system clock not seeded");
        return ESP_ERR_INVALID_STATE;
    }
    struct tm rtc_tm = {};
    bool valid = false;
    esp_err_t err = s_rtc->get_time(&rtc_tm, &valid);
    if (err != ESP_OK || !valid) {
        AC_LOGW(TAG, "RTC read failed or invalid; system clock not seeded");
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    // DS1307 stores local time directly; convert to UTC epoch by treating
    // the value as local and subtracting the UTC offset.
    // mktime() treats the struct as local time per the C library's TZ env;
    // we use timegm-equivalent then adjust by offset.
    rtc_tm.tm_isdst = 0;
    time_t local_as_utc = mktime(&rtc_tm);    // interprets as local; TZ unset = UTC
    time_t real_utc = local_as_utc - (time_t)s_utc_offset_min * 60;

    struct timeval tv = { .tv_sec = real_utc, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    s_synced.store(true, std::memory_order_release);
    AC_LOGI(TAG, "System clock seeded from DS1307 (UTC offset %+d min)",
            s_utc_offset_min);
    return ESP_OK;
}

esp_err_t TimeManager::set_time(const struct tm& local_tm_in) {
    struct tm local_tm = local_tm_in;
    local_tm.tm_isdst = 0;

    // mktime() with TZ unset treats the input as UTC, so the result is the
    // epoch if the local fields were UTC. Subtract the UTC offset to get
    // the actual UTC epoch.
    time_t local_as_utc = mktime(&local_tm);
    time_t real_utc     = local_as_utc - (time_t)s_utc_offset_min * 60;

    struct timeval tv = { .tv_sec = real_utc, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    s_synced.store(true, std::memory_order_release);

    if (s_rtc && s_rtc->initialized()) {
        esp_err_t err = s_rtc->set_time(local_tm);
        if (err != ESP_OK) {
            AC_LOGW(TAG, "DS1307 write failed: %s", esp_err_to_name(err));
            return err;
        }
    }
    AC_LOGI(TAG, "Time set: %04d-%02d-%02d %02d:%02d:%02d (local, UTC%+d min)",
            local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
            local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec,
            s_utc_offset_min);
    return ESP_OK;
}

struct tm TimeManager::now_local() {
    time_t t_utc = time(nullptr);
    time_t t_local = t_utc + (time_t)s_utc_offset_min * 60;
    struct tm out = {};
    gmtime_r(&t_local, &out);
    return out;
}

int TimeManager::minutes_since_midnight() {
    struct tm t = now_local();
    return t.tm_hour * 60 + t.tm_min;
}

bool TimeManager::is_synced() { return s_synced.load(std::memory_order_acquire); }

void TimeManager::after_ntp_sync() {
    s_synced.store(true, std::memory_order_release);
    // M-1: record sync time and clear the NTP-absence fault.
    s_last_ntp_sync_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    s_ever_synced      = true;
    aqua::faults::clear(0x0500, aqua::faults::Source::SENSOR);
    // SNTP has already called settimeofday() — system clock is correct UTC.
    // We only need to mirror the correct local time into the RTC.
    if (s_rtc && s_rtc->initialized()) {
        struct tm local_tm = now_local();
        esp_err_t err = s_rtc->set_time(local_tm);
        if (err != ESP_OK) {
            AC_LOGW(TAG, "after_ntp_sync: DS1307 write failed: %s", esp_err_to_name(err));
        }
    }
    struct tm local_tm = now_local();
    AC_LOGI(TAG, "NTP synced: local=%04d-%02d-%02d %02d:%02d:%02d (UTC%+d min)",
            local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
            local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec,
            s_utc_offset_min);
    aqua::solar::request_recalc();
}

// M-1: raise fault 0x0500 if NTP was once available but hasn't synced in 24 h.
void TimeManager::check_health() {
    if (!s_ever_synced) return;
    const uint64_t age_ms  = (uint64_t)esp_timer_get_time() / 1000ULL - s_last_ntp_sync_ms;
    const uint64_t kMaxMs  = 24ULL * 3600ULL * 1000ULL;
    if (age_ms > kMaxMs) {
        aqua::faults::raise(0x0500, aqua::faults::Source::SENSOR,
                            "NTP: no sync in 24 h");
    }
}

}  // namespace aqua::time_mgr
