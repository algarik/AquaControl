// AquaControl — Application entry point
//
// Phase 1 boot sequence:
//   1. Init logger / banner
//   2. Init shared I2C master bus
//   3. Init RGB display panel
//   4. Init LEDC backlight, raise to ~80%
//   5. Init LVGL port (creates Core 1 task)
//   6. Init GT911 touch (registers with LVGL)
//   7. Show boot console screen, emit fake progress log
//   8. Idle
//
// Phases 2+ replace the fake log with real I2C scan/device init results.

#include <cstdio>

#include "ac_logger.h"
#include "app_config.h"
#include "backlight.h"
#include "boot_screen.h"
#include "display_driver.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "lvgl_port.h"
#include "touch_gt911.h"

static const char* TAG = "main";

// Single global instance of the I2C bus, owned by app_main.
static aqua::i2c::I2CBus g_i2c_bus;

extern "C" void app_main(void) {
    AC_LOGI(TAG, "=== %s v%s starting ===", AC_FIRMWARE_NAME, AC_FIRMWARE_VERSION);
    AC_LOGI(TAG, "Free heap: %lu, free PSRAM: %lu",
            (unsigned long)esp_get_free_heap_size(),
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // 1. I2C bus (shared by touch + all external modules) — needed first
    //    because touch driver below uses it.
    ESP_ERROR_CHECK(g_i2c_bus.init(AC_I2C_PORT, AC_I2C_SDA_PIN, AC_I2C_SCL_PIN, AC_I2C_FREQ_HZ));

    // 2. Display panel
    ESP_ERROR_CHECK(aqua::display::init());

    // 3. Backlight on (after panel init, before LVGL attach)
    ESP_ERROR_CHECK(aqua::display::backlight_init());
    ESP_ERROR_CHECK(aqua::display::backlight_set_percent(80));

    // 4. LVGL port + attach RGB display
    ESP_ERROR_CHECK(aqua::display::lvgl_init());

    // 5. Touch (must run after LVGL port — registers an input device)
    esp_err_t terr = aqua::touch::init(g_i2c_bus);
    if (terr != ESP_OK) {
        AC_LOGW(TAG, "Touch init failed (will continue without touch): %s",
                esp_err_to_name(terr));
    }

    // 6. Boot console
    aqua::ui::boot_screen_show();
    aqua::ui::boot_screen_log("I2C bus initialized (SDA=19, SCL=20)", "OK");
    aqua::ui::boot_screen_log("Display: 800x480 RGB panel", "OK");
    aqua::ui::boot_screen_log("Backlight: LEDC PWM on GPIO 2", "OK");
    aqua::ui::boot_screen_log("LVGL v9 attached", "OK");
    aqua::ui::boot_screen_log(
        terr == ESP_OK ? "Touch: GT911 @ 0x14" : "Touch: GT911 not found",
        terr == ESP_OK ? "OK" : "FAIL");
    aqua::ui::boot_screen_log("Phase 1 stub: real device scan comes in Phase 2");
    aqua::ui::boot_screen_log("");
    aqua::ui::boot_screen_log("System ready (Phase 1).");

    AC_LOGI(TAG, "Boot complete. Free heap: %lu, PSRAM: %lu",
            (unsigned long)esp_get_free_heap_size(),
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Idle loop — periodically print heap stats
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        AC_LOGI(TAG, "alive  heap=%lu  psram=%lu",
                (unsigned long)esp_get_free_heap_size(),
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}
