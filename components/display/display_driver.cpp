#include "display_driver.h"

#include <cstring>

#include "ac_logger.h"
#include "app_config.h"
#include "esp_lcd_panel_rgb.h"

namespace aqua::display {

static const char* TAG = "Display";

static esp_lcd_panel_handle_t s_panel = nullptr;

esp_err_t init() {
    if (s_panel != nullptr) {
        AC_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    AC_LOGI(TAG, "Init RGB panel %dx%d @ %d MHz",
            AC_LCD_H_RES, AC_LCD_V_RES, AC_LCD_PIXEL_CLOCK_HZ / 1000000);

    esp_lcd_rgb_panel_config_t panel_cfg = {};
    panel_cfg.data_width = 16;             // RGB565
    panel_cfg.bits_per_pixel = 16;
    panel_cfg.psram_trans_align = 64;
    panel_cfg.num_fbs = 1;                 // One full PSRAM frame buffer
    panel_cfg.bounce_buffer_size_px = AC_LCD_BOUNCE_PX;
    panel_cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_cfg.disp_gpio_num = GPIO_NUM_NC;
    panel_cfg.pclk_gpio_num = AC_PIN_LCD_PCLK;
    panel_cfg.hsync_gpio_num = AC_PIN_LCD_HSYNC;
    panel_cfg.vsync_gpio_num = AC_PIN_LCD_VSYNC;
    panel_cfg.de_gpio_num = AC_PIN_LCD_DE;

    const gpio_num_t data_pins[16] = AC_LCD_DATA_PINS_INIT;
    memcpy(panel_cfg.data_gpio_nums, data_pins, sizeof(data_pins));

    panel_cfg.timings.pclk_hz = AC_LCD_PIXEL_CLOCK_HZ;
    panel_cfg.timings.h_res = AC_LCD_H_RES;
    panel_cfg.timings.v_res = AC_LCD_V_RES;
    panel_cfg.timings.hsync_back_porch = AC_LCD_HSYNC_BACK_PORCH;
    panel_cfg.timings.hsync_front_porch = AC_LCD_HSYNC_FRONT_PORCH;
    panel_cfg.timings.hsync_pulse_width = AC_LCD_HSYNC_PULSE_WIDTH;
    panel_cfg.timings.vsync_back_porch = AC_LCD_VSYNC_BACK_PORCH;
    panel_cfg.timings.vsync_front_porch = AC_LCD_VSYNC_FRONT_PORCH;
    panel_cfg.timings.vsync_pulse_width = AC_LCD_VSYNC_PULSE_WIDTH;
    panel_cfg.timings.flags.hsync_idle_low = 0;
    panel_cfg.timings.flags.vsync_idle_low = 0;
    panel_cfg.timings.flags.de_idle_high = 0;
    panel_cfg.timings.flags.pclk_active_neg = 1;     // Validated by reference config
    panel_cfg.timings.flags.pclk_idle_high = 0;

    panel_cfg.flags.fb_in_psram = true;              // Frame buffer in PSRAM
    panel_cfg.flags.refresh_on_demand = false;

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_cfg, &s_panel);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "panel_reset failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "panel_init failed: %s", esp_err_to_name(err));
        return err;
    }

    AC_LOGI(TAG, "RGB panel ready");
    return ESP_OK;
}

esp_lcd_panel_handle_t panel() {
    return s_panel;
}

}  // namespace aqua::display
