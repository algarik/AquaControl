// AquaControl — First-run setup wizard (Phase 4, Slice 4).
//
// A multi-step wizard that runs the first time the device boots after
// a factory reset.  Steps:
//   0 — Welcome / language choice
//   1 — WiFi credentials
//   2 — Time-zone & NTP
//   3 — Done / finish
//
// The wizard is shown as a full-screen overlay pushed on top of the
// dashboard via screen_manager::push().  On completion it calls
// aqua::storage::save_system_config() with first_run_complete=true and
// pops itself.
#pragma once

#include "lvgl.h"

namespace aqua::ui::wizard {

// Build and return the first step of the wizard.
// Subsequent steps transition internally; the whole wizard is a single
// push onto the screen stack and pops once complete.
lv_obj_t* build();

}  // namespace aqua::ui::wizard
