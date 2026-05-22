// AquaControl — Device / Trigger persistence implementation.
#include "config_storage.h"

#include <cstdlib>
#include <cstring>
#include <memory>

#include "ac_logger.h"
#include "cJSON.h"
#include "color_utils.h"
#include "nvs_store.h"
#include "pca9685.h"
#include "pcf8575.h"
#include "pwm_device.h"
#include "relay_device.h"
#include "rgb_device.h"
#include "schedule_trigger.h"
#include "solar_trigger.h"
#include "temp_map_trigger.h"
#include "temp_trigger.h"

namespace aqua::storage {

using aqua::devices::DeviceManager;
using aqua::devices::DeviceType;
using aqua::devices::IDevice;
using aqua::devices::PwmDevice;
using aqua::devices::RelayDevice;
using aqua::devices::RgbDevice;
using aqua::triggers::ITrigger;
using aqua::triggers::ScheduleTrigger;
using aqua::triggers::SolarEvent;
using aqua::triggers::SolarTrigger;
using aqua::triggers::TempCondition;
using aqua::triggers::TempMapTrigger;
using aqua::triggers::TempTrigger;
using aqua::triggers::TriggerManager;
using aqua::triggers::TriggerType;

static const char* TAG = "cfg_store";
static const char* KEY_DEV  = "dev_list";
static const char* KEY_TRIG = "trig_list";

// -----------------------------------------------------------------------------
// Small JSON helpers.
// -----------------------------------------------------------------------------
namespace {

template <typename T>
T num(const cJSON* obj, const char* key, T fallback) {
    cJSON* it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(it)) return fallback;
    return (T)it->valuedouble;
}

bool boolv(const cJSON* obj, const char* key, bool fallback) {
    cJSON* it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(it))    return cJSON_IsTrue(it);
    if (cJSON_IsNumber(it))  return it->valuedouble != 0.0;
    return fallback;
}

const char* strv(const cJSON* obj, const char* key, const char* fallback) {
    cJSON* it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(it) || !it->valuestring) return fallback;
    return it->valuestring;
}

cJSON* add_common_device(cJSON* root, const IDevice& d) {
    cJSON_AddNumberToObject(root, "id",      d.id);
    cJSON_AddStringToObject(root, "name",    d.name.c_str());
    cJSON_AddNumberToObject(root, "type",    (uint8_t)d.get_type());
    cJSON_AddBoolToObject  (root, "enabled", d.enabled);
    cJSON_AddNumberToObject(root, "role",    (uint8_t)d.role);
    return root;
}

cJSON* add_common_trigger(cJSON* root, const ITrigger& t) {
    cJSON_AddNumberToObject(root, "id",      t.id);
    cJSON_AddStringToObject(root, "name",    t.name.c_str());
    cJSON_AddNumberToObject(root, "type",    (uint8_t)t.get_type());
    cJSON_AddBoolToObject  (root, "enabled", t.enabled);
    cJSON* arr = cJSON_AddArrayToObject(root, "links");
    for (uint8_t did : t.linked_device_ids) {
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(did));
    }
    return root;
}

}  // namespace

// -----------------------------------------------------------------------------
// Devices.
// -----------------------------------------------------------------------------
esp_err_t save_devices(DeviceManager& dm) {
    cJSON* root = cJSON_CreateArray();
    if (!root) return ESP_ERR_NO_MEM;

    for (const auto& up : dm.all()) {
        const IDevice* d = up.get();
        cJSON* obj = cJSON_CreateObject();
        add_common_device(obj, *d);
        switch (d->get_type()) {
            case DeviceType::RELAY: {
                const auto* r = static_cast<const RelayDevice*>(d);
                cJSON_AddNumberToObject(obj, "ch", r->channel());
                cJSON_AddBoolToObject  (obj, "active_high", r->active_high);
                break;
            }
            case DeviceType::PWM: {
                const auto* p = static_cast<const PwmDevice*>(d);
                cJSON_AddNumberToObject(obj, "ch",       p->channel());
                cJSON_AddNumberToObject(obj, "level",    p->level_pct);
                cJSON_AddNumberToObject(obj, "level_lo", p->level_lo_pct);
                cJSON_AddNumberToObject(obj, "fade_in",  p->fade_in_min);
                cJSON_AddNumberToObject(obj, "fade_out", p->fade_out_min);
                break;
            }
            case DeviceType::RGB: {
                const auto* g = static_cast<const RgbDevice*>(d);
                cJSON_AddNumberToObject(obj, "ch",   g->base_channel());
                cJSON_AddNumberToObject(obj, "h",    g->color_hsv.h);
                cJSON_AddNumberToObject(obj, "s",    g->color_hsv.s);
                cJSON_AddNumberToObject(obj, "v",    g->color_hsv.v);
                cJSON_AddNumberToObject(obj, "hlo",  g->color_lo_hsv.h);
                cJSON_AddNumberToObject(obj, "slo",  g->color_lo_hsv.s);
                cJSON_AddNumberToObject(obj, "vlo",  g->color_lo_hsv.v);
                cJSON_AddNumberToObject(obj, "fade_in",  g->fade_in_min);
                cJSON_AddNumberToObject(obj, "fade_out", g->fade_out_min);
                break;
            }
        }
        cJSON_AddItemToArray(root, obj);
    }

    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    esp_err_t err = NvsStore::set_blob(KEY_DEV, str, strlen(str));
    cJSON_free(str);  // L-3: use cJSON_free for cJSON-allocated strings
    AC_LOGI(TAG, "save_devices: %u entries (%s)",
            (unsigned)dm.size(), esp_err_to_name(err));
    return err;
}

esp_err_t load_devices(DeviceManager& dm, const DriverContext& ctx) {
    std::string blob;
    esp_err_t err = NvsStore::get_blob(KEY_DEV, &blob);
    if (err != ESP_OK) {
        AC_LOGW(TAG, "load_devices: no blob (%s)", esp_err_to_name(err));
        return err;
    }
    cJSON* root = cJSON_ParseWithLength(blob.data(), blob.size());
    if (!cJSON_IsArray(root)) {
        AC_LOGE(TAG, "load_devices: bad JSON");
        if (root) cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        uint8_t  id      = num<uint8_t>(item, "id", 0);
        uint8_t  type    = num<uint8_t>(item, "type", 0);
        bool     enabled = boolv(item, "enabled", true);
        std::string name = strv(item, "name", "");

        std::unique_ptr<IDevice> dev;
        switch ((DeviceType)type) {
            case DeviceType::RELAY: {
                uint8_t ch = num<uint8_t>(item, "ch", 0);
                auto r = std::make_unique<RelayDevice>(id, std::move(name), ctx.pcf, ch);
                r->active_high = boolv(item, "active_high", true);
                dev = std::move(r);
                break;
            }
            case DeviceType::PWM: {
                uint8_t ch = num<uint8_t>(item, "ch", 0);
                auto p = std::make_unique<PwmDevice>(id, std::move(name), ctx.pca, ch);
                p->level_pct    = num<uint8_t> (item, "level",    100);
                p->level_lo_pct = num<uint8_t> (item, "level_lo", 0);
                p->fade_in_min  = num<uint16_t>(item, "fade_in",  0);
                p->fade_out_min = num<uint16_t>(item, "fade_out", 0);
                dev = std::move(p);
                break;
            }
            case DeviceType::RGB: {
                uint8_t ch = num<uint8_t>(item, "ch", 0);
                auto g = std::make_unique<RgbDevice>(id, std::move(name), ctx.pca, ch);
                // Backward-compat: if old "r" key exists, migrate from RGB+bright.
                if (cJSON_GetObjectItemCaseSensitive(item, "h")) {
                    g->color_hsv.h = num<float>(item, "h", 0.0f);
                    g->color_hsv.s = num<float>(item, "s", 0.0f);
                    g->color_hsv.v = num<float>(item, "v", 1.0f);
                } else {
                    // Legacy r/g/b/bright → convert.
                    uint8_t r     = num<uint8_t>(item, "r",      255);
                    uint8_t gr    = num<uint8_t>(item, "g",      255);
                    uint8_t b     = num<uint8_t>(item, "b",      255);
                    uint8_t brt   = num<uint8_t>(item, "bright", 100);
                    aqua::devices::Hsv hsv = aqua::devices::rgb_to_hsv({r, gr, b});
                    hsv.v *= static_cast<float>(brt) / 100.0f;
                    g->color_hsv = hsv;
                }
                g->color_lo_hsv.h = num<float>(item, "hlo", 0.0f);
                g->color_lo_hsv.s = num<float>(item, "slo", 0.0f);
                g->color_lo_hsv.v = num<float>(item, "vlo", 0.0f);
                g->fade_in_min    = num<uint16_t>(item, "fade_in",  0);
                g->fade_out_min   = num<uint16_t>(item, "fade_out", 0);
                dev = std::move(g);
                break;
            }
            default:
                AC_LOGW(TAG, "skip device id=%u: unknown type=%u", id, type);
                continue;
        }
        dev->enabled = enabled;
        dev->role    = (aqua::devices::DeviceRole)num<uint8_t>(item, "role", 0);
        dm.add(std::move(dev));
    }
    cJSON_Delete(root);
    AC_LOGI(TAG, "load_devices: %u entries", (unsigned)dm.size());
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Triggers.
// -----------------------------------------------------------------------------
esp_err_t save_triggers(TriggerManager& tm) {
    cJSON* root = cJSON_CreateArray();
    if (!root) return ESP_ERR_NO_MEM;

    for (const auto& up : tm.all()) {
        const ITrigger* t = up.get();
        cJSON* obj = cJSON_CreateObject();
        add_common_trigger(obj, *t);
        switch (t->get_type()) {
            case TriggerType::SCHEDULE: {
                const auto* s = static_cast<const ScheduleTrigger*>(t);
                cJSON_AddNumberToObject(obj, "start", s->start_min);
                cJSON_AddNumberToObject(obj, "stop",  s->stop_min);
                cJSON* d = cJSON_AddArrayToObject(obj, "days");
                for (bool b : s->days) cJSON_AddItemToArray(d, cJSON_CreateBool(b));
                cJSON_AddBoolToObject  (obj, "use_interval",    s->use_interval);
                cJSON_AddNumberToObject(obj, "interval_sec",    s->interval_sec);
                cJSON_AddNumberToObject(obj, "on_duration_sec", s->on_duration_sec);
                cJSON_AddBoolToObject  (obj, "daily_at",        s->daily_at);
                cJSON_AddNumberToObject(obj, "daily_at_min",    s->daily_at_min);
                break;
            }
            case TriggerType::SOLAR: {
                const auto* s = static_cast<const SolarTrigger*>(t);
                cJSON_AddNumberToObject(obj, "event",         (uint8_t)s->event);
                cJSON_AddNumberToObject(obj, "offset",        s->offset_min);
                cJSON_AddNumberToObject(obj, "duration",      s->duration_min);
                cJSON_AddBoolToObject  (obj, "use_end_event", s->use_end_event);
                cJSON_AddNumberToObject(obj, "end_event",     (uint8_t)s->end_event);
                cJSON_AddNumberToObject(obj, "end_off",       s->end_offset_min);
                break;
            }
            case TriggerType::TEMP: {
                const auto* s = static_cast<const TempTrigger*>(t);
                cJSON_AddNumberToObject(obj, "sensor",  s->sensor_id);
                cJSON_AddNumberToObject(obj, "thresh",  s->threshold_c);
                cJSON_AddNumberToObject(obj, "cond",    (uint8_t)s->condition);
                cJSON_AddNumberToObject(obj, "hyst",    s->hysteresis_c);
                break;
            }
            case TriggerType::TEMP_MAP: {
                const auto* s = static_cast<const TempMapTrigger*>(t);
                cJSON_AddNumberToObject(obj, "sensor",  s->sensor_id);
                cJSON_AddNumberToObject(obj, "hyst",    s->hysteresis_c);
                cJSON_AddNumberToObject(obj, "temp_lo", s->temp_lo_c);
                cJSON_AddNumberToObject(obj, "temp_hi", s->temp_hi_c);
                cJSON_AddBoolToObject  (obj, "reverse", s->reverse);
                break;
            }
        }
        cJSON_AddItemToArray(root, obj);
    }

    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;
    esp_err_t err = NvsStore::set_blob(KEY_TRIG, str, strlen(str));
    cJSON_free(str);  // L-3: use cJSON_free for cJSON-allocated strings
    AC_LOGI(TAG, "save_triggers: %u entries (%s)",
            (unsigned)tm.size(), esp_err_to_name(err));
    return err;
}

esp_err_t load_triggers(TriggerManager& tm) {
    std::string blob;
    esp_err_t err = NvsStore::get_blob(KEY_TRIG, &blob);
    if (err != ESP_OK) {
        AC_LOGW(TAG, "load_triggers: no blob (%s)", esp_err_to_name(err));
        return err;
    }
    cJSON* root = cJSON_ParseWithLength(blob.data(), blob.size());
    if (!cJSON_IsArray(root)) {
        AC_LOGE(TAG, "load_triggers: bad JSON");
        if (root) cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        uint8_t  id      = num<uint8_t>(item, "id", 0);
        uint8_t  type    = num<uint8_t>(item, "type", 0);
        bool     enabled = boolv(item, "enabled", true);
        std::string name = strv(item, "name", "");

        std::unique_ptr<ITrigger> trig;
        switch ((TriggerType)type) {
            case TriggerType::SCHEDULE: {
                auto s = std::make_unique<ScheduleTrigger>(id, std::move(name));
                s->start_min = num<uint16_t>(item, "start", 0);
                s->stop_min  = num<uint16_t>(item, "stop",  0);
                cJSON* d = cJSON_GetObjectItemCaseSensitive(item, "days");
                if (cJSON_IsArray(d)) {
                    int i = 0;
                    cJSON* dv = nullptr;
                    cJSON_ArrayForEach(dv, d) {
                        if (i >= 7) break;
                        s->days[i++] = cJSON_IsTrue(dv) ||
                                       (cJSON_IsNumber(dv) && dv->valuedouble != 0.0);
                    }
                }
                s->use_interval    = boolv(item, "use_interval", false);
                // New keys store values in seconds; fall back to legacy _min keys * 60.
                {
                    cJSON* iv = cJSON_GetObjectItemCaseSensitive(item, "interval_sec");
                    if (cJSON_IsNumber(iv))
                        s->interval_sec = (uint16_t)iv->valuedouble;
                    else
                        s->interval_sec = (uint16_t)(num<uint16_t>(item, "interval_min", 60) * 60);
                }
                {
                    cJSON* od = cJSON_GetObjectItemCaseSensitive(item, "on_duration_sec");
                    if (cJSON_IsNumber(od))
                        s->on_duration_sec = (uint16_t)od->valuedouble;
                    else
                        s->on_duration_sec = (uint16_t)(num<uint16_t>(item, "on_duration_min", 5) * 60);
                }
                s->daily_at        = boolv(item, "daily_at", false);
                s->daily_at_min    = num<uint16_t>(item, "daily_at_min", 480);
                trig = std::move(s);
                break;
            }
            case TriggerType::SOLAR: {
                auto s = std::make_unique<SolarTrigger>(id, std::move(name));
                s->event         = (SolarEvent)num<uint8_t>(item, "event",    0);
                s->offset_min    = num<int16_t> (item, "offset",   0);
                s->duration_min  = num<uint16_t>(item, "duration", 60);
                s->use_end_event = boolv(item, "use_end_event", false);
                s->end_event     = (SolarEvent)num<uint8_t>(item, "end_event", 0);
                s->end_offset_min = num<int16_t>(item, "end_off", 0);
                trig = std::move(s);
                break;
            }
            case TriggerType::TEMP: {
                auto s = std::make_unique<TempTrigger>(id, std::move(name));
                s->sensor_id    = num<uint8_t>(item, "sensor", 0);
                s->threshold_c  = num<float>  (item, "thresh", 25.0f);
                s->condition    = (TempCondition)num<uint8_t>(item, "cond", 0);
                s->hysteresis_c = num<float>  (item, "hyst",   0.5f);
                trig = std::move(s);
                break;
            }
            case TriggerType::TEMP_MAP: {
                auto s = std::make_unique<TempMapTrigger>(id, std::move(name));
                s->sensor_id    = num<uint8_t>(item, "sensor",  0);
                s->hysteresis_c = num<float>  (item, "hyst",    0.5f);
                s->temp_lo_c    = num<float>  (item, "temp_lo", 20.0f);
                s->temp_hi_c    = num<float>  (item, "temp_hi", 30.0f);
                s->reverse      = boolv(item, "reverse", false);
                trig = std::move(s);
                break;
            }
            default:
                AC_LOGW(TAG, "skip trigger id=%u: unknown type=%u", id, type);
                continue;
        }
        trig->enabled = enabled;

        cJSON* links = cJSON_GetObjectItemCaseSensitive(item, "links");
        if (cJSON_IsArray(links)) {
            cJSON* lv = nullptr;
            cJSON_ArrayForEach(lv, links) {
                if (cJSON_IsNumber(lv)) {
                    trig->linked_device_ids.push_back((uint8_t)lv->valuedouble);
                }
            }
        }
        tm.add(std::move(trig));
    }
    cJSON_Delete(root);
    AC_LOGI(TAG, "load_triggers: %u entries", (unsigned)tm.size());
    return ESP_OK;
}

}  // namespace aqua::storage
