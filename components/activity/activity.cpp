// AquaControl — Activity tracker implementation
#include "activity.h"

#include <atomic>

#include "esp_timer.h"

namespace aqua::activity {

namespace {
std::atomic<uint64_t> g_last_ms{0};
}

void notify() {
    g_last_ms.store((uint64_t)esp_timer_get_time() / 1000ULL,
                    std::memory_order_relaxed);
}

uint64_t last_input_ms() {
    return g_last_ms.load(std::memory_order_relaxed);
}

uint64_t idle_ms() {
    uint64_t last = last_input_ms();
    if (last == 0) return UINT64_MAX;
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000ULL;
    return (now > last) ? (now - last) : 0;
}

}  // namespace aqua::activity
