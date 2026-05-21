#include "ds1307.h"

#include "ac_logger.h"

namespace aqua::drivers {

static const char* TAG = "DS1307";

Ds1307::~Ds1307() {
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
    }
}

esp_err_t Ds1307::init(aqua::i2c::I2CBus& bus, uint8_t addr) {
    if (bus_ != nullptr) return ESP_OK;
    if (!bus.initialized()) return ESP_ERR_INVALID_STATE;

    // DS1307 is rated up to 100 kHz on standard parts. Some modern clones
    // accept 400 kHz; we follow the conservative datasheet limit.
    esp_err_t err = bus.add_device(addr, 100000, &dev_);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "add_device(0x%02X) failed: %s", addr, esp_err_to_name(err));
        return err;
    }
    bus_ = &bus;
    AC_LOGI(TAG, "Initialised @ 0x%02X", addr);
    return ESP_OK;
}

esp_err_t Ds1307::get_time(struct tm* out, bool* out_valid) {
    if (!initialized() || out == nullptr) return ESP_ERR_INVALID_ARG;

    const uint8_t reg = 0x00;
    uint8_t rx[7] = {0};
    esp_err_t err = bus_->transmit_receive(dev_, &reg, 1, rx, sizeof(rx), 50);
    if (err != ESP_OK) return err;

    const bool ch_set = (rx[0] & 0x80) != 0;
    if (ch_set) {
        AC_LOGW(TAG, "Clock-halt bit set — clearing");
        const uint8_t tx[2] = {0x00, static_cast<uint8_t>(rx[0] & 0x7F)};
        bus_->transmit(dev_, tx, sizeof(tx), 50);
        valid_ = false;
    }

    out->tm_sec  = from_bcd(rx[0] & 0x7F);
    out->tm_min  = from_bcd(rx[1] & 0x7F);
    // Bit6 of hours = 12/24h mode; we always write 24h. Mask off bit6 + bit7.
    out->tm_hour = from_bcd(rx[2] & 0x3F);
    out->tm_wday = (rx[3] & 0x07) - 1;        // DS1307 days 1..7 → wday 0..6
    out->tm_mday = from_bcd(rx[4] & 0x3F);
    out->tm_mon  = from_bcd(rx[5] & 0x1F) - 1; // DS1307 1..12 → tm_mon 0..11
    out->tm_year = from_bcd(rx[6]) + 100;      // years since 1900; DS1307 = 00..99 offset 2000

    if (out_valid != nullptr) *out_valid = valid_ && !ch_set;
    return ESP_OK;
}

esp_err_t Ds1307::set_time(const struct tm& t) {
    if (!initialized()) return ESP_ERR_INVALID_STATE;

    // Always write 24-hour mode (bit6 = 0). CH bit (bit7 of seconds) clears
    // by writing 0 — this starts the oscillator.
    const uint8_t tx[8] = {
        0x00,                                              // start register
        static_cast<uint8_t>(to_bcd(t.tm_sec)  & 0x7F),    // CH = 0
        to_bcd(t.tm_min),
        static_cast<uint8_t>(to_bcd(t.tm_hour) & 0x3F),    // 24-hour mode
        static_cast<uint8_t>((t.tm_wday & 0x07) + 1),
        to_bcd(t.tm_mday),
        to_bcd(t.tm_mon + 1),
        to_bcd(t.tm_year >= 100 ? (t.tm_year - 100) : t.tm_year),
    };
    esp_err_t err = bus_->transmit(dev_, tx, sizeof(tx), 50);
    if (err == ESP_OK) valid_ = true;
    return err;
}

}  // namespace aqua::drivers
