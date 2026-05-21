// AquaControl — TriggerValidator implementation (§6.4.1 cross-checks).
#include "trigger_validator.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "ac_logger.h"
#include "device_types.h"
#include "pwm_device.h"
#include "rgb_device.h"
#include "schedule_trigger.h"
#include "solar_trigger.h"
#include "temp_trigger.h"

namespace aqua::triggers {

using aqua::devices::DeviceManager;
using aqua::devices::DeviceType;
using aqua::devices::IDevice;
using aqua::devices::PwmDevice;
using aqua::devices::RgbDevice;

static const char* TAG = "trig_val";

namespace {

bool windows_overlap_same_day(const ScheduleTrigger& a, const ScheduleTrigger& b) {
    bool any_shared = false;
    for (int d = 0; d < 7; ++d) {
        if (a.days[d] && b.days[d]) { any_shared = true; break; }
    }
    if (!any_shared) return false;

    // Treat wrap-around windows (stop <= start) as two segments.
    auto expand = [](const ScheduleTrigger& t, std::pair<int,int>& s1,
                     std::pair<int,int>& s2, bool& two) {
        if (t.stop_min > t.start_min) {
            s1 = {t.start_min, t.stop_min};
            two = false;
        } else {
            s1 = {t.start_min, 1440};
            s2 = {0, t.stop_min};
            two = true;
        }
    };

    std::pair<int,int> a1{}, a2{}, b1{}, b2{};
    bool a_two = false, b_two = false;
    expand(a, a1, a2, a_two);
    expand(b, b1, b2, b_two);

    auto overlap = [](std::pair<int,int> x, std::pair<int,int> y) {
        return x.first < y.second && y.first < x.second;
    };
    if (overlap(a1, b1)) return true;
    if (a_two && overlap(a2, b1)) return true;
    if (b_two && overlap(a1, b2)) return true;
    if (a_two && b_two && overlap(a2, b2)) return true;
    return false;
}

}  // namespace

std::vector<ValidationWarning> TriggerValidator::validate_all(
    DeviceManager&    dm,
    TriggerManager&   tm,
    const aqua::storage::SystemConfig& cfg,
    bool sensor_water_present,
    bool sensor_ambient_present,
    bool has_rtc,
    bool has_wifi) {

    std::vector<ValidationWarning> out;

    // Build lookup: device_id -> list of trigger ids linking to it.
    std::unordered_map<uint8_t, std::vector<uint8_t>> dev_to_trigs;
    tm.for_each([&](ITrigger& t) {
        for (uint8_t did : t.linked_device_ids) {
            dev_to_trigs[did].push_back(t.id);
        }
    });

    // --- Check #1: empty trigger ---
    tm.for_each([&](ITrigger& t) {
        if (t.linked_device_ids.empty()) {
            out.push_back({t.id, 0, 1, WarningSeverity::WARN,
                "This trigger controls no devices and has no effect"});
        }
    });

    // --- Check #2: idle device ---
    for (const auto& up : dm.all()) {
        IDevice* d = up.get();
        if (!d->enabled) continue;
        if (dev_to_trigs.find(d->id) == dev_to_trigs.end()) {
            out.push_back({0, d->id, 2, WarningSeverity::INFO,
                "\"" + d->name + "\" has no triggers \u2014 will not activate automatically"});
        }
    }

    // --- Check #3: schedule overlap on same device ---
    for (const auto& kv : dev_to_trigs) {
        const auto& tids = kv.second;
        std::vector<ScheduleTrigger*> schedules;
        for (uint8_t tid : tids) {
            ITrigger* t = tm.find(tid);
            if (t && t->get_type() == TriggerType::SCHEDULE) {
                schedules.push_back(static_cast<ScheduleTrigger*>(t));
            }
        }
        for (size_t i = 0; i < schedules.size(); ++i) {
            for (size_t j = i + 1; j < schedules.size(); ++j) {
                if (windows_overlap_same_day(*schedules[i], *schedules[j])) {
                    out.push_back({schedules[i]->id, kv.first, 3,
                        WarningSeverity::INFO,
                        "Triggers overlap — device stays ON during overlap (OR logic)"});
                    out.push_back({schedules[j]->id, kv.first, 3,
                        WarningSeverity::INFO,
                        "Triggers overlap — device stays ON during overlap (OR logic)"});
                }
            }
        }
    }

    bool location_set = !(cfg.latitude == 0.0f && cfg.longitude == 0.0f);

    // --- Checks #4, #5 on each solar trigger ---
    tm.for_each([&](ITrigger& t) {
        if (t.get_type() != TriggerType::SOLAR) return;
        auto& s = static_cast<SolarTrigger&>(t);
        if (!location_set) {
            out.push_back({s.id, 0, 4, WarningSeverity::ERROR_,
                "Location required for solar triggers — configure in Time & Location settings"});
        }
        // #5: window extends past midnight when expressed as minutes-of-day.
        int16_t base = (s.event == SolarEvent::SUNRISE)
                        ? s.sunrise_min_today : s.sunset_min_today;
        if (s.valid && base >= 0) {
            int start_min = (int)base + (int)s.offset_min;
            if (start_min < 0) {
                out.push_back({s.id, 0, 5, WarningSeverity::WARN,
                    "Start offset pushes trigger before midnight — trigger will not activate"});
            } else if (start_min >= 1440) {
                out.push_back({s.id, 0, 5, WarningSeverity::WARN,
                    "Start offset pushes trigger past midnight — trigger will not activate today"});
            } else if (!s.use_end_event) {
                // Only check duration overflow in duration mode; end-event mode
                // handles midnight wrap correctly in evaluate().
                int end_min = start_min + (int)s.duration_min;
                if (end_min >= 1440) {
                    out.push_back({s.id, 0, 5, WarningSeverity::INFO,
                        "Duration extends past midnight; device stays on until 23:59"});
                }
            }
        }
    });

    // Build per-sensor list of TempTriggers for checks #6/#7/#8.
    // sensor_id 0 = water, 1 = ambient.
    std::unordered_map<uint8_t, std::vector<TempTrigger*>> by_sensor;

    // --- Check #6 ---
    tm.for_each([&](ITrigger& t) {
        if (t.get_type() != TriggerType::TEMP) return;
        auto& tt = static_cast<TempTrigger&>(t);
        bool present = (tt.sensor_id == 0) ? sensor_water_present : sensor_ambient_present;
        if (!present) {
            out.push_back({tt.id, 0, 6, WarningSeverity::WARN,
                "Sensor not detected — trigger is suspended until sensor is found"});
        }
        by_sensor[tt.sensor_id].push_back(&tt);
    });

    // --- Checks #7, #8 — per sensor, per linked device ---
    for (auto& kv : by_sensor) {
        auto& list = kv.second;
        // Group by linked device id (any linked device that appears in both
        // an ABOVE and a BELOW trigger).
        std::unordered_map<uint8_t, std::vector<TempTrigger*>> by_dev;
        for (TempTrigger* tt : list) {
            for (uint8_t did : tt->linked_device_ids) by_dev[did].push_back(tt);
        }
        for (auto& dkv : by_dev) {
            float min_above = INFINITY;
            float max_below = -INFINITY;
            TempTrigger* above_t = nullptr;
            TempTrigger* below_t = nullptr;
            for (TempTrigger* tt : dkv.second) {
                if (tt->condition == TempCondition::ABOVE) {
                    if (tt->threshold_c < min_above) { min_above = tt->threshold_c; above_t = tt; }
                } else {
                    if (tt->threshold_c > max_below) { max_below = tt->threshold_c; below_t = tt; }
                }
            }
            if (above_t && below_t) {
                if (min_above <= max_below) {
                    out.push_back({above_t->id, dkv.first, 7, WarningSeverity::WARN,
                        "Thresholds overlap — device may always be active or never active"});
                } else {
                    float gap  = min_above - max_below;
                    float hyst = std::max(above_t->hysteresis_c, below_t->hysteresis_c);
                    if (hyst >= gap) {
                        out.push_back({above_t->id, dkv.first, 8, WarningSeverity::WARN,
                            "Hysteresis larger than threshold gap; device may never switch off"});
                    }
                }
            }
        }
    }

    // --- Check #9: no time source ---
    if (!has_rtc && !has_wifi) {
        bool any_time_based = false;
        tm.for_each([&](ITrigger& t) {
            if (t.get_type() == TriggerType::SCHEDULE ||
                t.get_type() == TriggerType::SOLAR) {
                any_time_based = true;
            }
        });
        if (any_time_based) {
            out.push_back({0, 0, 9, WarningSeverity::ERROR_,
                "No reliable time source — trigger accuracy not guaranteed"});
        }
    }

    // --- Check #10: fade longer than schedule window ---
    for (const auto& kv : dev_to_trigs) {
        IDevice* d = dm.find(kv.first);
        if (!d) continue;
        uint16_t fade = 0;
        if (d->get_type() == DeviceType::PWM) {
            auto* p = static_cast<PwmDevice*>(d);
            fade = std::max(p->fade_in_min, p->fade_out_min);
        } else if (d->get_type() == DeviceType::RGB) {
            auto* g = static_cast<RgbDevice*>(d);
            fade = std::max(g->fade_in_min, g->fade_out_min);
        }
        if (fade == 0) continue;
        for (uint8_t tid : kv.second) {
            ITrigger* t = tm.find(tid);
            if (!t || t->get_type() != TriggerType::SCHEDULE) continue;
            auto* s = static_cast<ScheduleTrigger*>(t);
            int window = (s->stop_min > s->start_min)
                            ? (int)(s->stop_min - s->start_min)
                            : (int)(1440 - s->start_min + s->stop_min);
            if (window > 0 && fade > window) {
                out.push_back({s->id, d->id, 10, WarningSeverity::INFO,
                    "Fade duration exceeds active window — device may never reach full brightness"});
            }
        }
    }

    AC_LOGI(TAG, "validate_all: %u warnings", (unsigned)out.size());
    return out;
}

}  // namespace aqua::triggers
