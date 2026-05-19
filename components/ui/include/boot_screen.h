// AquaControl — Boot console screen
// Phase 1: shows a fake Linux-style boot log so we can validate the
// display + LVGL stack. Phase 2 will hook real I2C scan results in.
#pragma once

namespace aqua::ui {

// Build and show the boot screen. Must be called from inside lvgl_port_lock(),
// OR from the LVGL task itself. We provide a thread-safe wrapper.
void boot_screen_show();

// Append a single line to the boot console (thread-safe).
// Marker is one of: "OK", "FAIL", "..." or nullptr for plain text.
void boot_screen_log(const char* line, const char* marker = nullptr);

}  // namespace aqua::ui
