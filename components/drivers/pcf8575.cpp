#include "pcf8575.h"

#include "ac_logger.h"

namespace aqua::drivers {

static const char* TAG = "PCF8575";

Pcf8575::~Pcf8575() {
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

esp_err_t Pcf8575::init(aqua::i2c::I2CBus& bus, uint8_t addr) {
    if (bus_ != nullptr) return ESP_OK;
    if (!bus.initialized()) return ESP_ERR_INVALID_STATE;

    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) return ESP_ERR_NO_MEM;

    // PCF8575 is a quasi-bidirectional GPIO expander. 400 kHz is its rated
    // max and matches the shared bus speed.
    esp_err_t err = bus.add_device(addr, 400000, &dev_);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "add_device(0x%02X) failed: %s", addr, esp_err_to_name(err));
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        return err;
    }

    bus_              = &bus;
    logical_state_    = 0;
    polarity_invert_  = 0xFFFF;  // default ACTIVE_LOW for all 16 channels

    // Push the safe state to hardware immediately.
    err = write_physical(logical_to_physical(0));
    if (err != ESP_OK) {
        AC_LOGE(TAG, "Initial all-off write failed: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        bus_   = nullptr;
        return err;
    }

    AC_LOGI(TAG, "Initialised @ 0x%02X (all channels OFF, active-LOW default)", addr);
    return ESP_OK;
}

esp_err_t Pcf8575::set_polarity(uint8_t chan, Polarity p) {
    if (chan >= CHANNEL_COUNT) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (p == Polarity::ACTIVE_LOW) {
        polarity_invert_ |= (1u << chan);
    } else {
        polarity_invert_ &= ~(1u << chan);
    }
    xSemaphoreGive(mutex_);
    return ESP_OK;
}

esp_err_t Pcf8575::set_channel(uint8_t chan, bool on) {
    if (chan >= CHANNEL_COUNT) return ESP_ERR_INVALID_ARG;
    if (!initialized()) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;
    uint16_t next = logical_state_;
    if (on) next |= (1u << chan);
    else    next &= ~(1u << chan);

    esp_err_t err = write_physical(logical_to_physical(next));
    if (err == ESP_OK) logical_state_ = next;
    xSemaphoreGive(mutex_);
    return err;
}

esp_err_t Pcf8575::set_all(uint16_t logical_mask) {
    if (!initialized()) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = write_physical(logical_to_physical(logical_mask));
    if (err == ESP_OK) logical_state_ = logical_mask;
    xSemaphoreGive(mutex_);
    return err;
}

esp_err_t Pcf8575::all_off() {
    return set_all(0);
}

esp_err_t Pcf8575::read_back(uint16_t* out_physical) {
    if (!initialized() || out_physical == nullptr) return ESP_ERR_INVALID_ARG;
    uint8_t rx[2] = {0, 0};
    esp_err_t err = bus_->receive(dev_, rx, sizeof(rx), 50);
    if (err == ESP_OK) {
        *out_physical = static_cast<uint16_t>(rx[0]) |
                        (static_cast<uint16_t>(rx[1]) << 8);
    }
    return err;
}

uint16_t Pcf8575::logical_to_physical(uint16_t logical) const {
    // For ACTIVE_LOW channels we invert the bit before writing.
    // (Active-LOW: physical bit 0 = relay ON, physical bit 1 = relay OFF.)
    return static_cast<uint16_t>(logical ^ polarity_invert_);
}

esp_err_t Pcf8575::write_physical(uint16_t physical) {
    // PCF8575 wire format: P00..P07 first byte, P10..P17 second byte.
    const uint8_t tx[2] = {
        static_cast<uint8_t>(physical & 0xFF),
        static_cast<uint8_t>((physical >> 8) & 0xFF),
    };
    return bus_->transmit(dev_, tx, sizeof(tx), 50);
}

}  // namespace aqua::drivers
