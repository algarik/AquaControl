// AquaControl — TempMapTrigger implementation.
#include "temp_map_trigger.h"

#include <algorithm>

namespace aqua::triggers {

void TempMapTrigger::update_temperature(float celsius, bool present) {
    last_temp_c_    = celsius;
    sensor_present_ = present;
}

float TempMapTrigger::eval_level() const {
    if (!sensor_present_ || temp_hi_c <= temp_lo_c) {
        active_ = false;
        return 0.0f;
    }

    // Hysteresis: activate only when temp rises above lo+hyst;
    // once active, stay active until temp drops below lo.
    if (!active_) {
        if (last_temp_c_ >= temp_lo_c + hysteresis_c)
            active_ = true;
    } else {
        if (last_temp_c_ < temp_lo_c)
            active_ = false;
    }

    if (!active_) return 0.0f;

    float t = (last_temp_c_ - temp_lo_c) / (temp_hi_c - temp_lo_c);
    t = std::clamp(t, 0.0f, 1.0f);
    return reverse ? (1.0f - t) : t;
}

}  // namespace aqua::triggers
