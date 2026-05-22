// AquaControl — Screensaver screen (big 7-segment clock).
//
// Activated by dim_manager when inactivity_timeout fires and
// screensaver_enabled is true in SystemConfig.
//
// The screen renders a large HH:MM 7-segment style clock using LVGL's
// custom draw API.  Touch anywhere dismisses it and returns to whichever
// screen was active below it on the stack.
#pragma once

#include "lvgl.h"

namespace aqua::ui::screensaver_screen {

// Build and return the screensaver root lv_obj_t.
// Must be called from the LVGL task (Core 1) or from an lv_async_call
// callback — i.e. in any context where LVGL widget creation is safe.
lv_obj_t* build();

// Schedule a screensaver push via lv_async_call.
// Safe to call from Core 0 (dim_manager task).
// When the async callback fires on Core 1 it calls build() + push().
void schedule_push();

}  // namespace aqua::ui::screensaver_screen
