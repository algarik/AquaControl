// AquaControl — PCA9685 16-channel 12-bit PWM driver
//
// Wired channels:
//   0..4  — single PWM outputs (PWM 1..5)
//   5..7  — RGB strip #1 (R, G, B)
//   8..10 — RGB strip #2 (R, G, B)
//   11..15 — reserved
//
// PWM frequency: 1 kHz (prescale = 5 @ 25 MHz internal oscillator). All
// outputs drive MOSFET gates, so this is flicker-free and silent.
//
// Fade engine: each channel can ramp toward a target duty over a configured
// duration. A single FreeRTOS software timer (100 ms period) steps all
// active fades. When current == target the fade is removed from the active
// set; the timer stops itself when no fades remain.
#pragma once

#include <array>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "i2c_bus.h"

namespace aqua::drivers {

class Pca9685 {
public:
    static constexpr uint8_t CHANNEL_COUNT = 16;
    static constexpr uint16_t MAX_DUTY     = 4095;  // 12-bit

    Pca9685() = default;
    ~Pca9685();

    Pca9685(const Pca9685&) = delete;
    Pca9685& operator=(const Pca9685&) = delete;

    // Bind to the shared bus. Programs prescale for 1 kHz, enables auto-
    // increment, takes the chip out of sleep, and forces all channels to 0.
    esp_err_t init(aqua::i2c::I2CBus& bus, uint8_t addr);

    // Set channel duty (0..4095) immediately. Cancels any ongoing fade on
    // that channel.
    esp_err_t set_pwm(uint8_t chan, uint16_t duty);

    // RGB helper: strip 0 -> channels 5..7, strip 1 -> channels 8..10.
    esp_err_t set_rgb(uint8_t strip, uint16_t r, uint16_t g, uint16_t b);

    // Force all channels to 0 (safe state). Cancels all fades.
    esp_err_t all_off();

    // ---- Fade engine ----------------------------------------------------

    // Ramp `chan` from its current duty to `target_duty` over `duration_ms`.
    // duration_ms == 0 means apply immediately (calls set_pwm).
    // Replaces any existing fade on the channel.
    esp_err_t fade_to(uint8_t chan, uint16_t target_duty, uint32_t duration_ms);

    // Cancel a fade in progress; the channel stays at its current duty.
    void cancel_fade(uint8_t chan);

    // Returns true if a fade is currently in progress on the channel.
    bool is_fading(uint8_t chan) const;

    // Snapshot of current duty (last commanded, including in-flight fade).
    uint16_t current_duty(uint8_t chan) const;

    bool initialized() const { return bus_ != nullptr; }

private:
    // Single-channel hardware write: ON_L/ON_H = 0, OFF_L/OFF_H = duty.
    // Duty == 0 sets the FULL_OFF bit (idle low); duty == MAX_DUTY sets
    // FULL_ON for cleanest waveform.
    esp_err_t write_channel(uint8_t chan, uint16_t duty);

    static void timer_cb(TimerHandle_t t);
    void        tick();

    aqua::i2c::I2CBus*      bus_   = nullptr;
    i2c_master_dev_handle_t dev_   = nullptr;
    SemaphoreHandle_t       mutex_ = nullptr;
    TimerHandle_t           timer_ = nullptr;

    struct FadeState {
        uint16_t current;     // 0..4095
        uint16_t target;
        int32_t  step_q8;     // signed Q8.8 steps per 100 ms tick
        int32_t  accum_q8;    // accumulated fractional position (Q8.8)
        bool     active;
    };

    std::array<FadeState, CHANNEL_COUNT> fades_ = {};
};

}  // namespace aqua::drivers
