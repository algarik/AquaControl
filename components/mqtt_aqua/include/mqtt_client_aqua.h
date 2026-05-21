// AquaControl — MQTT client (Phase 3.5 Block D14).
//
// Thin wrapper over esp-mqtt that:
//   * connects with exponential backoff (managed by the underlying client)
//   * publishes per-device state as JSON on `<base>/devices/<id>/state`
//   * subscribes to `<base>/devices/<id>/cmd` for ON/OFF override commands
//   * publishes Home Assistant MQTT-discovery config when `ha_discovery` is
//     true (all devices as "switch" component — most reliable for JSON state)
//
// State publish is fire-and-forget. The scheduler calls publish_device_state
// after every apply(). Command callback is invoked from the mqtt task —
// it is the caller's responsibility to marshal back onto the main task if
// needed.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "esp_err.h"

namespace aqua::devices { class IDevice; }

namespace aqua::mqtt {

struct Config {
    std::string uri;           // e.g. "mqtt://192.168.1.10:1883"
    std::string user;
    std::string pass;
    std::string base_topic;    // e.g. "aquacontrol"
    std::string node_id;       // unique device id for HA grouping (derived from MAC if empty)
    bool        ha_discovery = true;
};

// command_cb(device_id, payload) where payload is one of "ON"/"OFF".
using CommandCallback = std::function<void(uint8_t /*dev_id*/,
                                           const std::string& /*payload*/)>;

// connect_cb() is called each time the MQTT client connects (including
// reconnects). Useful for re-publishing HA discovery after broker restart.
using ConnectCallback = std::function<void()>;

esp_err_t start(const Config& cfg);
void      stop();
bool      connected();

// Persist / load Config to / from NVS.
// Persists uri, user, pass, base_topic and ha_discovery flag.
esp_err_t save_config(const Config& cfg);
esp_err_t load_config(Config* out);  // ESP_ERR_NVS_NOT_FOUND if absent

void set_command_callback(CommandCallback cb);
void set_connect_callback(ConnectCallback cb);

esp_err_t publish_device_state(const aqua::devices::IDevice& dev);
esp_err_t publish_sensor(uint8_t sensor_id, float temp_c, float humid_pct);
esp_err_t publish_ha_discovery(const aqua::devices::IDevice& dev);
esp_err_t publish_ha_sensor_discovery(uint8_t sensor_id, const char* name);

// Publish a system-level status snapshot (uptime, heap, WiFi, fault count).
// Topic: <base>/status  — retained, QoS 1.
esp_err_t publish_system_status(uint32_t uptime_s, uint32_t heap_free,
                                bool wifi_connected, int8_t wifi_rssi,
                                const char* wifi_ip, uint16_t active_faults);

// Publish a generic event (non-retained, QoS 0).
// Topic: <base>/event  — useful for boot, WiFi loss, fault raise/clear.
esp_err_t publish_event(const char* type, const char* detail);

// Publish HA discovery config for system status sensors (uptime, RSSI, IP,
// fault count). Call once after connect, e.g. inside the connect_callback.
esp_err_t publish_ha_system_discovery();

}  // namespace aqua::mqtt
