// AquaControl — Copy-pasteable diagnostic dump helpers
//
// These macros emit a clearly-delimited block to the serial monitor that the
// user can copy verbatim into a bug report. The block bypasses the ESP-IDF
// log tag/level prefixes — it goes straight to `printf`, surrounded by easily
// greppable markers:
//
//     ===== AC-DUMP BEGIN: <section> =====
//     ...lines...
//     ===== AC-DUMP END:   <section> =====
//
// Usage:
//     AC_DUMP_BEGIN("boot-summary");
//     AC_DUMP("Firmware:     %s v%s", AC_FIRMWARE_NAME, AC_FIRMWARE_VERSION);
//     AC_DUMP("Free heap:    %u", esp_get_free_heap_size());
//     AC_DUMP_END("boot-summary");
//
// Gated by `AC_DEBUG_DUMP_ENABLED` (declared in app_config.h, default 1).
// Set to 0 for release builds and every macro compiles to a no-op.
#pragma once

#include <cstdio>

#include "app_config.h"

#if AC_DEBUG_DUMP_ENABLED

#define AC_DUMP_BEGIN(section)                                                \
    do {                                                                      \
        printf("\n===== AC-DUMP BEGIN: %s =====\n", (section));               \
    } while (0)

#define AC_DUMP(fmt, ...)                                                     \
    do {                                                                      \
        printf("  " fmt "\n", ##__VA_ARGS__);                                 \
    } while (0)

#define AC_DUMP_END(section)                                                  \
    do {                                                                      \
        printf("===== AC-DUMP END:   %s =====\n\n", (section));               \
    } while (0)

#else  // AC_DEBUG_DUMP_ENABLED == 0

#define AC_DUMP_BEGIN(section) ((void)0)
#define AC_DUMP(fmt, ...)      ((void)0)
#define AC_DUMP_END(section)   ((void)0)

#endif
