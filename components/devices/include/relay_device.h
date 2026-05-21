// AquaControl — RelayDevice (Phase 3).
//
// Binds an IDevice id to a single PCF8575 output channel. Polarity is
// configured on the PCF8575 driver, so we just call set_channel(active).
#pragma once

#include "device_types.h"
#include "pcf8575.h"

namespace aqua::devices {

class RelayDevice : public IDevice {
public:
    RelayDevice(uint8_t id, std::string name,
                aqua::drivers::Pcf8575* expander, uint8_t channel)
        : IDevice(id, std::move(name)), expander_(expander), channel_(channel) {}

    void apply(bool active, bool force = false) override;
    DeviceType get_type() const override { return DeviceType::RELAY; }

    uint8_t channel() const { return channel_; }

    // When true (default), activating the relay drives the channel HIGH.
    // When false, activating drives it LOW (for active-low relay modules).
    bool active_high = true;

private:
    aqua::drivers::Pcf8575* expander_;
    uint8_t                 channel_;
};

}  // namespace aqua::devices
