// AquaControl — Project logging macros
// Wraps ESP-IDF ESP_LOGx so we can switch off DEBUG/VERBOSE in production
// builds without touching call sites.
//
// Compile-time level (override in component CMakeLists or sdkconfig if needed):
//   AC_LOG_LEVEL  =  AC_LOG_NONE | AC_LOG_ERROR | AC_LOG_WARN |
//                    AC_LOG_INFO | AC_LOG_DEBUG | AC_LOG_VERBOSE
//
// Defaults to AC_LOG_INFO. Set to AC_LOG_ERROR (or NONE) for release.
#pragma once

#include "esp_log.h"

#define AC_LOG_NONE     0
#define AC_LOG_ERROR    1
#define AC_LOG_WARN     2
#define AC_LOG_INFO     3
#define AC_LOG_DEBUG    4
#define AC_LOG_VERBOSE  5

#ifndef AC_LOG_LEVEL
#define AC_LOG_LEVEL AC_LOG_INFO
#endif

#if AC_LOG_LEVEL >= AC_LOG_ERROR
#define AC_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#else
#define AC_LOGE(tag, fmt, ...) ((void)0)
#endif

#if AC_LOG_LEVEL >= AC_LOG_WARN
#define AC_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#else
#define AC_LOGW(tag, fmt, ...) ((void)0)
#endif

#if AC_LOG_LEVEL >= AC_LOG_INFO
#define AC_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#else
#define AC_LOGI(tag, fmt, ...) ((void)0)
#endif

#if AC_LOG_LEVEL >= AC_LOG_DEBUG
#define AC_LOGD(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#else
#define AC_LOGD(tag, fmt, ...) ((void)0)
#endif

#if AC_LOG_LEVEL >= AC_LOG_VERBOSE
#define AC_LOGV(tag, fmt, ...) ESP_LOGV(tag, fmt, ##__VA_ARGS__)
#else
#define AC_LOGV(tag, fmt, ...) ((void)0)
#endif
