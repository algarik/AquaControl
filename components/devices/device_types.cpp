#include "device_types.h"

#include <ctime>

namespace aqua::devices {

void IDevice::set_override(OverrideMode mode, bool target_active, time_t until_epoch_utc) {
    portENTER_CRITICAL(&override_mux_);
    override_mode_        = mode;
    override_target_      = target_active;
    override_until_epoch_ = until_epoch_utc;
    portEXIT_CRITICAL(&override_mux_);
}

void IDevice::clear_override() {
    portENTER_CRITICAL(&override_mux_);
    override_mode_        = OverrideMode::NONE;
    override_target_      = false;
    override_until_epoch_ = 0;
    portEXIT_CRITICAL(&override_mux_);
}

bool IDevice::resolve_active(bool desired_active) {
    // Read all three override fields atomically under the spinlock, then
    // release before making time() calls or calling clear_override()
    // (which re-acquires the same spinlock — portMUX is NOT reentrant).
    portENTER_CRITICAL(&override_mux_);
    OverrideMode mode      = override_mode_;
    bool         target    = override_target_;
    time_t       until_ep  = override_until_epoch_;
    portEXIT_CRITICAL(&override_mux_);

    if (mode == OverrideMode::NONE) {
        return desired_active;
    }

    // TIMED: clear once the deadline has passed.
    if (mode == OverrideMode::TIMED) {
        time_t now_utc = time(nullptr);
        if (now_utc >= until_ep) {
            clear_override();
            return desired_active;
        }
    }

    // UNTIL_NEXT: clear the first time the scheduler would naturally produce
    // a state different from the overridden target. (i.e. trigger logic
    // flipped the value.)
    if (mode == OverrideMode::UNTIL_NEXT) {
        if (desired_active != target) {
            clear_override();
            return desired_active;
        }
    }

    return target;
}

}  // namespace aqua::devices
