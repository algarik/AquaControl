#include "backlight.h"

#include <algorithm>

#include "ac_logger.h"
#include "app_config.h"
#include "driver/ledc.h"

namespace aqua::display {

static const char* TAG = "Backlight";
static uint8_t s_percent = 0;
static bool s_initialized = false;

esp_err_t backlight_init() {
    if (s_initialized) return ESP_OK;

    ledc_timer_config_t tcfg = {};
    tcfg.speed_mode = AC_BACKLIGHT_LEDC_MODE;
    tcfg.timer_num = AC_BACKLIGHT_LEDC_TIMER;
    tcfg.duty_resolution = static_cast<ledc_timer_bit_t>(AC_BACKLIGHT_LEDC_RES_BITS);
    tcfg.freq_hz = AC_BACKLIGHT_LEDC_FREQ_HZ;
    tcfg.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t ccfg = {};
    ccfg.gpio_num = AC_PIN_BACKLIGHT;
    ccfg.speed_mode = AC_BACKLIGHT_LEDC_MODE;
    ccfg.channel = AC_BACKLIGHT_LEDC_CHANNEL;
    ccfg.intr_type = LEDC_INTR_DISABLE;
    ccfg.timer_sel = AC_BACKLIGHT_LEDC_TIMER;
    ccfg.duty = 0;
    ccfg.hpoint = 0;
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    AC_LOGI(TAG, "Initialized on GPIO %d (%d Hz)", AC_PIN_BACKLIGHT, AC_BACKLIGHT_LEDC_FREQ_HZ);
    return ESP_OK;
}

esp_err_t backlight_set_percent(uint8_t percent) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    percent = std::min<uint8_t>(percent, 100);
    const uint32_t max_duty = (1u << AC_BACKLIGHT_LEDC_RES_BITS) - 1u;
    const uint32_t duty = (max_duty * percent) / 100u;
    esp_err_t err = ledc_set_duty(AC_BACKLIGHT_LEDC_MODE, AC_BACKLIGHT_LEDC_CHANNEL, duty);
    if (err != ESP_OK) return err;
    err = ledc_update_duty(AC_BACKLIGHT_LEDC_MODE, AC_BACKLIGHT_LEDC_CHANNEL);
    if (err == ESP_OK) s_percent = percent;
    return err;
}

uint8_t backlight_get_percent() { return s_percent; }

}  // namespace aqua::display
