// AquaControl — Dashboard screen (Phase 4 / Screen 2).
//
// Top-level home screen. Builds an LVGL screen object containing:
//   - Header bar: title, datetime, network/MQTT indicators, settings gear.
//   - Sensor row: ambient / water / solar cards.
//   - Device grid: one card per IDevice (state, level/colour, active trigger).
//   - Fault banner overlay (shown when faults::active_count() > 0).
//
// A 1 Hz LVGL timer refreshes labels and card states from the latest cached
// backend data (sensors, time_mgr, faults, device states). The dashboard
// does not write to any backend — manual override is delegated to the
// override_dialog (TODO Phase 4 slice 2).
#pragma once

#include "lvgl.h"

namespace aqua::ui::dashboard {

// Build the dashboard screen object. Caller is responsible for loading it
// via screen_manager::replace() or push(). The screen owns its refresh
// timer which is deleted automatically with the screen.
lv_obj_t* build();

// M3: Called from the scheduler (Core 0) whenever any device's active state
// changes. Thread-safe: uses lv_async_call() to post a status-pill refresh
// to the LVGL task (Core 1). No-op if the dashboard is not the active screen.
void notify_device_changed();

}  // namespace aqua::ui::dashboard
