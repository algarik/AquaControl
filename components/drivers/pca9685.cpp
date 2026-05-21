#include "pca9685.h"

#include <algorithm>

#include "ac_logger.h"

namespace aqua::drivers {

static const char* TAG = "PCA9685";

// PCA9685 register map (subset)
namespace reg {
constexpr uint8_t MODE1      = 0x00;
constexpr uint8_t MODE2      = 0x01;
constexpr uint8_t LED0_ON_L  = 0x06;   // 4 bytes per channel, stride 4
constexpr uint8_t ALL_LED_ON_L = 0xFA;
constexpr uint8_t PRESCALE   = 0xFE;

// MODE1 bits
constexpr uint8_t M1_RESTART = 1 << 7;
constexpr uint8_t M1_EXTCLK  = 1 << 6;
constexpr uint8_t M1_AI      = 1 << 5;   // auto-increment
constexpr uint8_t M1_SLEEP   = 1 << 4;
constexpr uint8_t M1_ALLCALL = 1 << 0;
// MODE2 bits
constexpr uint8_t M2_OUTDRV  = 1 << 2;   // totem-pole output (default for MOSFETs)
}  // namespace reg

constexpr uint32_t kFadeTickMs = 100;     // engine resolution

static esp_err_t write_reg(aqua::i2c::I2CBus* bus,
                           i2c_master_dev_handle_t dev,
                           uint8_t reg, uint8_t val) {
    const uint8_t tx[2] = {reg, val};
    return bus->transmit(dev, tx, sizeof(tx), 50);
}

Pca9685::~Pca9685() {
    if (timer_ != nullptr) {
        xTimerStop(timer_, 0);
        xTimerDelete(timer_, 0);
        timer_ = nullptr;
    }
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

esp_err_t Pca9685::init(aqua::i2c::I2CBus& bus, uint8_t addr) {
    if (bus_ != nullptr) return ESP_OK;
    if (!bus.initialized()) return ESP_ERR_INVALID_STATE;

    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) return ESP_ERR_NO_MEM;

    esp_err_t err = bus.add_device(addr, 400000, &dev_);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "add_device(0x%02X) failed: %s", addr, esp_err_to_name(err));
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        return err;
    }
    bus_ = &bus;

    // 1. Enter sleep so we can program PRESCALE.
    err = write_reg(bus_, dev_, reg::MODE1, reg::M1_SLEEP);
    if (err != ESP_OK) goto fail;

    // 2. Prescale = 5 → ~1 kHz PWM @ 25 MHz internal osc.
    //    prescale = round(25e6 / (4096 * f_pwm)) - 1 = 5 for f_pwm = 1 kHz.
    err = write_reg(bus_, dev_, reg::PRESCALE, 5);
    if (err != ESP_OK) goto fail;

    // 3. Wake + auto-increment + allcall.
    err = write_reg(bus_, dev_, reg::MODE1, reg::M1_AI | reg::M1_ALLCALL);
    if (err != ESP_OK) goto fail;

    // Oscillator needs >= 500 µs to stabilise after wake.
    vTaskDelay(pdMS_TO_TICKS(1));

    // 4. Totem-pole outputs (MOSFET gates).
    err = write_reg(bus_, dev_, reg::MODE2, reg::M2_OUTDRV);
    if (err != ESP_OK) goto fail;

    // 5. Force all channels to 0.
    err = all_off();
    if (err != ESP_OK) goto fail;

    // 6. Create the fade-engine timer (auto-reload, not started yet).
    timer_ = xTimerCreate("pca_fade", pdMS_TO_TICKS(kFadeTickMs), pdTRUE,
                         this, &Pca9685::timer_cb);
    if (timer_ == nullptr) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    AC_LOGI(TAG, "Initialised @ 0x%02X (1 kHz PWM, all channels 0)", addr);
    return ESP_OK;

fail:
    AC_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
    }
    vSemaphoreDelete(mutex_);
    mutex_ = nullptr;
    bus_   = nullptr;
    return err;
}

esp_err_t Pca9685::set_pwm(uint8_t chan, uint16_t duty) {
    if (chan >= CHANNEL_COUNT) return ESP_ERR_INVALID_ARG;
    if (!initialized()) return ESP_ERR_INVALID_STATE;
    if (duty > MAX_DUTY) duty = MAX_DUTY;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;
    fades_[chan].active = false;
    fades_[chan].current = duty;
    fades_[chan].target  = duty;
    esp_err_t err = write_channel(chan, duty);
    xSemaphoreGive(mutex_);
    return err;
}

esp_err_t Pca9685::set_rgb(uint8_t strip, uint16_t r, uint16_t g, uint16_t b) {
    if (strip > 1) return ESP_ERR_INVALID_ARG;
    const uint8_t base = (strip == 0) ? 5 : 8;
    esp_err_t e1 = set_pwm(base + 0, r);
    esp_err_t e2 = set_pwm(base + 1, g);
    esp_err_t e3 = set_pwm(base + 2, b);
    return (e1 != ESP_OK) ? e1 : (e2 != ESP_OK ? e2 : e3);
}

esp_err_t Pca9685::all_off() {
    if (!initialized()) return ESP_ERR_INVALID_STATE;
    // Use ALL_LED_ON_L block: write 4 bytes (ON_L, ON_H, OFF_L, OFF_H).
    // OFF=0 + FULL_OFF bit set in OFF_H gives a clean low.
    const uint8_t tx[5] = {reg::ALL_LED_ON_L, 0x00, 0x00, 0x00, 0x10};
    esp_err_t err = bus_->transmit(dev_, tx, sizeof(tx), 50);
    if (err == ESP_OK) {
        for (auto& f : fades_) {
            f.active = false;
            f.current = 0;
            f.target = 0;
        }
    }
    return err;
}

esp_err_t Pca9685::fade_to(uint8_t chan, uint16_t target_duty, uint32_t duration_ms) {
    if (chan >= CHANNEL_COUNT) return ESP_ERR_INVALID_ARG;
    if (!initialized()) return ESP_ERR_INVALID_STATE;
    if (target_duty > MAX_DUTY) target_duty = MAX_DUTY;

    if (duration_ms == 0) {
        return set_pwm(chan, target_duty);
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;

    FadeState& f = fades_[chan];
    const int32_t delta = static_cast<int32_t>(target_duty) - static_cast<int32_t>(f.current);
    const uint32_t ticks = std::max<uint32_t>(1, duration_ms / kFadeTickMs);
    f.step_q8  = (delta * 256) / static_cast<int32_t>(ticks);   // Q8.8 step per tick
    // Guard: tiny delta over long duration can truncate to 0 — ensure progress.
    if (f.step_q8 == 0 && delta != 0) {
        f.step_q8 = (delta < 0) ? -1 : 1;
    }
    f.accum_q8 = static_cast<int32_t>(f.current) * 256;
    f.target   = target_duty;
    f.active   = true;

    // Ensure the timer is running.
    if (timer_ != nullptr && xTimerIsTimerActive(timer_) == pdFALSE) {
        xTimerStart(timer_, 0);
    }

    xSemaphoreGive(mutex_);
    return ESP_OK;
}

void Pca9685::cancel_fade(uint8_t chan) {
    if (chan >= CHANNEL_COUNT) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return;
    fades_[chan].active = false;
    fades_[chan].target = fades_[chan].current;
    xSemaphoreGive(mutex_);
}

uint16_t Pca9685::current_duty(uint8_t chan) const {
    if (chan >= CHANNEL_COUNT) return 0;
    return fades_[chan].current;
}

esp_err_t Pca9685::write_channel(uint8_t chan, uint16_t duty) {
    // LEDn_ON_L = 0x06 + 4*chan ; write ON_L, ON_H, OFF_L, OFF_H.
    uint8_t on_l = 0, on_h = 0, off_l = 0, off_h = 0;
    if (duty == 0) {
        off_h = 0x10;  // FULL_OFF
    } else if (duty >= MAX_DUTY) {
        on_h = 0x10;   // FULL_ON
    } else {
        off_l = static_cast<uint8_t>(duty & 0xFF);
        off_h = static_cast<uint8_t>((duty >> 8) & 0x0F);
    }
    const uint8_t reg_addr = reg::LED0_ON_L + 4u * chan;
    const uint8_t tx[5] = {reg_addr, on_l, on_h, off_l, off_h};
    return bus_->transmit(dev_, tx, sizeof(tx), 50);
}

void Pca9685::timer_cb(TimerHandle_t t) {
    auto* self = static_cast<Pca9685*>(pvTimerGetTimerID(t));
    self->tick();
}

void Pca9685::tick() {
    if (xSemaphoreTake(mutex_, 0) != pdTRUE) return;

    bool any_active = false;
    for (uint8_t ch = 0; ch < CHANNEL_COUNT; ++ch) {
        FadeState& f = fades_[ch];
        if (!f.active) continue;

        f.accum_q8 += f.step_q8;
        int32_t next = f.accum_q8 / 256;

        bool done = false;
        if (f.step_q8 >= 0) {
            if (next >= f.target) { next = f.target; done = true; }
        } else {
            if (next <= f.target) { next = f.target; done = true; }
        }
        if (next < 0) next = 0;
        if (next > MAX_DUTY) next = MAX_DUTY;

        f.current = static_cast<uint16_t>(next);
        write_channel(ch, f.current);
        if (done) f.active = false;
        else      any_active = true;
    }

    // No more work — stop the timer to save CPU.
    if (!any_active && timer_ != nullptr) {
        xTimerStop(timer_, 0);
    }
    xSemaphoreGive(mutex_);
}

bool Pca9685::is_fading(uint8_t chan) const {
    if (chan >= CHANNEL_COUNT) return false;
    return fades_[chan].active;
}

}  // namespace aqua::drivers
