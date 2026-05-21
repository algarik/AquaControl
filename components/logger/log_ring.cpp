// AquaControl — In-RAM log ring implementation.
#include "log_ring.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace aqua::log_ring {

static constexpr size_t kMaxLines    = 50;
static constexpr size_t kMaxLineLen  = 220;

static bool                s_inited = false;
static SemaphoreHandle_t   s_mtx    = nullptr;
static vprintf_like_t      s_chain  = nullptr;

// Ring buffer of std::string. Newest at the back.
static std::string         s_buffer[kMaxLines];
static size_t              s_count = 0;
static size_t              s_head  = 0;   // next write slot

namespace {

void push_line(const char* line) {
    if (!line || !*line) return;
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(10)) != pdTRUE) return;
    s_buffer[s_head] = line;
    s_head = (s_head + 1) % kMaxLines;
    if (s_count < kMaxLines) ++s_count;
    xSemaphoreGive(s_mtx);
}

int tee_vprintf(const char* fmt, va_list args) {
    // Render once to a fixed-size buffer.
    va_list args2;
    va_copy(args2, args);
    char tmp[kMaxLineLen];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args2);
    va_end(args2);

    // Strip trailing CR/LF for ring storage; keep raw for stdout.
    if (n > 0) {
        char clean[kMaxLineLen];
        size_t len = (size_t)((n >= (int)sizeof(tmp)) ? sizeof(tmp) - 1 : n);
        size_t out = 0;
        for (size_t i = 0; i < len && out < sizeof(clean) - 1; ++i) {
            if (tmp[i] != '\r' && tmp[i] != '\n') clean[out++] = tmp[i];
        }
        clean[out] = '\0';
        if (out > 0) push_line(clean);
    }

    // Forward to the original sink (UART).
    if (s_chain) {
        return s_chain(fmt, args);
    }
    return vprintf(fmt, args);
}

}  // namespace

void init() {
    if (s_inited) return;
    s_mtx   = xSemaphoreCreateMutex();
    s_chain = esp_log_set_vprintf(&tee_vprintf);
    s_inited = true;
}

std::vector<std::string> snapshot() {
    std::vector<std::string> out;
    if (!s_inited) return out;
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(50)) != pdTRUE) return out;
    out.reserve(s_count);
    // Oldest is at (s_head - s_count + kMaxLines) % kMaxLines.
    size_t start = (s_head + kMaxLines - s_count) % kMaxLines;
    for (size_t i = 0; i < s_count; ++i) {
        out.push_back(s_buffer[(start + i) % kMaxLines]);
    }
    xSemaphoreGive(s_mtx);
    std::reverse(out.begin(), out.end());
    return out;
}

void clear() {
    if (!s_inited) return;
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(50)) != pdTRUE) return;
    for (auto& s : s_buffer) s.clear();
    s_count = 0;
    s_head  = 0;
    xSemaphoreGive(s_mtx);
}

}  // namespace aqua::log_ring
