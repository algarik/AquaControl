// AquaControl — I2C bus scanner
//
// Scans the shared I2C bus for known device addresses, returns a structured
// result so other modules (boot screen, watchdog, UI) can report status
// without re-scanning. Also exposes a generic "ping every 7-bit address"
// helper for the System Status screen.
#pragma once

#include <array>
#include <cstdint>

#include "esp_err.h"
#include "i2c_bus.h"

namespace aqua::i2c {

// Catalog of devices expected on the AquaControl bus. Order matters: it
// drives the boot-screen status table.
enum class KnownDevice : uint8_t {
    GT911_TOUCH,
    PCF8575_RELAYS,
    PCA9685_PWM,
    SHT30_WATER,
    SHT30_AMBIENT,
    DS1307_RTC,
    COUNT
};

struct DeviceInfo {
    KnownDevice  id;
    uint8_t      addr;          // 7-bit address actually observed (0 if not found)
    const char*  name;          // human-readable
    bool         present;       // ACK seen on the bus during last scan
    bool         critical;      // true => boot must halt on absence (fish safety)
};

struct ScanResult {
    std::array<DeviceInfo, static_cast<size_t>(KnownDevice::COUNT)> devices;
    uint8_t  total_responders;  // any address in 0x08..0x77 that ACKed
};

// Run a full scan of the expected catalog (plus a 0x08..0x77 sweep for the
// `total_responders` count). Pure read operation; safe to call multiple times.
// `verbose` logs each probe at INFO level (use for boot diagnostics).
ScanResult scan(I2CBus& bus, bool verbose = true);

// Access the latest result (populated by the last call to `scan`).
const ScanResult& last_result();

// Convenience: lookup by KnownDevice (asserts index < COUNT).
const DeviceInfo& info(KnownDevice id);

}  // namespace aqua::i2c
