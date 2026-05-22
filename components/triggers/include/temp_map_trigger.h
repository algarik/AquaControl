// AquaControl — TempMapTrigger: linear temperature → analog output mapping.
//
// Unlike boolean triggers, TempMapTrigger does not participate in the bool
// pipeline.  evaluate() always returns false so the device is excluded from
// the normal ON/OFF scheduling.  Instead the scheduler's post_eval hook
// calls eval_level() each tick and passes the result to IDevice::apply_analog().
//
// Level computation (forward, reverse=false):
//   t = (temp − temp_lo_c) / (temp_hi_c − temp_lo_c),  clamped to [0, 1]
//   If reverse=true the output is (1 − t): high temperature → low device value.
//   If the sensor is absent, output = 0 (safe default: lo-end output).
#pragma once

#include "trigger_types.h"

namespace aqua::triggers {

class TempMapTrigger : public ITrigger {
public:
    TempMapTrigger(uint8_t id, std::string name)
        : ITrigger(id, std::move(name)) {}

    // Which physical sensor to read.  0 = water, 1 = ambient.
    uint8_t sensor_id  = 0;

    // Dead-band around temp_lo_c to prevent rapid cycling on small fluctuations.
    // Output activates only when temp rises above (temp_lo_c + hysteresis_c);
    // deactivates when temp falls below temp_lo_c.
    float hysteresis_c = 0.5f;

    // Temperature range that maps to [0, 1] output (or [1, 0] when reverse=true).
    float temp_lo_c = 20.0f;   // temp at which output = 0 (or 1 when reversed)
    float temp_hi_c = 30.0f;   // temp at which output = 1 (or 0 when reversed)

    // When true the mapping is inverted: high temperature → low device value.
    // Useful for chiller / cooling fan scenarios.
    bool  reverse   = false;

    // Push the latest sensor reading before each evaluation tick.
    void update_temperature(float celsius, bool present);

    // Returns the current analogue level in [0.0, 1.0].
    // Returns 0.0 if the sensor is absent or temp_hi_c <= temp_lo_c.
    // When reverse=true returns (1 − t), mapping high temperature to low output.
    float eval_level() const;

    // Always returns false — this trigger bypasses the bool pipeline.
    bool evaluate() override { return false; }

    TriggerType get_type() const override { return TriggerType::TEMP_MAP; }

private:
    float         last_temp_c_    = 0.0f;
    bool          sensor_present_ = false;
    mutable bool  active_         = false;  // hysteresis state
};

}  // namespace aqua::triggers
