// AquaControl — Inactivity dim & fault-override manager implementation.
#include "dim_manager.h"

#include "ac_logger.h"
#include "activity.h"
#include "backlight.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "screen_manager.h"
#include "screensaver_screen.h"
#include "system_config.h"

namespace aqua::ui::dim {

namespace {

constexpr const char* TAG = "DimMgr";

struct State {
    const aqua::storage::SystemConfig* cfg = nullptr;
    TaskHandle_t task = nullptr;
    uint8_t      last_applied_pct = 255;
    // Screensaver: true while the screensaver screen is on the stack.
    // Reset automatically when the user touches (idle drops below threshold).
    bool         screensaver_pushed = false;
};

static State s_state;

static uint8_t compute_target() {
    const auto& cfg = *s_state.cfg;

    const uint16_t timeout_s = cfg.inactivity_timeout_s;
    if (timeout_s == 0) {
        // 0 = "never dim" — always at user-active level.
        return cfg.brightness_active_pct;
    }

    const uint64_t idle_ms = aqua::activity::idle_ms();
    // UINT64_MAX means no touch has been recorded yet; treat the device as
    // idle since boot so the dim timeout still fires on unattended restarts.
    const uint64_t effective_idle = (idle_ms == UINT64_MAX)
        ? (uint64_t)esp_timer_get_time() / 1000ULL
        : idle_ms;
    if (effective_idle / 1000 >= timeout_s) return cfg.brightness_dim_pct;
    return cfg.brightness_active_pct;
}

static void apply_if_changed(uint8_t target) {
    if (target == s_state.last_applied_pct) return;
    s_state.last_applied_pct = target;
    aqua::display::backlight_set_percent(target);
    AC_LOGI(TAG, "backlight -> %u%%", (unsigned)target);
}

static void task_main(void*) {
    AC_LOGI(TAG, "dim manager started (timeout=%us, active=%u%%, dim=%u%%)",
            (unsigned)s_state.cfg->inactivity_timeout_s,
            (unsigned)s_state.cfg->brightness_active_pct,
            (unsigned)s_state.cfg->brightness_dim_pct);
    for (;;) {
        apply_if_changed(compute_target());

        // ── Feature A: auto-return to dashboard at inactivity_timeout ────
        // Returns to the dashboard (depth 1) from any sub-screen the moment
        // the dim threshold fires — no extra 30-second lag.
        // ── Feature B: screensaver clock ─────────────────────────────────
        // 1 second after the dim fires (giving pop_to_root time to settle),
        // push the 7-segment screensaver if the user enabled it.
        const uint16_t timeout_s = s_state.cfg->inactivity_timeout_s;
        if (timeout_s > 0) {
            const uint64_t idle_ms = aqua::activity::idle_ms();
            const uint64_t effective_idle = (idle_ms == UINT64_MAX)
                ? (uint64_t)esp_timer_get_time() / 1000ULL
                : idle_ms;
            const uint64_t dim_ms   = (uint64_t)timeout_s * 1000ULL;
            const uint64_t saver_ms = dim_ms + 1000ULL;

            if (effective_idle < dim_ms) {
                // User was active recently — clear screensaver flag so it
                // can re-trigger on the next idle cycle.
                s_state.screensaver_pushed = false;
            } else if (!s_state.screensaver_pushed) {
                // Feature A: pop any sub-screen back to dashboard.
                if (aqua::ui::screen_manager::depth() > 1) {
                    aqua::ui::screen_manager::pop_to_root(
                        aqua::ui::screen_manager::Transition::NONE);
                }

                // Feature B: push screensaver 1 s after dim fires, but
                // only once the stack is at root depth.
                if (s_state.cfg->screensaver_enabled &&
                    effective_idle >= saver_ms &&
                    aqua::ui::screen_manager::depth() == 1) {
                    aqua::ui::screensaver_screen::schedule_push();
                    s_state.screensaver_pushed = true;
                }
            }
        }

        // Use task notification so poke() can wake us early.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }
}

}  // namespace

void start(const aqua::storage::SystemConfig* cfg) {
    if (s_state.task != nullptr) return;  // idempotent
    s_state.cfg = cfg;
    BaseType_t ok = xTaskCreatePinnedToCore(
        task_main, "dim_mgr", 4096, nullptr, 1, &s_state.task, /*core=*/0);
    if (ok != pdPASS) {
        AC_LOGE(TAG, "failed to create dim_mgr task");
        s_state.task = nullptr;
    }
}

void poke() {
    if (s_state.task) xTaskNotifyGive(s_state.task);
}

}  // namespace aqua::ui::dim
