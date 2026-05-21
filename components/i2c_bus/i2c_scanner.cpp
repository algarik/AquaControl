#include "i2c_scanner.h"

#include "ac_logger.h"
#include "app_config.h"

namespace aqua::i2c {

static const char* TAG = "I2CScan";

// Latest result cache. Initialised by the first call to scan().
static ScanResult s_last = {};

// Static catalog — addresses pulled from app_config.h. For GT911 we accept
// either 0x14 or 0x5D (board variant), so we probe both and record whichever
// ACKs. SHT30 entries record the canonical address; auto-detect of the actual
// role (water vs ambient) is done by the sensor driver from the scan result.
namespace {
struct CatalogEntry {
    KnownDevice id;
    uint8_t     primary;
    uint8_t     alt;        // 0 if no alternate
    const char* name;
    bool        critical;
};

constexpr CatalogEntry kCatalog[] = {
    { KnownDevice::GT911_TOUCH,   AC_ADDR_GT911_PRIMARY, AC_ADDR_GT911_ALT, "GT911 touch",       false },
    { KnownDevice::PCF8575_RELAYS, AC_ADDR_PCF8575,      0,                 "PCF8575 relays",    true  },
    { KnownDevice::PCA9685_PWM,    AC_ADDR_PCA9685,      0,                 "PCA9685 PWM",       true  },
    { KnownDevice::SHT30_WATER,    AC_ADDR_SHT30_WATER,  0,                 "SHT30 water",       false },
    { KnownDevice::SHT30_AMBIENT,  AC_ADDR_SHT30_AMBIENT, 0,                "SHT30 ambient",     false },
    { KnownDevice::DS1307_RTC,     AC_ADDR_DS1307,       0,                 "DS1307 RTC",        false },
};
static_assert(sizeof(kCatalog) / sizeof(kCatalog[0]) ==
              static_cast<size_t>(KnownDevice::COUNT),
              "kCatalog out of sync with KnownDevice enum");
}  // namespace

ScanResult scan(I2CBus& bus, bool verbose) {
    ScanResult r = {};

    if (!bus.initialized()) {
        AC_LOGE(TAG, "I2C bus not initialized");
        s_last = r;
        return r;
    }

    // 1. Targeted probe of expected devices.
    for (const auto& c : kCatalog) {
        DeviceInfo& d = r.devices[static_cast<size_t>(c.id)];
        d.id       = c.id;
        d.name     = c.name;
        d.critical = c.critical;
        d.present  = false;
        d.addr     = 0;

        if (bus.probe(c.primary, 20) == ESP_OK) {
            d.present = true;
            d.addr    = c.primary;
        } else if (c.alt != 0 && bus.probe(c.alt, 20) == ESP_OK) {
            d.present = true;
            d.addr    = c.alt;
        }

        if (verbose) {
            if (d.present) {
                AC_LOGI(TAG, "  %-16s @ 0x%02X  OK", c.name, d.addr);
            } else {
                AC_LOGW(TAG, "  %-16s         -- not found%s",
                        c.name, c.critical ? " (CRITICAL)" : "");
            }
        }
    }

    // 2. Generic sweep to count any other responders on the bus
    //    (helps spot unexpected hardware or wiring issues).
    uint8_t total = 0;
    for (uint8_t a = 0x08; a <= 0x77; ++a) {
        if (bus.probe(a, 5) == ESP_OK) ++total;
    }
    r.total_responders = total;

    if (verbose) {
        AC_LOGI(TAG, "Bus sweep: %u device(s) responding on 0x08..0x77", total);
    }

    s_last = r;
    return r;
}

const ScanResult& last_result() { return s_last; }

const DeviceInfo& info(KnownDevice id) {
    return s_last.devices[static_cast<size_t>(id)];
}

}  // namespace aqua::i2c
