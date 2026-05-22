// AquaControl — Sensor sampler implementation
#include "sensor_sampler.h"

#include <atomic>
#include <cstring>

#include "ac_logger.h"
#include "app_config.h"
#include "esp_timer.h"
#include "faults.h"
#include "freertos/semphr.h"

namespace aqua::sensors {

static const char* TAG = "sensors";

namespace {

// Moving-average smoothing buffer (N=8 samples per role).
static constexpr int kSmoothing = 8;
struct SmoothBuf {
    float temp_c[kSmoothing]   = {};
    float humidity[kSmoothing] = {};
    int   head = 0;
    int   count = 0;

    void push(float t, float h) {
        temp_c[head]   = t;
        humidity[head] = h;
        head = (head + 1) % kSmoothing;
        if (count < kSmoothing) ++count;
    }
    void average(float* out_t, float* out_h) const {
        float st = 0.f, sh = 0.f;
        for (int i = 0; i < count; ++i) {
            st += temp_c[i];
            sh += humidity[i];
        }
        *out_t = count ? st / count : 0.f;
        *out_h = count ? sh / count : 0.f;
    }
};

struct State {
    Config            cfg{};
    TaskHandle_t      task = nullptr;
    SemaphoreHandle_t mtx  = nullptr;
    std::atomic<bool> running{false};
    Reading           cache[(size_t)Role::COUNT]{};
    SmoothBuf         smooth[(size_t)Role::COUNT]{};
    float             cal_offset_c[(size_t)Role::COUNT]{};  // applied after smoothing
    // 12h water temperature history (15-min intervals)
    float    hist_temp[kHistorySlots]{};   // circular buffer values
    int      hist_head  = 0;               // write index
    int      hist_count = 0;              // valid entries (0..kHistorySlots)
    uint32_t hist_elapsed_ms = 0;         // ms since last history snapshot
    // I2C disconnect recovery: consecutive error counter per role
    int      consec_err[(size_t)Role::COUNT]{};
    // Per-role enabled flags (default true; false skips sampling entirely).
    std::atomic<bool> enabled[(size_t)Role::COUNT]{ {true}, {true} };
};
static constexpr uint32_t kHistIntervalMs = 15 * 60 * 1000u;

State s;

void store(Role r, const Reading& rd) {
    if (s.mtx) xSemaphoreTake(s.mtx, portMAX_DELAY);
    s.cache[(size_t)r] = rd;
    if (s.mtx) xSemaphoreGive(s.mtx);
}

static constexpr int kMaxConsecErrors = 5;  // errors before bus recovery attempt
static constexpr uint16_t kFaultCodeWater   = 0x0010;
static constexpr uint16_t kFaultCodeAmbient = 0x0011;

void attempt_recovery(Role r, aqua::drivers::Sht3x* drv) {
    if (!s.cfg.bus || !drv) return;
    uint8_t addr = (r == Role::WATER) ? s.cfg.water_addr : s.cfg.ambient_addr;
    if (!addr) return;
    AC_LOGW(TAG, "%s sensor: attempting I2C bus reset + re-init",
            r == Role::WATER ? "water" : "ambient");
    drv->deinit();  // release stale handle before bus reset
    // M-4: use try_reset() to avoid double-reset if watchdog fires concurrently.
    s.cfg.bus->try_reset(200);
    vTaskDelay(pdMS_TO_TICKS(100));
    // Re-initialize the driver (re-adds the device on the bus).
    esp_err_t err = drv->init(*s.cfg.bus, addr);
    if (err == ESP_OK) {
        AC_LOGI(TAG, "%s sensor: recovered @ 0x%02X", r == Role::WATER ? "water" : "ambient", addr);
        s.consec_err[(size_t)r] = 0;
        uint16_t code = (r == Role::WATER) ? kFaultCodeWater : kFaultCodeAmbient;
        aqua::faults::clear(code, aqua::faults::Source::SENSOR);
    } else {
        AC_LOGW(TAG, "%s sensor: recovery failed: %s",
                r == Role::WATER ? "water" : "ambient", esp_err_to_name(err));
    }
}

void sample_one(Role r, aqua::drivers::Sht3x* drv) {
    if (!drv) return;
    if (!s.enabled[(size_t)r].load(std::memory_order_relaxed)) return;
    if (!drv->initialized()) {
        // Try to init if not yet (e.g. sensor not present at boot but reconnected).
        if (s.cfg.bus) {
            uint8_t addr = (r == Role::WATER) ? s.cfg.water_addr : s.cfg.ambient_addr;
            if (addr) drv->init(*s.cfg.bus, addr);
        }
        if (!drv->initialized()) return;
    }
    aqua::drivers::Sht3xSample sam{};
    esp_err_t err = drv->read(&sam);
    if (err != ESP_OK || !sam.valid) {
        const char* role_name = (r == Role::WATER) ? "water" : "ambient";
        uint16_t    code      = (r == Role::WATER) ? kFaultCodeWater : kFaultCodeAmbient;
        ++s.consec_err[(size_t)r];
        AC_LOGW(TAG, "%s sample failed (%d/%d): %s", role_name,
                s.consec_err[(size_t)r], kMaxConsecErrors, esp_err_to_name(err));
        if (s.consec_err[(size_t)r] % kMaxConsecErrors == 0) {
            aqua::faults::raise(code, aqua::faults::Source::SENSOR,
                                r == Role::WATER ? "Water sensor offline" : "Ambient sensor offline");
            attempt_recovery(r, drv);
        }
        // Once the fault threshold is reached, mark the cache invalid so all
        // temperature triggers receive sensor_present=false and fail safe
        // (e.g. heater turns OFF rather than running on stale temperature).
        if (s.consec_err[(size_t)r] >= kMaxConsecErrors) {
            Reading inv{};
            inv.ts_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
            store(r, inv);                       // valid=false by default
            s.smooth[(size_t)r] = SmoothBuf{};  // discard stale averaged samples
        }
        return;
    }
    // Successful read — reset error counter and clear fault.
    if (s.consec_err[(size_t)r] > 0) {
        s.consec_err[(size_t)r] = 0;
        uint16_t code = (r == Role::WATER) ? kFaultCodeWater : kFaultCodeAmbient;
        aqua::faults::clear(code, aqua::faults::Source::SENSOR);
    }
    // Push into smoothing buffer and store the moving average + calibration offset.
    auto& buf = s.smooth[(size_t)r];
    buf.push(sam.temp_c, sam.humidity);
    Reading rd{};
    buf.average(&rd.temp_c, &rd.humidity);
    rd.temp_c += s.cal_offset_c[(size_t)r];
    rd.ts_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    rd.valid = true;
    store(r, rd);
}

void task_fn(void*) {
    AC_LOGI(TAG, "sampler task started (interval=%u ms)",
            (unsigned)s.cfg.interval_ms);
    // First pass immediately.
    sample_one(Role::WATER, s.cfg.water);
    sample_one(Role::AMBIENT, s.cfg.ambient);
    {
        Reading w = get(Role::WATER);
        Reading a = get(Role::AMBIENT);
#if AC_VERBOSE_BOOT
        if (w.valid) AC_LOGI(TAG, "water:   T=%.2f C  RH=%.1f %%", w.temp_c, w.humidity);
        if (a.valid) AC_LOGI(TAG, "ambient: T=%.2f C  RH=%.1f %%", a.temp_c, a.humidity);
#endif

        // Seed history with the initial water reading so the chart is visible
        // as soon as the screen is opened (without waiting 30+ minutes for the
        // regular 15-minute interval to fire twice).  Two identical entries are
        // pushed so the graph's n>=2 threshold is met from the start; real
        // 15-minute snapshots will replace the flat line over time.
        if (w.valid && s.mtx) {
            xSemaphoreTake(s.mtx, portMAX_DELAY);
            for (int i = 0; i < 2; ++i) {
                s.hist_temp[s.hist_head] = w.temp_c;
                s.hist_head = (s.hist_head + 1) % kHistorySlots;
                if (s.hist_count < kHistorySlots) ++s.hist_count;
            }
            xSemaphoreGive(s.mtx);
        }
    }

    while (s.running.load()) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(s.cfg.interval_ms));
        if (!s.running.load()) break;
        sample_one(Role::WATER, s.cfg.water);
        sample_one(Role::AMBIENT, s.cfg.ambient);
        // Accumulate time and push history snapshot every 15 minutes.
        s.hist_elapsed_ms += s.cfg.interval_ms;
        if (s.hist_elapsed_ms >= kHistIntervalMs) {
            s.hist_elapsed_ms = 0;
            Reading w = get(Role::WATER);
            if (w.valid) {
                if (s.mtx) xSemaphoreTake(s.mtx, portMAX_DELAY);
                s.hist_temp[s.hist_head] = w.temp_c;
                s.hist_head = (s.hist_head + 1) % kHistorySlots;
                if (s.hist_count < kHistorySlots) ++s.hist_count;
                if (s.mtx) xSemaphoreGive(s.mtx);
            }
        }
    }
    s.task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool start(const Config& cfg) {
    if (s.running.load()) {
        AC_LOGW(TAG, "start: already running");
        return false;
    }
    if (!cfg.water && !cfg.ambient) {
        AC_LOGW(TAG, "start: no sensors provided");
        return false;
    }
    s.cfg = cfg;
    if (!s.mtx) s.mtx = xSemaphoreCreateMutex();
    if (!s.mtx) {
        AC_LOGE(TAG, "mutex alloc failed");
        return false;
    }
    s.running.store(true);
    BaseType_t ok = xTaskCreatePinnedToCore(
        task_fn, "ac_sense", cfg.stack_size, nullptr,
        cfg.priority, &s.task, cfg.core_id);
    if (ok != pdPASS) {
        AC_LOGE(TAG, "task create failed");
        s.running.store(false);
        return false;
    }
    return true;
}

bool is_running() { return s.running.load(); }

Reading get(Role role) {
    Reading out{};
    if ((size_t)role >= (size_t)Role::COUNT) return out;
    if (s.mtx) xSemaphoreTake(s.mtx, portMAX_DELAY);
    out = s.cache[(size_t)role];
    if (s.mtx) xSemaphoreGive(s.mtx);
    return out;
}

uint64_t age_ms(Role role) {
    Reading rd = get(role);
    if (!rd.valid) return UINT64_MAX;
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000ULL;
    return (now > rd.ts_ms) ? (now - rd.ts_ms) : 0;
}

void wake_now() {
    if (s.task) xTaskNotifyGive(s.task);
}

void apply_calibration(float water_offset_c, float ambient_offset_c) {
    if (!s.mtx) return;
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    const float new_off[2] = { water_offset_c, ambient_offset_c };
    for (size_t i = 0; i < (size_t)Role::COUNT; ++i) {
        const float delta = new_off[i] - s.cal_offset_c[i];
        s.cal_offset_c[i] = new_off[i];
        if (s.cache[i].valid) {
            s.cache[i].temp_c += delta;
        }
    }
    xSemaphoreGive(s.mtx);
}

void set_enabled(Role role, bool enabled) {
    s.enabled[(size_t)role].store(enabled, std::memory_order_relaxed);
    AC_LOGI(TAG, "%s sensor %s", role == Role::WATER ? "water" : "ambient",
            enabled ? "enabled" : "disabled");
}

int get_water_history(HistorySample buf[kHistorySlots]) {
    if (!s.mtx) return 0;
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    const int n = s.hist_count;
    const int head = s.hist_head;
    // Copy into buf in chronological order (oldest first).
    for (int i = 0; i < n; ++i) {
        // oldest = head - n + i (wrapping)
        int idx = (head - n + i + kHistorySlots) % kHistorySlots;
        buf[i].temp_c  = s.hist_temp[idx];
        buf[i].age_min = (uint32_t)((n - 1 - i) * 15);
        buf[i].valid   = true;
    }
    xSemaphoreGive(s.mtx);
    return n;
}

}  // namespace aqua::sensors
