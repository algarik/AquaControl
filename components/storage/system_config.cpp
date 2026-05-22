// AquaControl — SystemConfig NVS load/save
#include "system_config.h"

#include <cstdlib>
#include <cstring>

#include "ac_logger.h"
#include "cJSON.h"
#include "nvs_store.h"

namespace aqua::storage {

static const char* TAG = "sys_cfg";
static const char* KEY = "sys_cfg";

namespace {

// Small helpers to keep the parser readable.
template <typename T>
T get_number(const cJSON* obj, const char* key, T fallback) {
    cJSON* it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(it)) return fallback;
    return (T)it->valuedouble;
}

bool get_bool(const cJSON* obj, const char* key, bool fallback) {
    cJSON* it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it);
    if (cJSON_IsNumber(it)) return it->valuedouble != 0.0;
    return fallback;
}

std::string get_string(const cJSON* obj, const char* key, const char* fallback) {
    cJSON* it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(it) || !it->valuestring) return fallback ? fallback : "";
    return it->valuestring;
}

}  // namespace

esp_err_t load_system_config(SystemConfig* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    std::string blob;
    esp_err_t err = NvsStore::get_blob(KEY, &blob);
    if (err != ESP_OK) {
        AC_LOGW(TAG, "no saved config (%s); using defaults", esp_err_to_name(err));
        return err;
    }
    cJSON* root = cJSON_ParseWithLength(blob.data(), blob.size());
    if (!root) {
        AC_LOGE(TAG, "JSON parse failed; using defaults");
        return ESP_ERR_INVALID_STATE;
    }

    out->latitude              = get_number<float>   (root, "lat",        out->latitude);
    out->longitude             = get_number<float>   (root, "lon",        out->longitude);
    out->utc_offset_min        = get_number<int16_t> (root, "tz_min",     out->utc_offset_min);
    out->ntp1                  = get_string          (root, "ntp1",       out->ntp1.c_str());
    out->ntp2                  = get_string          (root, "ntp2",       out->ntp2.c_str());
    out->brightness_active_pct = get_number<uint8_t> (root, "br_act",     out->brightness_active_pct);
    out->brightness_dim_pct    = get_number<uint8_t> (root, "br_dim",     out->brightness_dim_pct);
    out->inactivity_timeout_s  = get_number<uint16_t>(root, "dim_s",      out->inactivity_timeout_s);
    out->language              = (Language)get_number<uint8_t>(root, "lang",      (uint8_t)out->language);
    out->temp_unit             = (TempUnit)get_number<uint8_t>(root, "tunit",     (uint8_t)out->temp_unit);
    out->relay_count           = get_number<uint8_t> (root, "relay_n",    out->relay_count);
    out->first_run_complete    = get_bool            (root, "first_done", out->first_run_complete);
    out->wifi_enabled           = get_bool            (root, "wifi_en",    out->wifi_enabled);
    out->mqtt_enabled           = get_bool            (root, "mqtt_en",    out->mqtt_enabled);
    out->ap_password           = get_string          (root, "ap_pw",      out->ap_password.c_str());
    out->heater_device_id      = get_number<uint8_t> (root, "heater_id",  out->heater_device_id);
    out->heater_fault_timeout_s= get_number<uint16_t>(root, "heater_tmo", out->heater_fault_timeout_s);
    out->heater_max_temp_c     = get_number<float>   (root, "heater_max", out->heater_max_temp_c);
    out->water_sensor_enabled  = get_bool            (root, "w_en",      out->water_sensor_enabled);
    out->ambient_sensor_enabled= get_bool            (root, "a_en",      out->ambient_sensor_enabled);
    out->water_sensor_addr     = get_number<uint8_t> (root, "w_addr",     out->water_sensor_addr);
    out->ambient_sensor_addr   = get_number<uint8_t> (root, "a_addr",     out->ambient_sensor_addr);
    out->water_cal_offset_c    = get_number<float>   (root, "w_cal",      out->water_cal_offset_c);
    out->ambient_cal_offset_c  = get_number<float>   (root, "a_cal",      out->ambient_cal_offset_c);
    out->screensaver_enabled   = get_bool            (root, "scrn_en",    out->screensaver_enabled);

    cJSON_Delete(root);
    AC_LOGI(TAG, "loaded: lat=%.4f lon=%.4f tz=%d lang=%u relays=%u first=%d",
            out->latitude, out->longitude, out->utc_offset_min,
            (unsigned)out->language, out->relay_count, out->first_run_complete);
    return ESP_OK;
}

esp_err_t save_system_config(const SystemConfig& cfg) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddNumberToObject(root, "lat",        cfg.latitude);
    cJSON_AddNumberToObject(root, "lon",        cfg.longitude);
    cJSON_AddNumberToObject(root, "tz_min",     cfg.utc_offset_min);
    cJSON_AddStringToObject(root, "ntp1",       cfg.ntp1.c_str());
    cJSON_AddStringToObject(root, "ntp2",       cfg.ntp2.c_str());
    cJSON_AddNumberToObject(root, "br_act",     cfg.brightness_active_pct);
    cJSON_AddNumberToObject(root, "br_dim",     cfg.brightness_dim_pct);
    cJSON_AddNumberToObject(root, "dim_s",      cfg.inactivity_timeout_s);
    cJSON_AddNumberToObject(root, "lang",       (uint8_t)cfg.language);
    cJSON_AddNumberToObject(root, "tunit",      (uint8_t)cfg.temp_unit);
    cJSON_AddNumberToObject(root, "relay_n",    cfg.relay_count);
    cJSON_AddBoolToObject  (root, "first_done", cfg.first_run_complete);
    cJSON_AddBoolToObject  (root, "wifi_en",    cfg.wifi_enabled);
    cJSON_AddBoolToObject  (root, "mqtt_en",    cfg.mqtt_enabled);
    cJSON_AddStringToObject(root, "ap_pw",      cfg.ap_password.c_str());
    cJSON_AddNumberToObject(root, "heater_id",  cfg.heater_device_id);
    cJSON_AddNumberToObject(root, "heater_tmo", cfg.heater_fault_timeout_s);
    cJSON_AddNumberToObject(root, "heater_max", cfg.heater_max_temp_c);
    cJSON_AddBoolToObject  (root, "w_en",       cfg.water_sensor_enabled);
    cJSON_AddBoolToObject  (root, "a_en",       cfg.ambient_sensor_enabled);
    cJSON_AddNumberToObject(root, "w_addr",     cfg.water_sensor_addr);
    cJSON_AddNumberToObject(root, "a_addr",     cfg.ambient_sensor_addr);
    cJSON_AddNumberToObject(root, "w_cal",      cfg.water_cal_offset_c);
    cJSON_AddNumberToObject(root, "a_cal",      cfg.ambient_cal_offset_c);
    cJSON_AddBoolToObject  (root, "scrn_en",    cfg.screensaver_enabled);

    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    esp_err_t err = NvsStore::set_blob(KEY, str, strlen(str));
    free(str);
    if (err == ESP_OK) AC_LOGI(TAG, "saved system_config");
    else               AC_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    return err;
}

}  // namespace aqua::storage
