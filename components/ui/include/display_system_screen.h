// AquaControl — Display & System settings screen.
#pragma once

#include "lvgl.h"

namespace aqua::ui::display_system_screen {

// Build and return a fresh screen root. Caller pushes it onto the
// screen_manager stack.
lv_obj_t* build();

}  // namespace aqua::ui::display_system_screen
