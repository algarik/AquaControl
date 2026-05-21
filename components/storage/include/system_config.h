// AquaControl — System configuration (Phase 3.5 Block B6)
//
// Single struct that holds every persistent user preference outside of the
// device/trigger lists: location, timezone, NTP servers, display dim, UI
// language, temperature unit, relay count and first-run flag.
//
// Stored in NVS namespace "aquactl" as one JSON blob under key "sys_cfg".
// The blob form lets us atomically update related fields and keeps the
// per-key NVS quota tidy.
#pragma once

#include <cstdint>
#include <string>

#include "esp_err.h"

namespace aqua::storage {

enum class Language : uint8_t { EN = 0, RU = 1 };
enum class TempUnit : uint8_t { CELSIUS = 0, FAHRENHEIT = 1 };

struct SystemConfig {
    // Location (for SolarCalculator). Defaults to Smolensk, Russia.
    float    latitude  = 54.7826f;
    float    longitude = 32.0453f;

    // Timezone offset from UTC in minutes (e.g. UTC+03:00 = 180).
    int16_t  utc_offset_min = 180;   // UTC+3 Moscow time (matches Smolensk default location)

    // NTP servers.
    std::string ntp1 = "pool.ntp.org";
    std::string ntp2 = "time.google.com";

    // Display.
    uint8_t  brightness_active_pct = 80;
    uint8_t  brightness_dim_pct    = 15;
    uint16_t inactivity_timeout_s  = 120;  // 0 = never dim

    // Locale.
    Language language  = Language::EN;
    TempUnit temp_unit = TempUnit::CELSIUS;

    // Hardware sizing.
    uint8_t  relay_count = 5;   // active PCF8575 channels (1..16)

    // Wizard state.
    bool     first_run_complete = false;

    // WiFi on/off master switch.
    bool     wifi_enabled = true;

    // MQTT on/off master switch.
    bool     mqtt_enabled = false;

    // AP fallback password (random 8 chars; generated on first boot).
    std::string ap_password;

    // Heater safety (0 = feature disabled).
    uint8_t  heater_device_id       = 0;     // device ID of the heater relay (0 = off)
    uint16_t heater_fault_timeout_s = 1800;  // 30 min: how long heater ON without temp rise = fault

    // Sensor enable flags (can be disabled if sensor not physically present).
    bool     water_sensor_enabled   = true;
    bool     ambient_sensor_enabled = true;

    // Sensor address overrides (0 = Auto: use I2C scan result at boot).
    uint8_t  water_sensor_addr   = 0;    // 0 = Auto, 0x44, or 0x45
    uint8_t  ambient_sensor_addr = 0;    // 0 = Auto, 0x44, or 0x45

    // Calibration offsets applied to smoothed sensor readings (°C).
    float    water_cal_offset_c   = 0.0f;
    float    ambient_cal_offset_c = 0.0f;
};

// Load configuration from NVS. Missing keys leave defaults in place; missing
// blob entirely is reported via ESP_ERR_NVS_NOT_FOUND but `out` still
// receives sensible defaults so the rest of boot can proceed.
esp_err_t load_system_config(SystemConfig* out);

// Persist configuration. Always overwrites the entire blob.
esp_err_t save_system_config(const SystemConfig& cfg);

}  // namespace aqua::storage
