// AquaControl — LEDC PWM backlight control
#pragma once

#include <cstdint>
#include "esp_err.h"

namespace aqua::display {

// Initialize the LEDC timer and channel. Backlight is left at 0% until set_percent().
esp_err_t backlight_init();

// Set brightness 0..100. Percent is clamped.
esp_err_t backlight_set_percent(uint8_t percent);

uint8_t backlight_get_percent();

}  // namespace aqua::display
