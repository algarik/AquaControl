// AquaControl — SNTP wrapper implementation.
#include "ntp_sync.h"

#include <ctime>

#include "ac_logger.h"
#include "esp_sntp.h"
#include "history_log.h"
#include "time_manager.h"

namespace aqua::ntp {

static const char* TAG = "ntp";

static bool   s_started     = false;
static bool   s_last_ok     = false;
static time_t s_last_sync   = 0;

static void on_time_sync(struct timeval* /*tv*/) {
    s_last_ok   = true;
    s_last_sync = time(nullptr);

    // SNTP has already called settimeofday() — the system clock is now
    // correct UTC. after_ntp_sync() marks the clock as synced and writes
    // the correct local time to the RTC; it does NOT touch settimeofday.
    aqua::time_mgr::TimeManager::after_ntp_sync();

    struct tm local_tm = aqua::time_mgr::TimeManager::now_local();
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local_tm);
    AC_LOGI(TAG, "synced: local=%s", buf);
    aqua::history::append(aqua::history::EventType::NTP_SYNC, 0, 0, buf);
}

esp_err_t start(const std::string& server1, const std::string& server2) {
    if (s_started) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    if (!server1.empty()) esp_sntp_setservername(0, server1.c_str());
    if (!server2.empty()) esp_sntp_setservername(1, server2.c_str());
    sntp_set_time_sync_notification_cb(on_time_sync);
    esp_sntp_init();
    s_started = true;
    AC_LOGI(TAG, "started: '%s' / '%s'",
            server1.c_str(), server2.c_str());
    return ESP_OK;
}

void stop() {
    if (!s_started) return;
    esp_sntp_stop();
    s_started = false;
}

bool   last_sync_ok()        { return s_last_ok; }
time_t last_sync_epoch_utc() { return s_last_sync; }

}  // namespace aqua::ntp
