// AquaControl - Placeholder "Coming soon" screen for menu categories
// that haven't been implemented yet. Keeps navigation functional end-to-end.
#pragma once

#include "lvgl.h"

namespace aqua::ui::placeholder_screen {

// Build a placeholder screen with the given title and a friendly message.
lv_obj_t* build(const char* title, const char* message);

}  // namespace aqua::ui::placeholder_screen
