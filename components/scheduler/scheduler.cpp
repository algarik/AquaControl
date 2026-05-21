#include "scheduler.h"

#include <atomic>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ac_logger.h"
#include "esp_timer.h"
#include "faults.h"
#include "mqtt_client_aqua.h"
#include "sensor_sampler.h"

namespace aqua::scheduler {

static const char* TAG = "Scheduler";

static Config           s_cfg{};
static TaskHandle_t     s_task    = nullptr;
static std::atomic<bool> s_running{false};

// ---- Heater safety checker ----------------------------------------
static void check_heater_safety() {
    if (s_cfg.heater_device_id == 0 || !s_cfg.dm) return;

    auto* heater = s_cfg.dm->find(s_cfg.heater_device_id);
    if (!heater) return;

    static float     prev_water_c         = -999.f;
    static uint64_t  heater_on_since_ms   = 0;
    static uint64_t  last_temp_change_ms  = 0;

    const uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    const bool heater_on  = heater->current_active();
    aqua::sensors::Reading water = aqua::sensors::get(aqua::sensors::Role::WATER);

    if (!heater_on) {
        heater_on_since_ms = 0;
        // Check for unexpected temperature rise while heater is confirmed OFF.
        if (water.valid && prev_water_c > -900.f) {
            const uint64_t window_ms = 5ULL * 60ULL * 1000ULL;  // 5 min
            if (water.temp_c - prev_water_c > 2.0f &&
                now_ms - water.ts_ms < window_ms) {
                aqua::faults::raise(0x0301, aqua::faults::Source::SENSOR,
                                    "Water: unexpected heat rise");
            } else {
                aqua::faults::clear(0x0301, aqua::faults::Source::SENSOR);
            }
        }
        // Clear stuck-sensor fault when heater goes OFF.
        aqua::faults::clear(0x0300, aqua::faults::Source::SENSOR);
        prev_water_c = water.valid ? water.temp_c : prev_water_c;
        last_temp_change_ms = 0;
        return;
    }

    // Heater is ON.
    if (heater_on_since_ms == 0) {
        heater_on_since_ms  = now_ms;
        last_temp_change_ms = now_ms;
        prev_water_c        = water.valid ? water.temp_c : -999.f;
        return;
    }

    if (water.valid) {
        if (std::fabsf(water.temp_c - prev_water_c) > 0.5f) {
            // Temperature is changing — update baseline and reset timer.
            prev_water_c        = water.temp_c;
            last_temp_change_ms = now_ms;
            aqua::faults::clear(0x0300, aqua::faults::Source::SENSOR);
        }
    }

    // Check for sensor-stuck fault: heater ON for > timeout without temp rise.
    const uint64_t timeout_ms = (uint64_t)s_cfg.heater_fault_timeout_s * 1000ULL;
    const uint64_t no_change_ms = last_temp_change_ms > 0
        ? now_ms - last_temp_change_ms : now_ms - heater_on_since_ms;
    if (no_change_ms >= timeout_ms) {
        aqua::faults::raise(0x0300, aqua::faults::Source::SENSOR,
                            "Heater: sensor stuck / no heat transfer");
        // Force heater OFF via INDEFINITE override.
        heater->set_override(aqua::devices::OverrideMode::INDEFINITE, false, 0);
        AC_LOGE(TAG, "Heater safety: forced OFF (device %u)", s_cfg.heater_device_id);
    }
}

// One pass: ask trigger manager for per-device desired states then apply
// them through each device's override-aware resolve_active().
static void run_one_pass(bool full_pass) {
    if (!s_cfg.dm || !s_cfg.tm) return;

    // Optional sensor-cache push, etc., before triggers evaluate.
    if (s_cfg.pre_eval) s_cfg.pre_eval(*s_cfg.tm);

    // Collect desired states + driver-id keyed by device id.
    // Use direct-index stack arrays (H2) to avoid heap allocation per tick.
    // Device IDs are 1..255; index 0 is unused. 256 bytes + 256 bytes = 512 B.
    constexpr int kMaxDevId = 256;
    bool    desired_active[kMaxDevId] = {};
    uint8_t desired_driver[kMaxDevId] = {};

    s_cfg.tm->evaluate_all([&](uint8_t did, bool active, uint8_t drv) {
        if (did > 0) {
            desired_active[did] = active;
            desired_driver[did] = drv;
        }
    });

    // Apply to every enabled device. Devices not referenced by any trigger
    // default to inactive (off).
    s_cfg.dm->for_each([&](aqua::devices::IDevice& dev) {
        bool target  = (dev.id < kMaxDevId) ? desired_active[dev.id] : false;
        uint8_t drv  = (dev.id < kMaxDevId) ? desired_driver[dev.id] : 0;
        bool resolved = dev.resolve_active(target);
        dev.last_driver_trigger_id = resolved ? drv : 0;
        bool prev_active = dev.current_active();
        if (full_pass || resolved != prev_active) {
            // full_pass=true forces re-drive even when cached state matches,
            // so a hardware power-glitch (relay snap-off) is corrected.
            dev.apply(resolved, full_pass);
        }
        // M3: notify UI if the effective state changed.
        if (s_cfg.on_device_changed && dev.current_active() != prev_active) {
            s_cfg.on_device_changed();
            // B2: publish state change to MQTT broker if connected.
            if (aqua::mqtt::connected())
                aqua::mqtt::publish_device_state(dev);
        }
    });

    // H1: auto-detect heater device by role if not set via Config.
    if (s_cfg.heater_device_id == 0 && s_cfg.dm) {
        s_cfg.dm->for_each([](aqua::devices::IDevice& dev) {
            if (dev.role == aqua::devices::DeviceRole::HEATER) {
                s_cfg.heater_device_id = dev.id;  // pick first HEATER-role device
            }
        });
    }

    // H1: heater safety check after all device states are resolved.
    check_heater_safety();
}

static void scheduler_task(void* /*arg*/) {
    AC_LOGI(TAG, "task started: fast=%ums full=%ums",
            (unsigned)s_cfg.fast_interval_ms,
            (unsigned)s_cfg.full_interval_ms);

    TickType_t last_full = xTaskGetTickCount();
    s_running = true;

    // Run one full pass immediately so devices reach a known state.
    run_one_pass(true);

    while (s_running.load()) {
        // Sleep until next fast tick or until wake_now() notifies us.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(s_cfg.fast_interval_ms));

        TickType_t now = xTaskGetTickCount();
        bool full = (now - last_full) >= pdMS_TO_TICKS(s_cfg.full_interval_ms);
        if (full) last_full = now;

        run_one_pass(full);
    }

    AC_LOGW(TAG, "task exiting");
    s_task = nullptr;
    vTaskDelete(nullptr);
}

bool start(const Config& cfg) {
    if (s_task != nullptr) {
        AC_LOGW(TAG, "already running");
        return false;
    }
    if (cfg.dm == nullptr || cfg.tm == nullptr) {
        AC_LOGE(TAG, "start() requires non-null dm and tm");
        return false;
    }
    s_cfg = cfg;

    BaseType_t ok = xTaskCreatePinnedToCore(
        scheduler_task, "ac_sched", 4096, nullptr,
        /*priority=*/3, &s_task, /*core=*/0);
    if (ok != pdPASS) {
        AC_LOGE(TAG, "xTaskCreatePinnedToCore failed");
        s_task = nullptr;
        return false;
    }
    return true;
}

bool is_running() { return s_running.load(); }

void wake_now() {
    if (s_task) xTaskNotifyGive(s_task);
}

}  // namespace aqua::scheduler
