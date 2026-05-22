// AquaControl — device abstraction layer (Phase 3).
//
// IDevice is the polymorphic base used by the scheduler. Concrete devices
// translate the boolean "active" command coming from triggers into the
// physical action (relay GPIO, PWM channel ramp, RGB colour application).
//
// Overrides: a device may be manually overridden by the user. While an
// override is active, the scheduler's evaluation result is ignored.
#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace aqua::devices {

enum class DeviceType : uint8_t {
    RELAY = 0,
    PWM   = 1,
    RGB   = 2,
};

// Semantic role of a device — tells the scheduler and UI what physical
// function this device performs.  The role is stored alongside the device
// and controls safety rules (e.g. heater shuts off if water sensor fails).
enum class DeviceRole : uint8_t {
    GENERIC = 0,  // No special behaviour (default)
    HEATER  = 1,  // Heating element — shuts off if water sensor fails
    PUMP    = 2,  // Water pump / circulation
    LIGHT   = 3,  // Illumination (incl. UV)
    FAN     = 4,  // Ventilation / cooling fan
    CO2     = 5,  // CO2 solenoid valve
    DOSER   = 6,  // Dosing pump
    UV      = 7,  // UV steriliser
};

enum class OverrideMode : uint8_t {
    NONE        = 0,  // No override active
    UNTIL_NEXT  = 1,  // Cleared by next scheduler-driven state change
    TIMED       = 2,  // Cleared after `override_until_epoch_` (UTC) is reached
    INDEFINITE  = 3,  // Persists until user clears
};

// A-2: Component health check API.
// Each hardware-owning device optionally overrides health() to report whether
// its underlying hardware is responding. The I2C watchdog and scheduler can
// call health() and raise typed faults via faults::raise().
struct HealthStatus {
    bool           ok         = true;   // true = hardware responding normally
    const char*    fault_msg  = nullptr; // human-readable fault description (static string)
    uint16_t       fault_code = 0;       // aqua::faults fault code (0 = no fault)
};

class IDevice {
public:
    IDevice(uint8_t id, std::string name) : id(id), name(std::move(name)) {}
    virtual ~IDevice() = default;

    IDevice(const IDevice&) = delete;
    IDevice& operator=(const IDevice&) = delete;

    // Identity & user-facing settings
    uint8_t     id;
    std::string name;
    bool        enabled = true;
    DeviceRole  role    = DeviceRole::GENERIC;  // semantic function

    // ID of the trigger that most recently drove this device active (0 = none
    // or no trigger). Maintained by the scheduler; used by the UI to show
    // "active trigger" on each device card.
    uint8_t     last_driver_trigger_id = 0;

    // Trigger engine entry point. `active` is the OR of all linked triggers.
    // Implementations decide what to physically do (turn on relay, ramp PWM,
    // etc.) and update current_active_.
    // `force` bypasses the current_active_ == active early-return so the
    // full-pass safety net re-drives hardware even when cached state matches.
    virtual void apply(bool active, bool force = false) = 0;

    // Analog output path for TEMP_MAP triggers.
    // `t` is in [0.0, 1.0]: 0 = lo setpoint, 1 = hi setpoint.
    // Default implementation is a no-op (devices that don't support analog
    // output ignore the call).  Implementations should respect has_override().
    virtual void apply_analog(float /*t*/) {}

    // What type am I (used by UI and serialisation).
    virtual DeviceType get_type() const = 0;

    // A-2: Report hardware health. Default = always healthy.
    // Concrete devices that can probe their hardware (relay I/O expander,
    // PWM driver, etc.) should override this and return a non-ok status if
    // the last I2C probe failed.
    virtual HealthStatus health() const { return {}; }

    // Current commanded active state (after override / fade tracking).
    // Atomic because Core 0 (scheduler/apply) writes and Core 1 (dashboard,
    // device_detail_screen) reads concurrently.
    bool current_active() const {
        return current_active_.load(std::memory_order_relaxed);
    }

    // --- Manual override -----------------------------------------------------
    // Apply a manual override. `target_active` is what the user wants; for
    // TIMED mode `until_epoch_utc` is when the override should auto-clear.
    void set_override(OverrideMode mode, bool target_active,
                      time_t until_epoch_utc = 0);
    void clear_override();
    bool has_override() const { return override_mode_ != OverrideMode::NONE; }
    OverrideMode override_mode() const { return override_mode_; }
    bool override_target() const { return override_target_; }
    time_t override_until_epoch_utc() const { return override_until_epoch_; }

    // Called once per scheduler full-evaluation cycle. Handles TIMED expiry
    // and UNTIL_NEXT clearing when the new desired_active differs from the
    // overridden target. Returns the effective active state to apply.
    bool resolve_active(bool desired_active);

protected:
    // Atomic: written on Core 0 (scheduler → apply()), read on Core 1 (UI).
    std::atomic<bool> current_active_{false};

private:
    // Spinlock protecting override_mode_ / override_target_ / override_until_epoch_.
    // Used as a critical-section guard between Core 0 resolve_active() reads
    // and Core 1 set_override() / clear_override() writes.
    mutable portMUX_TYPE override_mux_ = portMUX_INITIALIZER_UNLOCKED;

    OverrideMode override_mode_         = OverrideMode::NONE;
    bool         override_target_       = false;
    time_t       override_until_epoch_  = 0;
};

}  // namespace aqua::devices
