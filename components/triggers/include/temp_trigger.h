// AquaControl — TempTrigger (Phase 3).
//
// Active when the bound sensor's latest reading satisfies the threshold/
// condition with hysteresis. The scheduler pushes the latest temperature
// into the trigger each cycle via update_temperature().
#pragma once

#include <cstdint>

#include "trigger_types.h"

namespace aqua::triggers {

enum class TempCondition : uint8_t { ABOVE = 0, BELOW = 1 };

class TempTrigger : public ITrigger {
public:
    TempTrigger(uint8_t id, std::string name)
        : ITrigger(id, std::move(name)) {}

    uint8_t       sensor_id     = 0;     // 0 = water, 1 = ambient
    float         threshold_c   = 25.0f;
    TempCondition condition     = TempCondition::ABOVE;
    float         hysteresis_c  = 0.5f;

    // Pushed in by the scheduler. If `sensor_present == false` evaluate()
    // returns false (and trigger_validator will warn).
    void update_temperature(float celsius, bool sensor_present) {
        last_temp_c_    = celsius;
        sensor_present_ = sensor_present;
    }

    bool evaluate() override;
    TriggerType get_type() const override { return TriggerType::TEMP; }

private:
    float last_temp_c_   = 0.0f;
    bool  sensor_present_ = false;
};

}  // namespace aqua::triggers
