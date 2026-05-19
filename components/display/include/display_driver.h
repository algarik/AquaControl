// AquaControl — Display driver
// Initializes the 800×480 RGB parallel panel (ILI6122 + ILI5960)
// using ESP-IDF's built-in esp_lcd_new_rgb_panel().
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

namespace aqua::display {

// One-time initialization. Returns the handle that LVGL port will use.
esp_err_t init();

// Returns the panel handle (nullptr until init() succeeds).
esp_lcd_panel_handle_t panel();

}  // namespace aqua::display
