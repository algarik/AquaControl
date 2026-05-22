// AquaControl — Captive HTTP portal served when Wi-Fi is in AP mode.
//
// Provides a single-page UI accessible at http://192.168.4.1 that lets the
// user configure station credentials and monitor live sensor/device state
// without needing the touchscreen.
//
// Endpoints:
//   GET  /            Single-page HTML: Wi-Fi setup form + live status
//   GET  /status.json JSON snapshot of sensors + device states
//   POST /wifi        Save SSID/password and restart in STA mode
//   POST /device      Toggle a device  { "id": N, "on": true|false }
//
// Lifecycle:
//   start() — call when AP fallback is active (safe from any task)
//   stop()  — call when STA connects or WiFi is disabled
//   stop_deferred() — safe to call from the WiFi event loop task (low stack);
//                     defers httpd_stop() via a one-shot esp_timer.
#pragma once

#include "device_manager.h"
#include "esp_err.h"

namespace aqua::web_portal {

struct Context {
    aqua::devices::DeviceManager* devices;  // may be nullptr
};

esp_err_t start(const Context& ctx);
void      stop();
void      stop_deferred();  // safe from event-loop task; fires after 100 ms
bool      is_running();

}  // namespace aqua::web_portal
