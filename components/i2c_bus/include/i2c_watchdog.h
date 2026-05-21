// AquaControl — I2C bus watchdog
//
// A low-priority FreeRTOS task on Core 0 that pings every expected device
// every 5 s and tracks consecutive failures per device. Graduated recovery
// follows the plan (§6.2):
//   * 1 consecutive failure  → log warning
//   * 2 consecutive failures → log error, attempt I2C bus reset (deinit +
//     re-init) and immediately re-probe
//   * 3 consecutive failures → mark device faulted; raise a per-device
//     callback so the rest of the system (UI banner, MQTT, fish-safety
//     relay-off) can react.
//
// A device returning to ACK clears its fault state and emits a "recovered"
// callback.
//
// The bus reset step is intentionally cheap — it tears down and recreates
// the underlying `i2c_master_bus_handle_t` only; device handles registered
// by drivers continue to work because they keep a pointer to the shared
// I2CBus object, not the raw handle. (Driver-side reset of registered
// device handles is a Phase 3 concern.)
#pragma once

#include <cstdint>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "i2c_scanner.h"

namespace aqua::i2c {

struct WatchdogConfig {
    aqua::i2c::I2CBus* bus      = nullptr;
    uint32_t           interval_ms = 5000;
    // Re-init parameters used when a bus reset is triggered.
    i2c_port_t  port    = I2C_NUM_0;
    gpio_num_t  sda     = GPIO_NUM_NC;
    gpio_num_t  scl     = GPIO_NUM_NC;
    uint32_t    freq_hz = 400000;
};

struct WatchdogEvent {
    KnownDevice id;
    uint8_t     addr;
    bool        faulted;   // true = transitioning to fault, false = recovered
};

using WatchdogCallback = std::function<void(const WatchdogEvent&)>;

class Watchdog {
public:
    Watchdog() = default;
    ~Watchdog();

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    esp_err_t start(const WatchdogConfig& cfg, WatchdogCallback on_event);
    void      stop();

    // Snapshot: true if the device is currently marked faulted.
    bool is_faulted(KnownDevice id) const;

private:
    static void task_entry(void* arg);
    void        run();

    WatchdogConfig    cfg_   = {};
    WatchdogCallback  cb_    = nullptr;
    TaskHandle_t      task_  = nullptr;
    volatile bool     stop_  = false;

    uint8_t fail_count_[static_cast<size_t>(KnownDevice::COUNT)] = {};
    bool    faulted_   [static_cast<size_t>(KnownDevice::COUNT)] = {};
};

}  // namespace aqua::i2c
