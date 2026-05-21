// AquaControl — trigger engine base types (Phase 3).
//
// A trigger is a stateless predicate. Scheduler calls evaluate() each cycle;
// if any trigger linked to a device returns true, the device is activated
// (OR logic across multiple triggers per device).
//
// Each trigger may be linked to multiple devices via linked_device_ids.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace aqua::triggers {

enum class TriggerType : uint8_t {
    SCHEDULE = 0,
    SOLAR    = 1,
    TEMP     = 2,
};

class ITrigger {
public:
    ITrigger(uint8_t id, std::string name) : id(id), name(std::move(name)) {}
    virtual ~ITrigger() = default;

    ITrigger(const ITrigger&) = delete;
    ITrigger& operator=(const ITrigger&) = delete;

    uint8_t              id;
    std::string          name;
    bool                 enabled = true;
    std::vector<uint8_t> linked_device_ids;

    // Returns true if the trigger is currently "active" (devices linked to
    // it should be ON unless another rule overrides). Implementations must
    // not block.
    virtual bool evaluate() = 0;

    virtual TriggerType get_type() const = 0;

    // Tracks whether evaluate() returned true on the previous fast cycle,
    // used by the scheduler to detect edges for logging/MQTT.
    // Atomic because Core 0 (scheduler) writes, Core 1 (dashboard) reads.
    std::atomic<bool> last_state_{false};
};

}  // namespace aqua::triggers
