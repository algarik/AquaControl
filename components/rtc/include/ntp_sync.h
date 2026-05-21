// AquaControl — SNTP wrapper (Phase 3.5 Block D13).
//
// Starts esp-sntp against two servers (from SystemConfig). On successful
// sync, latches `last_sync_ok()` to true and stores the UTC epoch. The
// TimeManager will pick the new system time up automatically via
// `now_local()` (because settimeofday() was called inside SNTP); we also
// proactively call `TimeManager::set_time()` so the DS3231 is updated.
#pragma once

#include <ctime>
#include <string>

#include "esp_err.h"

namespace aqua::ntp {

esp_err_t start(const std::string& server1, const std::string& server2);
void      stop();
bool      last_sync_ok();
time_t    last_sync_epoch_utc();

}  // namespace aqua::ntp
