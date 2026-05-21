// AquaControl — Sensirion SHT3x temperature/humidity sensor driver
//
// Single-shot, high-repeatability measurement (command 0x2C 0x06).
// Each measurement returns 6 bytes: T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC.
// CRC-8 polynomial 0x31, init 0xFF.
//
// One driver instance covers one sensor at a known address. The watchdog /
// boot scanner decides which physical role (water = 0x44, ambient = 0x45)
// to assign to each detected sensor.
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "i2c_bus.h"

namespace aqua::drivers {

struct Sht3xSample {
    float temp_c;       // °C
    float humidity;     // % RH (0..100); 0 if sensor variant has no humidity
    bool  valid;
};

class Sht3x {
public:
    Sht3x() = default;
    ~Sht3x();
    Sht3x(const Sht3x&) = delete;
    Sht3x& operator=(const Sht3x&) = delete;

    esp_err_t init(aqua::i2c::I2CBus& bus, uint8_t addr);

    // Release the I2C device handle so init() can be called again (recovery).
    void deinit();

    // Trigger a single-shot measurement and read 6 bytes. Blocks for ~16 ms
    // (max conversion time for high-repeatability is 15 ms per datasheet).
    esp_err_t read(Sht3xSample* out);

    uint8_t address() const { return addr_; }
    bool initialized() const { return bus_ != nullptr; }

private:
    static uint8_t crc8(const uint8_t* data, size_t len);

    aqua::i2c::I2CBus*      bus_  = nullptr;
    i2c_master_dev_handle_t dev_  = nullptr;
    uint8_t                 addr_ = 0;
};

}  // namespace aqua::drivers
