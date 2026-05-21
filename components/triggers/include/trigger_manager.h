// AquaControl — TriggerManager (Phase 3).
//
// Owns all ITrigger instances and produces a per-device desired_active
// boolean by OR-ing every trigger that lists the device id.
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "trigger_types.h"

namespace aqua::triggers {

class TriggerManager {
public:
    TriggerManager();
    ~TriggerManager();

    ITrigger* add(std::unique_ptr<ITrigger> trig);
    ITrigger* find(uint8_t id) const;

    // Remove (and destroy) the trigger with the given id. Returns true if a
    // trigger was removed. Safe to call from UI thread; serialized via the
    // internal mutex so it cannot race with the scheduler's evaluate_all().
    bool remove(uint8_t id);

    // Iterate all triggers. Pass `enabled_only=true` to skip disabled triggers
    // (useful for UI display of active count). Default iterates all.
    void for_each(const std::function<void(ITrigger&)>& fn,
                  bool enabled_only = false);

    // Re-evaluate all triggers, capture each one's new state into last_state_,
    // then for every referenced device id emit (device_id, active, driver_id).
    // `driver_id` is the id of the first active trigger that listed the
    // device (0 if `active == false`).
    void evaluate_all(
        const std::function<void(uint8_t did, bool active, uint8_t driver_id)>& out_active);

    const std::vector<std::unique_ptr<ITrigger>>& all() const { return triggers_; }
    size_t size() const { return triggers_.size(); }

private:
    std::vector<std::unique_ptr<ITrigger>> triggers_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace aqua::triggers
