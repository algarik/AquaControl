#include "trigger_manager.h"

#include "ac_logger.h"

static const char* TAG = "TriggerManager";

namespace aqua::triggers {

TriggerManager::TriggerManager() {
    mutex_ = xSemaphoreCreateRecursiveMutex();
}

TriggerManager::~TriggerManager() {
    if (mutex_) vSemaphoreDelete(mutex_);
}

ITrigger* TriggerManager::add(std::unique_ptr<ITrigger> trig) {
    ITrigger* raw = trig.get();
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    triggers_.push_back(std::move(trig));
    xSemaphoreGiveRecursive(mutex_);
    return raw;
}

ITrigger* TriggerManager::find(uint8_t id) const {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    for (auto& t : triggers_) {
        if (t->id == id) {
            xSemaphoreGiveRecursive(mutex_);
            return t.get();
        }
    }
    xSemaphoreGiveRecursive(mutex_);
    return nullptr;
}

bool TriggerManager::remove(uint8_t id) {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    bool removed = false;
    for (auto it = triggers_.begin(); it != triggers_.end(); ++it) {
        if ((*it)->id == id) {
            triggers_.erase(it);
            removed = true;
            break;
        }
    }
    xSemaphoreGiveRecursive(mutex_);
    return removed;
}

void TriggerManager::for_each(const std::function<void(ITrigger&)>& fn,
                               bool enabled_only) {
    // A-4: timed wait on scheduler hot path.
    if (xSemaphoreTakeRecursive(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
        AC_LOGE(TAG, "mutex timeout in for_each — skipping pass");
        return;
    }
    for (auto& t : triggers_) {
        if (enabled_only && !t->enabled) continue;
        fn(*t);
    }
    xSemaphoreGiveRecursive(mutex_);
}

void TriggerManager::evaluate_all(
    const std::function<void(uint8_t, bool, uint8_t)>& out_active,
    const std::function<void(uint8_t)>& out_analog_only) {
    // Use direct-index stack arrays instead of std::unordered_map to avoid
    // heap allocation on every scheduler tick (H2/M4).
    // Device IDs 1..255 are valid; index 0 is unused.
    constexpr int kMaxDevId = 256;
    bool     per_device[kMaxDevId]    = {};   // true = at least one active trigger links this device
    uint8_t  driver_id[kMaxDevId]     = {};   // first active trigger's id per device
    bool     any_linked[kMaxDevId]    = {};   // true = any bool-pipeline trigger links this device
    bool     analog_linked[kMaxDevId] = {};   // true = any TEMP_MAP trigger links this device

    // A-4: timed wait on scheduler hot path.
    if (xSemaphoreTakeRecursive(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
        AC_LOGE(TAG, "mutex timeout in evaluate_all — skipping pass");
        return;
    }
    for (auto& t : triggers_) {
        bool now = t->evaluate();
        // M1: atomic write — Core 0 writes, Core 1 (dashboard) reads.
        t->last_state_.store(now, std::memory_order_relaxed);
        // TEMP_MAP triggers opt out of the bool pipeline entirely; they are
        // handled by the post_eval analog pass in the scheduler.
        if (t->get_type() == TriggerType::TEMP_MAP) {
            for (uint8_t did : t->linked_device_ids) analog_linked[did] = true;
            continue;
        }
        for (uint8_t did : t->linked_device_ids) {
            any_linked[did] = true;
            if (now && !per_device[did]) {
                per_device[did] = true;
                driver_id[did]  = t->id;
            }
        }
    }
    xSemaphoreGiveRecursive(mutex_);

    // Emit one row per device that appears in any trigger's link list.
    // TEMP_MAP (analog_linked) takes absolute priority: a device linked to a
    // TEMP_MAP trigger must never be driven by the boolean pipeline, even if it
    // also appears in a boolean trigger's linked_device_ids. The analog output
    // already handles the "disabled / 0%" case via eval_level() = 0.
    for (int did = 1; did < kMaxDevId; ++did) {
        if (analog_linked[did]) {
            // Analog path — TEMP_MAP owns this device exclusively.
            if (out_analog_only) out_analog_only((uint8_t)did);
        } else if (any_linked[did]) {
            out_active((uint8_t)did, per_device[did],
                       per_device[did] ? driver_id[did] : 0);
        }
    }
}

}  // namespace aqua::triggers
