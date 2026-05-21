// AquaControl — UI dependency context (Phase 4).
//
// UI screens need access to several backend singletons (device list, trigger
// list, system config). Rather than each screen reaching into globals that
// live in main.cpp, we hand them a single `UiContext` struct populated by
// main during boot. Screens read from this struct only; never write to the
// pointers themselves.
#pragma once

namespace aqua::devices  { class DeviceManager; }
namespace aqua::triggers { class TriggerManager; }
namespace aqua::storage  { struct SystemConfig; }
namespace aqua::drivers  { class Pcf8575; class Pca9685; }

namespace aqua::ui {

struct UiContext {
    aqua::devices::DeviceManager*   devices  = nullptr;
    aqua::triggers::TriggerManager* triggers = nullptr;
    aqua::storage::SystemConfig*    sys_cfg  = nullptr;
    aqua::drivers::Pcf8575*         drv_pcf  = nullptr;
    aqua::drivers::Pca9685*         drv_pca  = nullptr;
};

// Install the global context. Must be called before any screen builder runs.
void set_ui_context(const UiContext& ctx);
const UiContext& ui_context();

}  // namespace aqua::ui
