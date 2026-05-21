#include "nvs_store.h"

#include <vector>

#include "ac_logger.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace aqua::storage {

static const char* TAG = "NvsStore";
static constexpr const char* kNamespace = "aquactl";

bool NvsStore::s_initialised = false;
bool NvsStore::s_erased      = false;

bool NvsStore::was_erased() { return s_erased; }

// --- helpers -----------------------------------------------------------------

namespace {

esp_err_t open_rw(nvs_handle_t* h) {
    return nvs_open(kNamespace, NVS_READWRITE, h);
}

esp_err_t open_ro(nvs_handle_t* h) {
    return nvs_open(kNamespace, NVS_READONLY, h);
}

}  // namespace

// --- init --------------------------------------------------------------------

esp_err_t NvsStore::init() {
    if (s_initialised) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        AC_LOGW(TAG, "NVS needs erase (%s) — all saved config will be lost",
                esp_err_to_name(err));
        nvs_flash_erase();
        s_erased = true;
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        AC_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_initialised = true;
    AC_LOGI(TAG, "NVS ready (namespace=%s)", kNamespace);
    return ESP_OK;
}

// --- primitive getters -------------------------------------------------------

#define GET_PRIM(suffix, type, nvs_fn)                                          \
    esp_err_t NvsStore::get_##suffix(const char* key, type* out) {              \
        nvs_handle_t h;                                                         \
        esp_err_t err = open_ro(&h);                                            \
        if (err != ESP_OK) return err;                                          \
        type tmp = 0;                                                           \
        err = nvs_fn(h, key, &tmp);                                             \
        nvs_close(h);                                                           \
        if (err == ESP_OK && out) *out = tmp;                                   \
        return err;                                                             \
    }

GET_PRIM(u8,  uint8_t,  nvs_get_u8)
GET_PRIM(u16, uint16_t, nvs_get_u16)
GET_PRIM(u32, uint32_t, nvs_get_u32)
GET_PRIM(i32, int32_t,  nvs_get_i32)

#undef GET_PRIM

// --- primitive setters -------------------------------------------------------

#define SET_PRIM(suffix, type, nvs_fn)                                          \
    esp_err_t NvsStore::set_##suffix(const char* key, type v) {                 \
        nvs_handle_t h;                                                         \
        esp_err_t err = open_rw(&h);                                            \
        if (err != ESP_OK) return err;                                          \
        err = nvs_fn(h, key, v);                                                \
        if (err == ESP_OK) err = nvs_commit(h);                                 \
        nvs_close(h);                                                           \
        if (err != ESP_OK) AC_LOGE(TAG, "set_%s(%s): %s",                       \
                                   #suffix, key, esp_err_to_name(err));        \
        return err;                                                             \
    }

SET_PRIM(u8,  uint8_t,  nvs_set_u8)
SET_PRIM(u16, uint16_t, nvs_set_u16)
SET_PRIM(u32, uint32_t, nvs_set_u32)
SET_PRIM(i32, int32_t,  nvs_set_i32)

#undef SET_PRIM

// --- strings -----------------------------------------------------------------

esp_err_t NvsStore::get_str(const char* key, std::string* out) {
    nvs_handle_t h;
    esp_err_t err = open_ro(&h);
    if (err != ESP_OK) return err;
    size_t len = 0;
    err = nvs_get_str(h, key, nullptr, &len);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    std::string buf(len, '\0');
    err = nvs_get_str(h, key, buf.data(), &len);
    nvs_close(h);
    if (err == ESP_OK && out) {
        // NVS includes the trailing NUL in len; trim it from std::string size.
        if (!buf.empty() && buf.back() == '\0') buf.pop_back();
        *out = std::move(buf);
    }
    return err;
}

esp_err_t NvsStore::set_str(const char* key, const std::string& v) {
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, v.c_str());
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) AC_LOGE(TAG, "set_str(%s): %s", key, esp_err_to_name(err));
    return err;
}

// --- blobs -------------------------------------------------------------------

esp_err_t NvsStore::get_blob(const char* key, std::string* out) {
    nvs_handle_t h;
    esp_err_t err = open_ro(&h);
    if (err != ESP_OK) return err;
    size_t len = 0;
    err = nvs_get_blob(h, key, nullptr, &len);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    std::string buf(len, '\0');
    err = nvs_get_blob(h, key, buf.data(), &len);
    nvs_close(h);
    if (err == ESP_OK && out) *out = std::move(buf);
    return err;
}

esp_err_t NvsStore::set_blob(const char* key, const void* data, size_t len) {
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, key, data, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) AC_LOGE(TAG, "set_blob(%s): %s", key, esp_err_to_name(err));
    return err;
}

// --- erase -------------------------------------------------------------------

esp_err_t NvsStore::erase(const char* key) {
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

}  // namespace aqua::storage
