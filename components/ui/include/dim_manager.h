// AquaControl — Inactivity dim & fault-override manager (Phase 4).
//
// Single Core-0 task that ticks every second and decides the backlight
// brightness based on:
//   - active fault count (force 100% so the user sees the alert)
//   - touch idle time vs SystemConfig.inactivity_timeout_s
// Brightness setpoint comes from SystemConfig.brightness_active_pct /
// brightness_dim_pct. The task only writes to the backlight when the
// computed target differs from the previous setpoint, avoiding redundant
// I2C/LEDC writes.
#pragma once

namespace aqua::storage { struct SystemConfig; }

namespace aqua::ui::dim {

// Start the dim manager task. `cfg` pointer must remain valid for the
// program lifetime — the task re-reads it every tick so changes made by
// the settings UI take effect on the next cycle.
void start(const aqua::storage::SystemConfig* cfg);

// Force an immediate brightness recompute (e.g. after the user changes
// brightness settings).
void poke();

}  // namespace aqua::ui::dim
