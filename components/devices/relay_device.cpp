#include "relay_device.h"

#include "ac_logger.h"

namespace aqua::devices {

static const char* TAG = "Relay";

void RelayDevice::apply(bool active, bool force) {
    if (!enabled || expander_ == nullptr) return;
    if (!force && current_active_.load(std::memory_order_relaxed) == active) return;
    // Invert output signal for active-low relay modules.
    bool pin_state = active_high ? active : !active;
    esp_err_t err = expander_->set_channel(channel_, pin_state);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "%s ch%u set_channel(%d) failed: %s",
                name.c_str(), channel_, (int)pin_state, esp_err_to_name(err));
        return;
    }
    current_active_.store(active, std::memory_order_relaxed);
    AC_LOGI(TAG, "%s (id=%u, ch=%u, active_high=%d) -> %s%s",
            name.c_str(), id, channel_, (int)active_high,
            active ? "ON" : "OFF",
            force ? " [force]" : "");
}

}  // namespace aqua::devices
