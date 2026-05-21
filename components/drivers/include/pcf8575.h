// AquaControl — PCF8575 16-bit I2C GPIO expander driver
//
// Used for relay outputs (1..16 channels, configurable count). The PCF8575
// has no command/register protocol — you simply write 2 bytes (P0..P7 then
// P10..P17) to set all 16 outputs, and read 2 bytes to sample them.
//
// Polarity: each channel can be active-HIGH or active-LOW. Logical state
// (ON/OFF) is converted to a physical bit using the polarity table before
// the hardware write. Default polarity is active-LOW (typical for cheap
// opto-isolated relay boards).
//
// Thread safety: all I2C transactions go through the shared `I2CBus`
// mutex; the driver also serializes its own logical state with a small
// mutex so concurrent set_channel() calls cannot interleave a
// read-modify-write.
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c_bus.h"

namespace aqua::drivers {

class Pcf8575 {
public:
    static constexpr uint8_t CHANNEL_COUNT = 16;

    enum class Polarity : uint8_t { ACTIVE_HIGH, ACTIVE_LOW };

    Pcf8575() = default;
    ~Pcf8575();

    Pcf8575(const Pcf8575&) = delete;
    Pcf8575& operator=(const Pcf8575&) = delete;

    // Bind to the shared bus at the given 7-bit address. Sets all channels
    // to the safe (logical OFF) state immediately.
    esp_err_t init(aqua::i2c::I2CBus& bus, uint8_t addr);

    // Set per-channel polarity. `chan` is 0..15. Does not write to hardware
    // by itself — apply by calling write_all() or set_channel() after.
    esp_err_t set_polarity(uint8_t chan, Polarity p);

    // Logical on/off for a single channel; writes to the device.
    esp_err_t set_channel(uint8_t chan, bool on);

    // Bulk logical state (bit n = channel n, 1 = ON, 0 = OFF).
    esp_err_t set_all(uint16_t logical_mask);

    // Force all channels to safe (logical OFF). Used at boot and on shutdown.
    esp_err_t all_off();

    // Read the device output latch (returns the *physical* word; the high
    // byte is P10..P17, low byte is P00..P07). Used by the watchdog.
    esp_err_t read_back(uint16_t* out_physical);

    // Logical state cache (last value written), not a hardware read.
    uint16_t logical_state() const { return logical_state_; }

    bool initialized() const { return bus_ != nullptr; }

private:
    // Convert the cached logical mask to the physical bit pattern using
    // the polarity table.
    uint16_t logical_to_physical(uint16_t logical) const;

    esp_err_t write_physical(uint16_t physical);

    aqua::i2c::I2CBus*        bus_   = nullptr;
    i2c_master_dev_handle_t   dev_   = nullptr;
    SemaphoreHandle_t         mutex_ = nullptr;

    uint16_t logical_state_   = 0;   // last commanded ON/OFF mask
    uint16_t polarity_invert_ = 0;   // bit set = ACTIVE_LOW for that channel
};

}  // namespace aqua::drivers
