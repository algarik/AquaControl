#include "i2c_watchdog.h"

#include "ac_logger.h"

namespace aqua::i2c {

static const char* TAG = "I2CWdg";

Watchdog::~Watchdog() { stop(); }

esp_err_t Watchdog::start(const WatchdogConfig& cfg, WatchdogCallback on_event) {
    if (task_ != nullptr)            return ESP_OK;
    if (cfg.bus == nullptr)          return ESP_ERR_INVALID_ARG;
    if (!cfg.bus->initialized())     return ESP_ERR_INVALID_STATE;

    cfg_ = cfg;
    cb_  = std::move(on_event);
    stop_ = false;
    for (auto& f : fail_count_) f = 0;
    for (auto& f : faulted_)    f = false;

    BaseType_t ok = xTaskCreatePinnedToCore(
        &Watchdog::task_entry, "i2c_wdg", 4096, this, 2, &task_, /*Core 0*/ 0);
    if (ok != pdPASS) {
        task_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    AC_LOGI(TAG, "watchdog started, interval %lu ms", (unsigned long)cfg.interval_ms);
    return ESP_OK;
}

void Watchdog::stop() {
    if (task_ == nullptr) return;
    stop_ = true;
    // Task is short-cycle; just wait for it to exit naturally.
    for (int i = 0; i < 20 && task_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool Watchdog::is_faulted(KnownDevice id) const {
    const auto idx = static_cast<size_t>(id);
    if (idx >= static_cast<size_t>(KnownDevice::COUNT)) return false;
    return faulted_[idx];
}

void Watchdog::task_entry(void* arg) {
    static_cast<Watchdog*>(arg)->run();
    auto* self = static_cast<Watchdog*>(arg);
    TaskHandle_t handle = self->task_;
    self->task_ = nullptr;
    (void)handle;
    vTaskDelete(nullptr);
}

void Watchdog::run() {
    while (!stop_) {
        const ScanResult& last = last_result();

        for (uint8_t i = 0; i < static_cast<uint8_t>(KnownDevice::COUNT); ++i) {
            const DeviceInfo& info = last.devices[i];
            // Devices that were absent at boot don't get watchdogged — they
            // weren't installed. (Re-scan on add is a Phase 3 concern.)
            if (info.addr == 0) continue;

            const esp_err_t err = cfg_.bus->probe(info.addr, 20);
            if (err == ESP_OK) {
                if (faulted_[i]) {
                    AC_LOGW(TAG, "%s recovered @ 0x%02X", info.name, info.addr);
                    faulted_[i] = false;
                    if (cb_) cb_(WatchdogEvent{info.id, info.addr, false});
                }
                fail_count_[i] = 0;
                continue;
            }

            fail_count_[i]++;
            switch (fail_count_[i]) {
                case 1:
                    AC_LOGW(TAG, "%s @ 0x%02X: probe failed (%s) — 1/3",
                            info.name, info.addr, esp_err_to_name(err));
                    break;

                case 2: {
                    AC_LOGE(TAG, "%s @ 0x%02X: 2 consecutive failures — "
                                 "resetting I2C bus (H6)", info.name, info.addr);
                    // M-4: use try_reset() to prevent double-reset when sensor_sampler
                    // detects the same failure concurrently.
                    cfg_.bus->try_reset(200);
                    break;
                }

                case 3:
                default:
                    if (!faulted_[i]) {
                        AC_LOGE(TAG, "%s @ 0x%02X: FAULT (3 failures)%s",
                                info.name, info.addr,
                                info.critical ? " [CRITICAL]" : "");
                        faulted_[i] = true;
                        if (cb_) cb_(WatchdogEvent{info.id, info.addr, true});
                    }
                    // Keep counting so the log shows continued absence.
                    if (fail_count_[i] > 250) fail_count_[i] = 3;
                    break;
            }
        }

        // Sleep, then continue. Done as a tight check loop on the stop flag
        // so stop() returns quickly.
        const uint32_t slept = cfg_.interval_ms;
        for (uint32_t t = 0; t < slept && !stop_; t += 100) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    AC_LOGI(TAG, "watchdog stopping");
}

}  // namespace aqua::i2c
