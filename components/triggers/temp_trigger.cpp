#include "temp_trigger.h"

namespace aqua::triggers {

bool TempTrigger::evaluate() {
    if (!enabled || !sensor_present_) return false;

    // Hysteresis: if currently active stay active until the value crosses
    // back past (threshold - hysteresis) for ABOVE or (threshold + hyst.)
    // for BELOW.
    const float on_thr  = threshold_c;
    const float off_thr = (condition == TempCondition::ABOVE)
                          ? (threshold_c - hysteresis_c)
                          : (threshold_c + hysteresis_c);

    if (condition == TempCondition::ABOVE) {
        if (last_state_) return last_temp_c_ >= off_thr;
        return last_temp_c_ >= on_thr;
    }
    // BELOW
    if (last_state_) return last_temp_c_ <= off_thr;
    return last_temp_c_ <= on_thr;
}

}  // namespace aqua::triggers
