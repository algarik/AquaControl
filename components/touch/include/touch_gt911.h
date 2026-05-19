// AquaControl — GT911 capacitive touch driver wrapper
// Uses espressif/esp_lcd_touch_gt911 + esp_lvgl_port for LVGL integration.
// INT pin is unconnected on this board → polling mode @ 16 ms.
#pragma once

#include "esp_err.h"
#include "i2c_bus.h"

namespace aqua::touch {

// Init touch on the shared I2C bus and register it as an LVGL input device.
esp_err_t init(aqua::i2c::I2CBus& bus);

}  // namespace aqua::touch
