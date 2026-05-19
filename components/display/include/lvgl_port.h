// AquaControl — LVGL port wrapper
// Wraps esp_lvgl_port: initializes the LVGL task (pinned to Core 1),
// attaches the RGB display, and exposes lvgl_port_lock/unlock for callers.
#pragma once

#include "esp_err.h"
#include "lvgl.h"

namespace aqua::display {

// Initialize LVGL + attach RGB display. Must be called after display::init().
esp_err_t lvgl_init();

// Active display handle (nullptr before lvgl_init()).
lv_display_t* lvgl_display();

}  // namespace aqua::display
