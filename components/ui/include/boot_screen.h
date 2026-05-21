// AquaControl — Boot console screen
#pragma once

#include <cstdint>

namespace aqua::ui {

// Build and show the boot screen (thread-safe).
void boot_screen_show();

// Append a single line to the boot console (thread-safe).
// Marker is one of: "OK", "FAIL", "..." or nullptr for plain text.
void boot_screen_log(const char* line, const char* marker = nullptr);

// Called when boot is complete.  Waits for any active pause to be released,
// then holds the boot log visible for hold_ms milliseconds before returning.
// The caller then performs the screen transition to the dashboard.
void boot_screen_finish(uint32_t hold_ms = 2000);

}  // namespace aqua::ui
