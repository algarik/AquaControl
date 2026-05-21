#include "i2c_bus.h"

#include "ac_logger.h"

namespace aqua::i2c {

static const char* TAG = "I2CBus";

I2CBus::~I2CBus() {
    if (bus_ != nullptr) {
        i2c_del_master_bus(bus_);
        bus_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

esp_err_t I2CBus::init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t freq_hz) {
    if (bus_ != nullptr) {
        AC_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        AC_LOGE(TAG, "Failed to allocate mutex");
        return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = port;
    cfg.sda_io_num = sda;
    cfg.scl_io_num = scl;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&cfg, &bus_);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        return err;
    }

    AC_LOGI(TAG, "Initialized on port %d (SDA=%d, SCL=%d, %lu Hz)",
            (int)port, (int)sda, (int)scl, (unsigned long)freq_hz);
    return ESP_OK;
}

esp_err_t I2CBus::add_device(uint8_t addr, uint32_t scl_speed_hz,
                             i2c_master_dev_handle_t* out_handle) {
    if (bus_ == nullptr || out_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = scl_speed_hz;
    return i2c_master_bus_add_device(bus_, &cfg, out_handle);
}

bool I2CBus::lock(int timeout_ms) {
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void I2CBus::unlock() {
    xSemaphoreGive(mutex_);
}

esp_err_t I2CBus::transmit(i2c_master_dev_handle_t dev, const uint8_t* data, size_t len,
                           int timeout_ms) {
    if (!lock(timeout_ms)) {
        AC_LOGW(TAG, "transmit: bus mutex timeout");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit(dev, data, len, timeout_ms);
    unlock();
    return err;
}

esp_err_t I2CBus::receive(i2c_master_dev_handle_t dev, uint8_t* data, size_t len,
                          int timeout_ms) {
    if (!lock(timeout_ms)) {
        AC_LOGW(TAG, "receive: bus mutex timeout");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_receive(dev, data, len, timeout_ms);
    unlock();
    return err;
}

esp_err_t I2CBus::transmit_receive(i2c_master_dev_handle_t dev,
                                   const uint8_t* tx, size_t tx_len,
                                   uint8_t* rx, size_t rx_len,
                                   int timeout_ms) {
    if (!lock(timeout_ms)) {
        AC_LOGW(TAG, "transmit_receive: bus mutex timeout");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, timeout_ms);
    unlock();
    return err;
}

esp_err_t I2CBus::probe(uint8_t addr, int timeout_ms) {
    if (bus_ == nullptr) return ESP_ERR_INVALID_STATE;
    // Decouple mutex-wait from I2C transfer timeout.  Any concurrent
    // transaction (LVGL touch poll, sensor read) holds the mutex for only
    // a few ms; waiting up to 300 ms avoids false "probe failed" reports
    // due to transient bus contention.  The actual i2c_master_probe() call
    // still uses the caller-supplied timeout so real device faults are
    // detected quickly.
    const int mutex_wait_ms = (timeout_ms >= 300) ? timeout_ms : 300;
    if (!lock(mutex_wait_ms)) return ESP_ERR_TIMEOUT;
    esp_err_t err = i2c_master_probe(bus_, addr, timeout_ms);
    unlock();
    return err;
}

esp_err_t I2CBus::reset(int timeout_ms) {
    // H6: Reset the I2C peripheral state machine. This unsticks any
    // operation that failed mid-transfer (e.g., device disconnected).
    // Device handles registered via add_device() remain valid after reset.
    if (bus_ == nullptr) return ESP_ERR_INVALID_STATE;
    if (!lock(timeout_ms)) {
        AC_LOGW(TAG, "reset: bus mutex timeout — forcing hardware reset");
        // Even if we couldn't take the mutex, attempt the reset so the
        // hardware recovers. The mutex state may be corrupted but the
        // watchdog has a higher priority than normal I2C users.
        esp_err_t err = i2c_master_bus_reset(bus_);
        return err;
    }
    esp_err_t err = i2c_master_bus_reset(bus_);
    unlock();
    if (err != ESP_OK) {
        AC_LOGE(TAG, "bus reset failed: %s", esp_err_to_name(err));
    } else {
        AC_LOGW(TAG, "I2C bus reset OK");
    }
    return err;
}

}  // namespace aqua::i2c
