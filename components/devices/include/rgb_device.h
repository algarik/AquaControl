// AquaControl — RgbDevice (Phase 3).
//
// Three contiguous PCA9685 channels (R, G, B) representing a single RGB
// fixture. Activation ramps each channel from 0 → (component × brightness)
// over fade_in_min, deactivation ramps each to 0 over fade_out_min.
//
// Fades are performed in HSV colour space via a 100 ms software timer so
// that transitions between saturated colours avoid the "grey mud" artefact
// caused by naive per-channel linear-RGB interpolation.
#pragma once

#include "color_utils.h"
#include "device_types.h"
#include "pca9685.h"

#include "esp_timer.h"
#include "freertos/portmacro.h"

namespace aqua::devices {

class RgbDevice : public IDevice {
public:
    RgbDevice(uint8_t id, std::string name,
              aqua::drivers::Pca9685* pwm, uint8_t base_channel)
        : IDevice(id, std::move(name)), pwm_(pwm), base_channel_(base_channel) {}

    ~RgbDevice() override;

    void apply(bool active, bool force = false) override;
    void apply_analog(float t) override;
    DeviceType get_type() const override { return DeviceType::RGB; }

    uint8_t base_channel() const { return base_channel_; }  // R = base, G = base+1, B = base+2

    // User-configurable
    // color_hsv: the "on" / hi-end colour.  h ∈ [0,360), s,v ∈ [0,1].
    // color_lo_hsv: the lo-end colour for TEMP_MAP analog path (default: off).
    Hsv      color_hsv    = {0.0f, 0.0f, 1.0f};   // white at full brightness
    Hsv      color_lo_hsv = {0.0f, 0.0f, 0.0f};   // off
    uint16_t fade_in_min  = 0;
    uint16_t fade_out_min = 0;

private:
    aqua::drivers::Pca9685* pwm_;
    uint8_t                 base_channel_;

    // --- HSV soft-fade engine -------------------------------------------
    // `current_hsv_` holds the last FINALISED displayed colour (updated on
    // immediate apply and on fade completion).  During a fade it is the FROM
    // value; `fade_to_hsv_` is the TO value.
    esp_timer_handle_t fade_timer_   = nullptr;
    Hsv                current_hsv_  = {};   // {0,0,0} when OFF
    Hsv                fade_to_hsv_  = {};
    uint64_t           fade_start_us_ = 0;
    uint64_t           fade_dur_us_   = 1;
    portMUX_TYPE       fade_mux_      = portMUX_INITIALIZER_UNLOCKED;

    void start_hsv_fade(Hsv to_hsv, uint32_t duration_ms);
    void stop_hsv_fade();
    void fade_tick();
    static void fade_timer_cb(void* arg);
};

}  // namespace aqua::devices
