// AquaControl — Shared I2C master bus
//
// Single bus shared by GT911 touch, PCF8575, PCA9685, DS1307, SHT30 (×2).
// All driver classes receive a reference to this object and must NEVER touch
// the underlying ESP-IDF i2c_master API directly.
//
// Thread safety: the bus is owned by Core 0. The internal mutex protects
// against concurrent transmit/receive from the touch-polling task and the
// scheduler/watchdog tasks.
#pragma once

#include <atomic>
#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace aqua::i2c {

class I2CBus {
public:
    I2CBus() = default;
    ~I2CBus();

    I2CBus(const I2CBus&) = delete;
    I2CBus& operator=(const I2CBus&) = delete;

    // One-time initialization of the I2C master bus. Safe to call once at boot.
    esp_err_t init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t freq_hz);

    // Register a 7-bit device. Returned handle is owned by the bus.
    esp_err_t add_device(uint8_t addr, uint32_t scl_speed_hz,
                         i2c_master_dev_handle_t* out_handle);

    // Mutex-protected transactions.
    esp_err_t transmit(i2c_master_dev_handle_t dev, const uint8_t* data, size_t len,
                       int timeout_ms = 50);
    esp_err_t receive(i2c_master_dev_handle_t dev, uint8_t* data, size_t len,
                      int timeout_ms = 50);
    esp_err_t transmit_receive(i2c_master_dev_handle_t dev,
                               const uint8_t* tx, size_t tx_len,
                               uint8_t* rx, size_t rx_len,
                               int timeout_ms = 50);

    // Probe: returns ESP_OK if the device responds, ESP_ERR_NOT_FOUND otherwise.
    esp_err_t probe(uint8_t addr, int timeout_ms = 20);

    // H6: Attempt an I2C bus reset to recover from a stuck peripheral.
    // Safe to call from any task; acquires the bus mutex so in-flight
    // transactions complete (or time out) first.
    esp_err_t reset(int timeout_ms = 200);

    // M-4: Like reset(), but returns false immediately (without resetting)
    // if another task is already performing a reset. Prevents double-reset
    // when sensor_sampler and i2c_watchdog both detect a failure at once.
    bool try_reset(int timeout_ms = 200);

    i2c_master_bus_handle_t handle() const { return bus_; }
    bool initialized() const { return bus_ != nullptr; }

private:
    bool lock(int timeout_ms);
    void unlock();

    i2c_master_bus_handle_t bus_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace aqua::i2c
