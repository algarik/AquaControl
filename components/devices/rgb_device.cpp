#include "rgb_device.h"

#include "ac_logger.h"
#include "color_utils.h"

#include "esp_timer.h"

namespace aqua::devices {

static const char* TAG = "Rgb";

// Soft-fade tick interval.
static constexpr uint64_t FADE_STEP_US = 100'000;   // 100 ms

// Maps an 8-bit colour component to PCA9685's 12-bit duty cycle (0–4080).
static inline uint16_t comp_to_duty12(uint8_t c8) {
    return static_cast<uint16_t>(static_cast<uint16_t>(c8) << 4);
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
// apply() — entry point called by the bool-pipeline scheduler
// ---------------------------------------------------------------------------
void RgbDevice::apply(bool active, bool force) {
    if (!enabled || pwm_ == nullptr) return;
    if (!force && current_active_.load(std::memory_order_relaxed) == active) return;

    const Hsv to_hsv = active ? color_hsv : Hsv{0.0f, 0.0f, 0.0f};

    const uint32_t fade_ms = active
        ? static_cast<uint32_t>(fade_in_min)  * 60u * 1000u
        : static_cast<uint32_t>(fade_out_min) * 60u * 1000u;

    if (fade_ms == 0) {
        stop_hsv_fade();
        const Rgb8 rgb = hsv_to_rgb(to_hsv);
        pwm_->set_pwm(base_channel_,     comp_to_duty12(rgb.r));
        pwm_->set_pwm(base_channel_ + 1, comp_to_duty12(rgb.g));
        pwm_->set_pwm(base_channel_ + 2, comp_to_duty12(rgb.b));
        taskENTER_CRITICAL(&fade_mux_);
        current_hsv_ = to_hsv;
        taskEXIT_CRITICAL(&fade_mux_);
    } else {
        start_hsv_fade(to_hsv, fade_ms);
    }

    current_active_.store(active, std::memory_order_relaxed);
    AC_LOGI(TAG, "%s (id=%u, base_ch=%u) -> %s hsv=(%.0f,%.2f,%.2f)%s",
            name.c_str(), id, base_channel_, active ? "ON" : "OFF",
            (double)to_hsv.h, (double)to_hsv.s, (double)to_hsv.v,
            fade_ms ? " [hsv-fade]" : "");
}

// ---------------------------------------------------------------------------
// apply_analog() — called by the TEMP_MAP analog scheduler pass
// ---------------------------------------------------------------------------
void RgbDevice::apply_analog(float t) {
    if (!enabled || pwm_ == nullptr) return;
    if (has_override()) return;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    stop_hsv_fade();
    const Hsv  mid = lerp_hsv_native(color_lo_hsv, color_hsv, t);
    const Rgb8 rgb = hsv_to_rgb(mid);
    pwm_->set_pwm(base_channel_,     comp_to_duty12(rgb.r));
    pwm_->set_pwm(base_channel_ + 1, comp_to_duty12(rgb.g));
    pwm_->set_pwm(base_channel_ + 2, comp_to_duty12(rgb.b));
    taskENTER_CRITICAL(&fade_mux_);
    current_hsv_ = mid;
    taskEXIT_CRITICAL(&fade_mux_);
    current_active_.store(mid.v > 0.0f, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// HSV soft-fade helpers
// ---------------------------------------------------------------------------
void RgbDevice::start_hsv_fade(Hsv to_hsv, uint32_t duration_ms) {
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
    fade_to_hsv_   = to_hsv;
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
    Hsv      from_hsv, to_hsv;
    uint64_t start_us, dur_us;

    taskENTER_CRITICAL(&fade_mux_);
    from_hsv = current_hsv_;
    to_hsv   = fade_to_hsv_;
    start_us = fade_start_us_;
    dur_us   = fade_dur_us_;
    taskEXIT_CRITICAL(&fade_mux_);

    const uint64_t elapsed = esp_timer_get_time() - start_us;
    float t = (dur_us > 0)
        ? static_cast<float>(elapsed) / static_cast<float>(dur_us)
        : 1.0f;
    const bool done = (t >= 1.0f);
    if (done) t = 1.0f;

    // Interpolate natively in HSV space — no RGB round-trip.
    const Hsv  mid = lerp_hsv_native(from_hsv, to_hsv, t);
    const Rgb8 rgb = hsv_to_rgb(mid);

    if (pwm_) {
        pwm_->set_pwm(base_channel_,     comp_to_duty12(rgb.r));
        pwm_->set_pwm(base_channel_ + 1, comp_to_duty12(rgb.g));
        pwm_->set_pwm(base_channel_ + 2, comp_to_duty12(rgb.b));
    }

    if (done) {
        taskENTER_CRITICAL(&fade_mux_);
        current_hsv_ = to_hsv;
        taskEXIT_CRITICAL(&fade_mux_);
        esp_timer_stop(fade_timer_);
    }
}

}  // namespace aqua::devices

