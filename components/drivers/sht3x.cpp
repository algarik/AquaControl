#include "sht3x.h"

#include "ac_logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace aqua::drivers {

static const char* TAG = "SHT3x";

Sht3x::~Sht3x() {
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
    }
}

void Sht3x::deinit() {
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
    }
    bus_  = nullptr;
    addr_ = 0;
}

esp_err_t Sht3x::init(aqua::i2c::I2CBus& bus, uint8_t addr) {
    if (bus_ != nullptr) return ESP_OK;
    if (!bus.initialized()) return ESP_ERR_INVALID_STATE;

    esp_err_t err = bus.add_device(addr, 400000, &dev_);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "add_device(0x%02X) failed: %s", addr, esp_err_to_name(err));
        return err;
    }
    bus_  = &bus;
    addr_ = addr;
    AC_LOGI(TAG, "Initialised @ 0x%02X", addr);
    return ESP_OK;
}

esp_err_t Sht3x::read(Sht3xSample* out) {
    if (!initialized() || out == nullptr) return ESP_ERR_INVALID_ARG;
    *out = {};

    // Single-shot, high-repeatability, clock-stretching disabled.
    const uint8_t cmd[2] = {0x24, 0x00};
    esp_err_t err = bus_->transmit(dev_, cmd, sizeof(cmd), 50);
    if (err != ESP_OK) return err;

    // Datasheet: high-rep max = 15.5 ms. Wait a bit longer for margin.
    vTaskDelay(pdMS_TO_TICKS(20));

    // Retry the receive up to 3 times on CRC failure.  The SHT3x holds its
    // last measurement in the output register until a new command is issued,
    // so a bare re-read is sufficient to recover from a transient I2C glitch
    // without re-triggering a full 20 ms single-shot cycle.
    uint8_t rx[6]  = {0};
    bool    crc_ok = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        err = bus_->receive(dev_, rx, sizeof(rx), 50);
        if (err != ESP_OK) return err;

        if (crc8(&rx[0], 2) == rx[2] && crc8(&rx[3], 2) == rx[5]) {
            crc_ok = true;
            break;
        }
        AC_LOGW(TAG, "0x%02X: CRC mismatch (attempt %d/3)%s",
                addr_, attempt + 1, attempt < 2 ? ", retrying" : "");
        if (attempt < 2) vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (!crc_ok) return ESP_ERR_INVALID_CRC;

    const uint16_t raw_t  = (static_cast<uint16_t>(rx[0]) << 8) | rx[1];
    const uint16_t raw_rh = (static_cast<uint16_t>(rx[3]) << 8) | rx[4];

    out->temp_c   = -45.0f + 175.0f * (static_cast<float>(raw_t)  / 65535.0f);
    out->humidity = 100.0f * (static_cast<float>(raw_rh) / 65535.0f);
    if (out->humidity < 0.0f)   out->humidity = 0.0f;
    if (out->humidity > 100.0f) out->humidity = 100.0f;
    out->valid = true;
    return ESP_OK;
}

uint8_t Sht3x::crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                               : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

}  // namespace aqua::drivers
