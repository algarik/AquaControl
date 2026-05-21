#include "device_manager.h"

#include "pwm_device.h"
#include "relay_device.h"
#include "rgb_device.h"

namespace aqua::devices {

DeviceManager::DeviceManager() {
    mutex_ = xSemaphoreCreateRecursiveMutex();
}

DeviceManager::~DeviceManager() {
    if (mutex_) vSemaphoreDelete(mutex_);
}

IDevice* DeviceManager::add(std::unique_ptr<IDevice> dev) {
    IDevice* raw = dev.get();
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    devices_.push_back(std::move(dev));
    xSemaphoreGiveRecursive(mutex_);
    return raw;
}

IDevice* DeviceManager::find(uint8_t id) const {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    for (auto& d : devices_) {
        if (d->id == id) {
            xSemaphoreGiveRecursive(mutex_);
            return d.get();
        }
    }
    xSemaphoreGiveRecursive(mutex_);
    return nullptr;
}

bool DeviceManager::remove(uint8_t id) {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    for (auto it = devices_.begin(); it != devices_.end(); ++it) {
        if ((*it)->id == id) {
            devices_.erase(it);
            xSemaphoreGiveRecursive(mutex_);
            return true;
        }
    }
    xSemaphoreGiveRecursive(mutex_);
    return false;
}

uint8_t DeviceManager::next_free_id() const {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    for (uint16_t candidate = 1; candidate <= 255; ++candidate) {
        bool taken = false;
        for (auto& d : devices_) {
            if (d->id == candidate) { taken = true; break; }
        }
        if (!taken) {
            xSemaphoreGiveRecursive(mutex_);
            return (uint8_t)candidate;
        }
    }
    xSemaphoreGiveRecursive(mutex_);
    return 0;
}

void DeviceManager::for_each(const std::function<void(IDevice&)>& fn) {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    for (auto& d : devices_) {
        if (d->enabled) fn(*d);
    }
    xSemaphoreGiveRecursive(mutex_);
}

bool DeviceManager::is_channel_free(DeviceType type, uint8_t base_channel,
                                     const IDevice* exclude) const {
    // Returns the set of (chip, channel) pairs an existing device occupies.
    // chip is 0 for PCF8575 (relays), 1 for PCA9685 (PWM + RGB share this).
    auto occupies = [](const IDevice& dev,
                       uint8_t want_chip, uint8_t want_ch) -> bool {
        switch (dev.get_type()) {
            case DeviceType::RELAY: {
                if (want_chip != 0) return false;
                const auto& r = static_cast<const RelayDevice&>(dev);
                return r.channel() == want_ch;
            }
            case DeviceType::PWM: {
                if (want_chip != 1) return false;
                const auto& p = static_cast<const PwmDevice&>(dev);
                return p.channel() == want_ch;
            }
            case DeviceType::RGB: {
                if (want_chip != 1) return false;
                const auto& g = static_cast<const RgbDevice&>(dev);
                uint8_t b = g.base_channel();
                return want_ch == b || want_ch == b + 1 || want_ch == b + 2;
            }
        }
        return false;
    };

    const uint8_t chip = (type == DeviceType::RELAY) ? 0 : 1;
    const uint8_t span = (type == DeviceType::RGB)   ? 3 : 1;

    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    for (uint8_t i = 0; i < span; ++i) {
        const uint8_t ch = (uint8_t)(base_channel + i);
        for (auto& d : devices_) {
            if (d.get() == exclude) continue;
            if (occupies(*d, chip, ch)) {
                xSemaphoreGiveRecursive(mutex_);
                return false;
            }
        }
    }
    xSemaphoreGiveRecursive(mutex_);
    return true;
}

}  // namespace aqua::devices
