// AquaControl — DS1307 I2C real-time clock driver
//
// Registers (datasheet):
//   0x00 seconds  (bit7 = CH, clock-halt)
//   0x01 minutes
//   0x02 hours    (bit6 = 12/24-hour select; we always use 24-h)
//   0x03 day of week (1..7)
//   0x04 day of month (1..31)
//   0x05 month (1..12)
//   0x06 year  (0..99, offset from 2000)
//   0x07 control
//
// All time fields are BCD-encoded. The driver translates to/from a flat
// `struct tm` (POSIX), and reports invalid state if the CH bit is set or
// if the device was never written (year == 0 and CH is still 1 after we
// clear it indicates the chip lost power or never had a battery).
#pragma once

#include <ctime>

#include "esp_err.h"
#include "i2c_bus.h"

namespace aqua::drivers {

class Ds1307 {
public:
    Ds1307() = default;
    ~Ds1307();
    Ds1307(const Ds1307&) = delete;
    Ds1307& operator=(const Ds1307&) = delete;

    esp_err_t init(aqua::i2c::I2CBus& bus, uint8_t addr);

    // Read the current time. If CH (clock-halt) is set the chip has lost
    // power; the function will clear CH but the returned time is unreliable
    // (`out_valid` will be false until set_time() succeeds).
    esp_err_t get_time(struct tm* out, bool* out_valid);

    // Set time from a struct tm. Clears CH; subsequent reads are valid.
    esp_err_t set_time(const struct tm& t);

    // True only after a successful read returned valid time, or set_time().
    bool is_valid() const { return valid_; }

    bool initialized() const { return bus_ != nullptr; }

private:
    static uint8_t to_bcd(uint8_t b)   { return ((b / 10) << 4) | (b % 10); }
    static uint8_t from_bcd(uint8_t b) { return ((b >> 4) * 10) + (b & 0x0F); }

    aqua::i2c::I2CBus*      bus_   = nullptr;
    i2c_master_dev_handle_t dev_   = nullptr;
    bool                    valid_ = false;
};

}  // namespace aqua::drivers
