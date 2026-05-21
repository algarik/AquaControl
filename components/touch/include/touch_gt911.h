// AquaControl — GT911 capacitive touch driver wrapper
// Uses espressif/esp_lcd_touch_gt911 + esp_lvgl_port for LVGL integration.
// INT pin is unconnected on this board → polling mode @ 16 ms.
#pragma once

#include "esp_err.h"
#include "i2c_bus.h"

namespace aqua::touch {

// Init touch on the shared I2C bus and register it as an LVGL input device.
esp_err_t init(aqua::i2c::I2CBus& bus);

// 7-bit I2C address the GT911 was actually detected at (0x14 or 0x5D).
// Returns 0 if init() was not successful.
uint8_t detected_address();

}  // namespace aqua::touch
