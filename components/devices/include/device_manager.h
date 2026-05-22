// AquaControl — DeviceManager (Phase 3).
//
// Owns all IDevice instances and provides lookup by id. The scheduler
// iterates devices via for_each() or all().
//
// A-3: LOCK ORDER: DeviceManager::mutex_ → I2CBus::mutex_.
// The scheduler acquires DeviceManager::mutex_ (via for_each) and then may
// call device->apply() which acquires I2CBus::mutex_ internally.
// Never acquire I2CBus::mutex_ first and then try to acquire
// DeviceManager::mutex_ — that order causes deadlock.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "device_types.h"

namespace aqua::devices {

class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    // Take ownership of a device. Returns the raw pointer for convenient
    // configuration of fields immediately after creation.
    IDevice* add(std::unique_ptr<IDevice> dev);

    // Lookup by id; returns nullptr if not found.
    IDevice* find(uint8_t id) const;

    // Remove the device with the given id. Returns true if a device was
    // removed. The unique_ptr is destroyed, releasing the IDevice.
    bool remove(uint8_t id);

    // Return the lowest unused device id in the range [1..255]. Returns 0
    // if all 255 slots are taken (which is far beyond hardware capacity).
    uint8_t next_free_id() const;

    // Returns true if no existing device occupies the given hardware
    // channel (or any of the channels an RGB device would occupy). RELAY
    // shares the PCF8575 namespace; PWM and RGB share the PCA9685
    // namespace and therefore conflict with each other.  `exclude` is
    // skipped during the scan — pass the device currently being edited
    // so it does not count itself as a conflict.
    bool is_channel_free(DeviceType type, uint8_t base_channel,
                         const IDevice* exclude = nullptr) const;

    // Iterate all enabled devices. Protected by mutex so it is safe to call
    // from Core 0 (scheduler) concurrently with Core 1 add()/remove().
    void for_each(const std::function<void(IDevice&)>& fn);

    // Returns a const reference to the internal vector. Only call from
    // Core 1 (LVGL task) where add/remove are also serialised.
    const std::vector<std::unique_ptr<IDevice>>& all() const { return devices_; }
    size_t size() const { return devices_.size(); }

    // H-4: monotonically increasing version; incremented on add() or remove().
    // Callers (e.g. MQTT discovery guard) can detect when the device list changes.
    uint32_t config_version() const { return config_ver_.load(std::memory_order_acquire); }

private:
    std::vector<std::unique_ptr<IDevice>> devices_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<uint32_t> config_ver_{0};
};

}  // namespace aqua::devices
