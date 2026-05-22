// AquaControl — sys_cfg_api.h
//
// A-1: Unified SystemConfig mutator.
// Include this header ONLY in files that mutate SystemConfig (wizard.cpp,
// time_location_screen.cpp, web_portal.cpp, etc.).
// Do NOT include from app_config.h — that header is included by all components
// and must not pull in storage/system_config.h or <functional>.
#pragma once

#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "system_config.h"

// Declared in main.cpp alongside g_sys_cfg.
extern portMUX_TYPE g_sys_cfg_mux;

// Thread-safe mutator. Acquires g_sys_cfg_mux, applies mutation, persists to
// NVS. Safe to call from Core 0 or Core 1.
//
// Usage:
//   update_system_config(g_sys_cfg, [&](aqua::storage::SystemConfig& c) {
//       c.latitude       = new_lat;
//       c.longitude      = new_lon;
//       c.utc_offset_min = new_ofs;
//   });
inline void update_system_config(aqua::storage::SystemConfig& cfg,
                                  std::function<void(aqua::storage::SystemConfig&)> mutator) {
    taskENTER_CRITICAL(&g_sys_cfg_mux);
    mutator(cfg);
    taskEXIT_CRITICAL(&g_sys_cfg_mux);
    aqua::storage::save_system_config(cfg);
}
