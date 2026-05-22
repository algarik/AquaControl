// AquaControl — Scheduler task (Phase 3).
//
// A single FreeRTOS task evaluates triggers and pushes the resulting
// desired-active state into the bound devices. Per the plan we use a
// dual-rate loop:
//   * fast tick (every 1 s) — re-evaluate triggers + apply changed states.
//     1 s granularity is required for cyclic schedule triggers whose ON
//     window may be as short as 1 second.  evaluate() is pure arithmetic
//     so calling it at 1 Hz adds negligible CPU load.
//   * full tick (every 30 s) — also re-apply current state to all devices
//     (idempotent safety net in case of missed I2C writes)
//
// Task pinned to Core 0, stack 4 kB, priority 3.
#pragma once

#include <functional>

#include "device_manager.h"
#include "trigger_manager.h"

namespace aqua::scheduler {

struct Config {
    aqua::devices::DeviceManager*    dm = nullptr;
    aqua::triggers::TriggerManager*  tm = nullptr;
    uint32_t fast_interval_ms = 1000;
    uint32_t full_interval_ms = 30000;

    // Optional hook invoked each tick BEFORE triggers are evaluated. Used
    // to push fresh sensor readings into TempTriggers (see main.cpp). The
    // scheduler does not take ownership; lifetime must outlive start().
    std::function<void(aqua::triggers::TriggerManager&)> pre_eval;

    // Optional hook invoked each tick AFTER the bool pipeline has applied
    // states to all devices. Used for the TEMP_MAP analog pass.
    std::function<void(aqua::triggers::TriggerManager&,
                       aqua::devices::DeviceManager&)> post_eval;

    // Optional hook invoked from Core 0 (scheduler task) immediately after
    // a device's active state changes. Thread-safe wrt LVGL: call
    // lv_async_call() inside this callback to post UI updates to Core 1.
    std::function<void()> on_device_changed;

    // Heater safety monitoring. Set heater_device_id != 0 to enable.
    // Fault raised if heater is ON for heater_fault_timeout_s without a
    // measurable water temperature rise (sensor stuck / no heat transfer).
    uint8_t  heater_device_id       = 0;      // 0 = disabled
    uint16_t heater_fault_timeout_s = 1800;   // default 30 min
    float    heater_max_temp_c      = 0.0f;   // 0 = disabled; hard ceiling — force OFF above this °C
};

bool start(const Config& cfg);
bool is_running();

// Force an immediate evaluation (used after manual config changes).
void wake_now();

}  // namespace aqua::scheduler
