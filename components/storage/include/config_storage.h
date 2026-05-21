// AquaControl — Device / Trigger persistence (Phase 3.5 Block B7)
//
// Serialise / deserialise the contents of a DeviceManager and
// TriggerManager to JSON blobs stored in NVS under keys "dev_list" and
// "trig_list" respectively.
//
// Devices need driver pointers at construction time, so the loader is
// supplied with a DriverContext that resolves the channel hardware.
#pragma once

#include <cstdint>

#include "device_manager.h"
#include "esp_err.h"
#include "trigger_manager.h"

namespace aqua::drivers {
class Pcf8575;
class Pca9685;
}  // namespace aqua::drivers

namespace aqua::storage {

struct DriverContext {
    aqua::drivers::Pcf8575* pcf  = nullptr;
    aqua::drivers::Pca9685* pca  = nullptr;
};

// Devices.
esp_err_t save_devices(aqua::devices::DeviceManager& dm);
esp_err_t load_devices(aqua::devices::DeviceManager& dm,
                       const DriverContext& ctx);

// Triggers.
esp_err_t save_triggers(aqua::triggers::TriggerManager& tm);
esp_err_t load_triggers(aqua::triggers::TriggerManager& tm);

}  // namespace aqua::storage
