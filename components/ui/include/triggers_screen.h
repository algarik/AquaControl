// AquaControl — Triggers settings screen (Phase 4, Slice 3).
//
// Three sub-screens accessible from the Settings menu:
//   1. trigger_list_screen   — lists all triggers with enable toggle
//                              and Edit/Delete/Add buttons.
//   2. trigger_edit_screen   — edit an existing trigger (all three types).
//   3. trigger_add_screen    — create a new trigger (type picker first).
//
// All three are declared here; only build() is public (returns the list).
#pragma once

#include <cstdint>

#include "lvgl.h"

namespace aqua::ui::triggers_screen {

// Build and return the trigger list screen root.
lv_obj_t* build();

}  // namespace aqua::ui::triggers_screen
