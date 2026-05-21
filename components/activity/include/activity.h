// AquaControl — User input activity tracker (Phase 3.5 Block A4)
//
// Single global timestamp updated whenever the touch driver detects a press.
// Used by the inactivity-dim feature and the dashboard to know when the
// user last interacted with the screen. Header-only API; all state is
// atomic so any task can read/notify without locking.
#pragma once

#include <cstdint>

namespace aqua::activity {

// Call from the touch driver on every press event. Cheap; safe from ISR.
void notify();

// Epoch in ms (esp_timer based) of the last input event. 0 if no input yet.
uint64_t last_input_ms();

// Convenience: ms since last input. UINT64_MAX if never.
uint64_t idle_ms();

}  // namespace aqua::activity
