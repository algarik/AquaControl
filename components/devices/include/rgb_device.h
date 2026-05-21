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

struct RgbColor {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
};

class RgbDevice : public IDevice {
public:
    RgbDevice(uint8_t id, std::string name,
              aqua::drivers::Pca9685* pwm, uint8_t base_channel)
        : IDevice(id, std::move(name)), pwm_(pwm), base_channel_(base_channel) {}

    ~RgbDevice() override;

    void apply(bool active, bool force = false) override;
    DeviceType get_type() const override { return DeviceType::RGB; }

    uint8_t base_channel() const { return base_channel_; }  // R = base, G = base+1, B = base+2

    // User-configurable
    RgbColor color;
    uint8_t  brightness_pct = 100;
    uint16_t fade_in_min    = 0;
    uint16_t fade_out_min   = 0;

private:
    aqua::drivers::Pca9685* pwm_;
    uint8_t                 base_channel_;

    // --- HSV soft-fade engine -------------------------------------------
    // `current_color_` / `current_brt_` hold the last FINALISED displayed
    // colour (updated on immediate apply and on fade completion).  During a
    // fade they serve as the FROM values; the TO values are in fade_to_*.
    esp_timer_handle_t fade_timer_    = nullptr;
    RgbColor           current_color_ = {};   // {0,0,0} when OFF
    uint8_t            current_brt_   = 0;
    RgbColor           fade_to_color_ = {};
    uint8_t            fade_to_brt_   = 0;
    uint64_t           fade_start_us_ = 0;
    uint64_t           fade_dur_us_   = 1;
    portMUX_TYPE       fade_mux_      = portMUX_INITIALIZER_UNLOCKED;

    void start_hsv_fade(RgbColor to_color, uint8_t to_brt, uint32_t duration_ms);
    void stop_hsv_fade();
    void fade_tick();
    static void fade_timer_cb(void* arg);
};

}  // namespace aqua::devices
