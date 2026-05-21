// AquaControl — type-safe NVS wrapper.
//
// All AquaControl configuration lives in NVS namespace `aquactl`. This
// wrapper centralises namespace handling, error logging and conversion to
// std::string so the rest of the codebase never touches the raw
// `nvs_handle_t` API.
//
// Usage:
//     aqua::storage::NvsStore::init();
//     uint8_t relays = 5;
//     aqua::storage::NvsStore::get_u8("relay_count", &relays);
//     aqua::storage::NvsStore::set_u8("relay_count", 8);
//
// Phase 3: numeric + blob/string getters/setters. Phase 5/6 may add
// commit-tracking and key enumeration.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_err.h"

namespace aqua::storage {

class NvsStore {
public:
    // Initialise the NVS partition exactly once. Safe to call repeatedly.
    // When a partition erase was required (NVS_NO_FREE_PAGES or
    // NVS_NEW_VERSION_FOUND), was_erased() returns true so the caller can
    // record the event in the history log after SPIFFS is available.
    static esp_err_t init();
    static bool was_erased();

    // Read / write primitive integers. Return ESP_ERR_NVS_NOT_FOUND if the
    // key does not exist; *out is left untouched in that case.
    static esp_err_t get_u8 (const char* key, uint8_t*  out);
    static esp_err_t get_u16(const char* key, uint16_t* out);
    static esp_err_t get_u32(const char* key, uint32_t* out);
    static esp_err_t get_i32(const char* key, int32_t*  out);

    static esp_err_t set_u8 (const char* key, uint8_t  v);
    static esp_err_t set_u16(const char* key, uint16_t v);
    static esp_err_t set_u32(const char* key, uint32_t v);
    static esp_err_t set_i32(const char* key, int32_t  v);

    // Strings. get_str returns ESP_ERR_NVS_NOT_FOUND if the key is missing.
    static esp_err_t get_str(const char* key, std::string* out);
    static esp_err_t set_str(const char* key, const std::string& v);

    // Variable-length blobs (e.g. JSON config). get_blob returns
    // ESP_ERR_NVS_NOT_FOUND if missing.
    static esp_err_t get_blob(const char* key, std::string* out);
    static esp_err_t set_blob(const char* key, const void* data, size_t len);

    // Delete a key. Returns ESP_OK on success or if the key didn't exist.
    static esp_err_t erase(const char* key);

private:
    static bool s_initialised;
    static bool s_erased;
};

}  // namespace aqua::storage
