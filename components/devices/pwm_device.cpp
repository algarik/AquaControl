#include "pwm_device.h"

#include "ac_logger.h"

namespace aqua::devices {

static const char* TAG = "Pwm";

void PwmDevice::apply(bool active, bool force) {
    if (!enabled || pwm_ == nullptr) return;
    if (!force && current_active_.load(std::memory_order_relaxed) == active) return;

    const uint16_t target_duty = active
        ? static_cast<uint16_t>((static_cast<uint32_t>(level_pct) * 4095) / 100)
        : 0;
    const uint32_t fade_ms = active
        ? static_cast<uint32_t>(fade_in_min)  * 60u * 1000u
        : static_cast<uint32_t>(fade_out_min) * 60u * 1000u;

    esp_err_t err = (fade_ms == 0)
        ? pwm_->set_pwm(channel_, target_duty)
        : pwm_->fade_to(channel_, target_duty, fade_ms);

    if (err != ESP_OK) {
        AC_LOGE(TAG, "%s ch%u apply(%d) failed: %s",
                name.c_str(), channel_, active, esp_err_to_name(err));
        return;
    }
    current_active_.store(active, std::memory_order_relaxed);
    AC_LOGI(TAG, "%s (id=%u, ch=%u) -> %s%s%s",
            name.c_str(), id, channel_, active ? "ON" : "OFF",
            fade_ms ? " (fade)" : "",
            force ? " [force]" : "");
}

PwmDevice::FadeStatus PwmDevice::fade_status() const {
    if (pwm_ == nullptr || !pwm_->is_fading(channel_)) return FadeStatus::IDLE;
    return current_active_.load(std::memory_order_relaxed)
        ? FadeStatus::FADING_IN : FadeStatus::FADING_OUT;
}

}  // namespace aqua::devices
