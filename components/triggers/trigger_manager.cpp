#include "trigger_manager.h"

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
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    for (auto& t : triggers_) {
        if (enabled_only && !t->enabled) continue;
        fn(*t);
    }
    xSemaphoreGiveRecursive(mutex_);
}

void TriggerManager::evaluate_all(
    const std::function<void(uint8_t, bool, uint8_t)>& out_active) {
    // Use direct-index stack arrays instead of std::unordered_map to avoid
    // heap allocation on every scheduler tick (H2/M4).
    // Device IDs 1..255 are valid; index 0 is unused.
    constexpr int kMaxDevId = 256;
    bool     per_device[kMaxDevId] = {};   // true = at least one active trigger links this device
    uint8_t  driver_id[kMaxDevId]  = {};   // first active trigger's id per device
    bool     any_linked[kMaxDevId] = {};   // true = any trigger (active or not) links this device

    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    for (auto& t : triggers_) {
        bool now = t->evaluate();
        // M1: atomic write — Core 0 writes, Core 1 (dashboard) reads.
        t->last_state_.store(now, std::memory_order_relaxed);
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
    for (int did = 1; did < kMaxDevId; ++did) {
        if (any_linked[did]) {
            out_active((uint8_t)did, per_device[did],
                       per_device[did] ? driver_id[did] : 0);
        }
    }
}

}  // namespace aqua::triggers
