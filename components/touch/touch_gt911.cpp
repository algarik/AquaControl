#include "touch_gt911.h"

#include "ac_logger.h"
#include "app_config.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "lvgl_port.h"

namespace aqua::touch {

static const char* TAG = "TouchGT911";

esp_err_t init(aqua::i2c::I2CBus& bus) {
    if (!bus.initialized()) {
        AC_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Create the I2C panel-IO handle wrapping the shared bus.
    //    NOTE: do not use ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() macro — it uses
    //    designated initializers in an order that conflicts with the struct
    //    declaration in IDF 5.5+, which is an error in C++.
    esp_lcd_panel_io_handle_t io_handle = nullptr;
    esp_lcd_panel_io_i2c_config_t io_cfg = {};
    io_cfg.dev_addr = AC_ADDR_GT911;
    io_cfg.control_phase_bytes = 1;
    io_cfg.dc_bit_offset = 0;
    io_cfg.lcd_cmd_bits = 16;
    io_cfg.lcd_param_bits = 0;
    io_cfg.flags.disable_control_phase = 1;
    io_cfg.scl_speed_hz = AC_I2C_FREQ_HZ;

    esp_err_t err = esp_lcd_new_panel_io_i2c(bus.handle(), &io_cfg, &io_handle);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "panel_io_i2c failed: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Create touch handle (polling; INT/RST not connected)
    esp_lcd_touch_handle_t touch_handle = nullptr;
    esp_lcd_touch_config_t tcfg = {};
    tcfg.x_max = AC_LCD_H_RES;
    tcfg.y_max = AC_LCD_V_RES;
    tcfg.rst_gpio_num = GPIO_NUM_NC;
    tcfg.int_gpio_num = GPIO_NUM_NC;
    tcfg.levels.reset = 0;
    tcfg.levels.interrupt = 0;
    tcfg.flags.swap_xy = 0;
    tcfg.flags.mirror_x = 0;
    tcfg.flags.mirror_y = 0;

    err = esp_lcd_touch_new_i2c_gt911(io_handle, &tcfg, &touch_handle);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "esp_lcd_touch_new_i2c_gt911 failed: %s", esp_err_to_name(err));
        return err;
    }

    // 3. Register with LVGL via esp_lvgl_port
    const lvgl_port_touch_cfg_t lvgl_touch_cfg = {
        .disp = aqua::display::lvgl_display(),
        .handle = touch_handle,
    };
    if (lvgl_port_add_touch(&lvgl_touch_cfg) == nullptr) {
        AC_LOGE(TAG, "lvgl_port_add_touch failed");
        return ESP_FAIL;
    }

    AC_LOGI(TAG, "GT911 initialized at 0x%02X (polling mode)", AC_ADDR_GT911);
    return ESP_OK;
}

}  // namespace aqua::touch
