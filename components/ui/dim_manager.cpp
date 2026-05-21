// AquaControl — Inactivity dim & fault-override manager implementation.
#include "dim_manager.h"

#include "ac_logger.h"
#include "activity.h"
#include "backlight.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "screen_manager.h"
#include "system_config.h"

namespace aqua::ui::dim {

namespace {

constexpr const char* TAG = "DimMgr";

struct State {
    const aqua::storage::SystemConfig* cfg = nullptr;
    TaskHandle_t task = nullptr;
    uint8_t      last_applied_pct = 255;
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
    if (idle_ms == UINT64_MAX) {
        // No input recorded yet (boot). Keep at active level so the user can
        // see the dashboard come up.
        return cfg.brightness_active_pct;
    }
    if (idle_ms / 1000 >= timeout_s) return cfg.brightness_dim_pct;
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

        // D1: auto-return to dashboard 30 s after the dim threshold fires.
        // Only when the user is in a sub-screen (depth > 1) and a timeout
        // is configured (0 = never).
        const uint16_t timeout_s = s_state.cfg->inactivity_timeout_s;
        if (timeout_s > 0) {
            const uint64_t idle_ms = aqua::activity::idle_ms();
            const uint64_t nav_ms  = ((uint64_t)timeout_s + 30) * 1000;
            if (idle_ms != UINT64_MAX && idle_ms >= nav_ms) {
                if (aqua::ui::screen_manager::depth() > 1) {
                    aqua::ui::screen_manager::pop_to_root(
                        aqua::ui::screen_manager::Transition::NONE);
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
