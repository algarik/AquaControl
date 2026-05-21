// AquaControl — PwmDevice (Phase 3).
//
// Wraps a single PCA9685 channel for a generic dimmable output (heater,
// pump, single-colour LED). Fade duration is in minutes; on apply(true) the
// channel ramps from 0 to `level_%` over fade_in_min, on apply(false) it
// ramps to 0 over fade_out_min. Fade is performed by the PCA9685 driver's
// engine — we just hand it the target.
#pragma once

#include "device_types.h"
#include "pca9685.h"

namespace aqua::devices {

class PwmDevice : public IDevice {
public:
    PwmDevice(uint8_t id, std::string name,
              aqua::drivers::Pca9685* pwm, uint8_t channel)
        : IDevice(id, std::move(name)), pwm_(pwm), channel_(channel) {}

    void apply(bool active, bool force = false) override;
    DeviceType get_type() const override { return DeviceType::PWM; }

    enum class FadeStatus { IDLE, FADING_IN, FADING_OUT };
    FadeStatus fade_status() const;

    uint8_t channel() const { return channel_; }

    // User-configurable
    uint8_t  level_pct       = 100;   // 0–100 % at full activation
    uint16_t fade_in_min     = 0;     // 0 = immediate
    uint16_t fade_out_min    = 0;

private:
    aqua::drivers::Pca9685* pwm_;
    uint8_t                 channel_;
};

}  // namespace aqua::devices
