// AquaControl — Network settings screen (Phase 4, Slice 3).
//
// Allows the user to:
//  * Enter Wi-Fi SSID and password (keyboard popup).
//  * See the current Wi-Fi connection status and IP address.
//  * Enter MQTT broker URI, user, password, base topic.
//  * Toggle HA MQTT discovery.
//  * Save — persists credentials to NVS and reconnects.
#pragma once

#include "lvgl.h"

namespace aqua::ui::network_settings_screen {

// Build and return a fresh screen root. Ownership is LVGL's; the screen
// tears itself down on LV_EVENT_DELETE.
lv_obj_t* build();

}  // namespace aqua::ui::network_settings_screen
