#include "rgb_device.h"

#include "ac_logger.h"
#include "color_utils.h"

#include "esp_timer.h"

namespace aqua::devices {

static const char* TAG = "Rgb";

// Soft-fade tick interval.
static constexpr uint64_t FADE_STEP_US = 100'000;   // 100 ms

// Maps brightness-scaled 8-bit colour component to PCA9685's 12-bit duty.
static inline uint16_t comp_to_duty(uint8_t c8, uint8_t brightness_pct) {
    const uint32_t scaled = (static_cast<uint32_t>(c8) * brightness_pct) / 100u;
    return static_cast<uint16_t>(scaled << 4);
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
RgbDevice::~RgbDevice() {
    stop_hsv_fade();
    if (fade_timer_ != nullptr) {
        esp_timer_delete(fade_timer_);
        fade_timer_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// apply() — entry point called by the scheduler
// ---------------------------------------------------------------------------
void RgbDevice::apply(bool active, bool force) {
    if (!enabled || pwm_ == nullptr) return;
    if (!force && current_active_.load(std::memory_order_relaxed) == active) return;

    const RgbColor to_color = active ? color : RgbColor{0, 0, 0};
    const uint8_t  to_brt   = active ? brightness_pct : 0u;

    const uint32_t fade_ms = active
        ? static_cast<uint32_t>(fade_in_min)  * 60u * 1000u
        : static_cast<uint32_t>(fade_out_min) * 60u * 1000u;

    if (fade_ms == 0) {
        stop_hsv_fade();
        pwm_->set_pwm(base_channel_,     comp_to_duty(to_color.r, to_brt));
        pwm_->set_pwm(base_channel_ + 1, comp_to_duty(to_color.g, to_brt));
        pwm_->set_pwm(base_channel_ + 2, comp_to_duty(to_color.b, to_brt));
        taskENTER_CRITICAL(&fade_mux_);
        current_color_ = to_color;
        current_brt_   = to_brt;
        taskEXIT_CRITICAL(&fade_mux_);
    } else {
        start_hsv_fade(to_color, to_brt, fade_ms);
    }

    current_active_.store(active, std::memory_order_relaxed);
    AC_LOGI(TAG, "%s (id=%u, base_ch=%u) -> %s rgb=(%u,%u,%u)%s",
            name.c_str(), id, base_channel_, active ? "ON" : "OFF",
            to_color.r, to_color.g, to_color.b,
            fade_ms ? " [hsv-fade]" : "");
}

// ---------------------------------------------------------------------------
// HSV soft-fade helpers
// ---------------------------------------------------------------------------
void RgbDevice::start_hsv_fade(RgbColor to_color, uint8_t to_brt,
                                uint32_t duration_ms) {
    stop_hsv_fade();

    if (fade_timer_ == nullptr) {
        esp_timer_create_args_t args{};
        args.callback        = &RgbDevice::fade_timer_cb;
        args.arg             = this;
        args.name            = "rgb_fade";
        args.dispatch_method = ESP_TIMER_TASK;
        esp_timer_create(&args, &fade_timer_);
    }

    taskENTER_CRITICAL(&fade_mux_);
    fade_to_color_ = to_color;
    fade_to_brt_   = to_brt;
    fade_start_us_ = esp_timer_get_time();
    fade_dur_us_   = static_cast<uint64_t>(duration_ms) * 1000u;
    taskEXIT_CRITICAL(&fade_mux_);

    esp_timer_start_periodic(fade_timer_, FADE_STEP_US);
}

void RgbDevice::stop_hsv_fade() {
    if (fade_timer_ != nullptr) {
        esp_timer_stop(fade_timer_);   // safe to call even if not running
    }
}

void RgbDevice::fade_timer_cb(void* arg) {
    static_cast<RgbDevice*>(arg)->fade_tick();
}

void RgbDevice::fade_tick() {
    // Snapshot shared state under spinlock (no I2C calls inside).
    RgbColor from_c, to_c;
    uint8_t  from_brt, to_brt;
    uint64_t start_us, dur_us;

    taskENTER_CRITICAL(&fade_mux_);
    from_c   = current_color_;
    from_brt = current_brt_;
    to_c     = fade_to_color_;
    to_brt   = fade_to_brt_;
    start_us = fade_start_us_;
    dur_us   = fade_dur_us_;
    taskEXIT_CRITICAL(&fade_mux_);

    const uint64_t elapsed = esp_timer_get_time() - start_us;
    float t = (dur_us > 0) ? static_cast<float>(elapsed) / static_cast<float>(dur_us) : 1.0f;
    const bool done = (t >= 1.0f);
    if (done) t = 1.0f;

    // HSV-interpolated colour at time t.
    const Rgb8 cur = lerp_hsv({from_c.r, from_c.g, from_c.b},
                               {to_c.r, to_c.g, to_c.b}, t);
    const uint8_t brt = static_cast<uint8_t>(
        static_cast<float>(from_brt) + static_cast<float>(to_brt - from_brt) * t);

    if (pwm_) {
        pwm_->set_pwm(base_channel_,     comp_to_duty(cur.r, brt));
        pwm_->set_pwm(base_channel_ + 1, comp_to_duty(cur.g, brt));
        pwm_->set_pwm(base_channel_ + 2, comp_to_duty(cur.b, brt));
    }

    if (done) {
        // Finalise — update tracking and stop timer.
        taskENTER_CRITICAL(&fade_mux_);
        current_color_ = to_c;
        current_brt_   = to_brt;
        taskEXIT_CRITICAL(&fade_mux_);
        esp_timer_stop(fade_timer_);
    }
}

}  // namespace aqua::devices

