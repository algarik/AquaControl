// AquaControl — Fault registry implementation
#include "faults.h"

#include "ac_logger.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace aqua::faults {

static const char* TAG = "faults";

namespace {

struct State {
    SemaphoreHandle_t  mtx = nullptr;
    std::vector<Fault> list;
};

// Mutex is created once at first use, protected by a portMUX_TYPE spinlock
// that is safe to acquire from both tasks and ISR context on a single core.
// A plain "if (!mtx)" check is not safe when two tasks race to initialise
// the registry before the first fault is raised (e.g. I2C watchdog task and
// the scheduler both raising faults simultaneously at boot).
static portMUX_TYPE s_once_mux = portMUX_INITIALIZER_UNLOCKED;
State s;

void ensure_mtx() {
    if (s.mtx) return;                              // fast path (already inited)
    taskENTER_CRITICAL(&s_once_mux);
    if (!s.mtx) s.mtx = xSemaphoreCreateMutex();   // exactly one creation
    taskEXIT_CRITICAL(&s_once_mux);
}

uint64_t now_ms() { return (uint64_t)esp_timer_get_time() / 1000ULL; }

}  // namespace

void set(uint16_t code, Source src, const char* label, bool active) {
    ensure_mtx();
    if (!s.mtx) return;
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    uint64_t t = now_ms();
    bool found = false;
    for (auto& f : s.list) {
        if (f.code == code && f.source == src) {
            found = true;
            bool was_active = f.active;
            f.active  = active;
            f.last_ms = t;
            if (label && *label) f.label = label;
            if (active && !was_active) {
                f.since_ms = t;
                AC_LOGW(TAG, "RAISE 0x%04X src=%u %s", code,
                        (unsigned)src, f.label.c_str());
            } else if (!active && was_active) {
                AC_LOGI(TAG, "CLEAR 0x%04X src=%u %s", code,
                        (unsigned)src, f.label.c_str());
            }
            break;
        }
    }
    if (!found) {
        Fault f{};
        f.code    = code;
        f.source  = src;
        f.label   = (label ? label : "");
        f.since_ms = t;
        f.last_ms  = t;
        f.active   = active;
        s.list.push_back(std::move(f));
        if (active) {
            AC_LOGW(TAG, "RAISE 0x%04X src=%u %s", code,
                    (unsigned)src, label ? label : "");
        }
    }
    xSemaphoreGive(s.mtx);
}

std::vector<Fault> snapshot() {
    std::vector<Fault> out;
    ensure_mtx();
    if (!s.mtx) return out;
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    out = s.list;
    xSemaphoreGive(s.mtx);
    return out;
}

std::vector<Fault> active() {
    std::vector<Fault> out;
    ensure_mtx();
    if (!s.mtx) return out;
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    for (auto& f : s.list) if (f.active) out.push_back(f);
    xSemaphoreGive(s.mtx);
    return out;
}

uint16_t active_count() {
    ensure_mtx();
    if (!s.mtx) return 0;
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    uint16_t n = 0;
    for (auto& f : s.list) if (f.active) ++n;
    xSemaphoreGive(s.mtx);
    return n;
}

}  // namespace aqua::faults
