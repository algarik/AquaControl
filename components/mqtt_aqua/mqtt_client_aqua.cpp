// AquaControl — MQTT client implementation.
#include "mqtt_client_aqua.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "ac_logger.h"
#include "cJSON.h"
#include "device_types.h"
#include "esp_timer.h"
#include "history_log.h"
#include "mqtt_client.h"
#include "nvs_store.h"
#include "pwm_device.h"
#include "relay_device.h"
#include "rgb_device.h"

namespace aqua::mqtt {

static const char* TAG = "mqtt";

static esp_mqtt_client_handle_t s_client     = nullptr;
static Config                   s_cfg;
static std::atomic<bool>        s_connected{false};
static CommandCallback          s_cmd_cb;
static ConnectCallback          s_connect_cb;

namespace {

std::string topic_state(uint8_t id) {
    return s_cfg.base_topic + "/devices/" + std::to_string(id) + "/state";
}
std::string topic_cmd(uint8_t id) {
    return s_cfg.base_topic + "/devices/" + std::to_string(id) + "/cmd";
}
std::string topic_sensor(uint8_t id) {
    return s_cfg.base_topic + "/sensors/" + std::to_string(id) + "/state";
}
std::string topic_status() {
    return s_cfg.base_topic + "/status";
}
std::string topic_event() {
    return s_cfg.base_topic + "/event";
}

// Add a standard HA device block to a discovery JSON object.
// Groups all entities under one logical device in the HA UI.
void add_ha_device_block(cJSON* root) {
    cJSON* dev = cJSON_CreateObject();
    cJSON* ids = cJSON_CreateArray();
    cJSON_AddItemToArray(ids, cJSON_CreateString(s_cfg.node_id.c_str()));
    cJSON_AddItemToObject(dev, "identifiers", ids);
    cJSON_AddStringToObject(dev, "name",         "AquaControl");
    cJSON_AddStringToObject(dev, "model",        "AquaControl");
    cJSON_AddStringToObject(dev, "manufacturer", "DIY");
    cJSON_AddItemToObject(root, "device", dev);
}

uint8_t parse_dev_id_from_topic(const char* topic, int len) {
    // .../devices/<id>/cmd
    std::string s(topic, len);
    auto p = s.find("/devices/");
    if (p == std::string::npos) return 0;
    p += 9;
    auto q = s.find('/', p);
    if (q == std::string::npos) return 0;
    return (uint8_t)std::atoi(s.substr(p, q - p).c_str());
}

void on_mqtt_event(void* /*arg*/, esp_event_base_t /*base*/,
                   int32_t event_id, void* event_data) {
    auto* ev = static_cast<esp_mqtt_event_handle_t>(event_data);
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            s_connected.store(true, std::memory_order_release);
            AC_LOGI(TAG, "connected");
            std::string sub = s_cfg.base_topic + "/devices/+/cmd";
            esp_mqtt_client_subscribe(s_client, sub.c_str(), 0);
            if (s_connect_cb) s_connect_cb();
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            s_connected.store(false, std::memory_order_release);
            AC_LOGW(TAG, "disconnected");
            break;
        case MQTT_EVENT_DATA: {
            uint8_t id = parse_dev_id_from_topic(ev->topic, ev->topic_len);
            std::string payload(ev->data, ev->data_len);
            AC_LOGI(TAG, "cmd dev=%u payload='%s'", id, payload.c_str());
            if (s_cmd_cb) s_cmd_cb(id, payload);
            break;
        }
        case MQTT_EVENT_ERROR:
            AC_LOGW(TAG, "error event");
            break;
        default:
            break;
    }
}

}  // namespace

esp_err_t start(const Config& cfg) {
    if (cfg.uri.empty()) return ESP_ERR_INVALID_ARG;
    if (s_client) stop();

    s_cfg = cfg;
    if (s_cfg.base_topic.empty()) s_cfg.base_topic = "aquacontrol";

    esp_mqtt_client_config_t mc = {};
    mc.broker.address.uri        = s_cfg.uri.c_str();
    mc.credentials.username      = s_cfg.user.empty() ? nullptr : s_cfg.user.c_str();
    mc.credentials.authentication.password = s_cfg.pass.empty() ? nullptr : s_cfg.pass.c_str();
    mc.network.reconnect_timeout_ms        = 10000;
    mc.network.disable_auto_reconnect      = false;

    s_client = esp_mqtt_client_init(&mc);
    if (!s_client) return ESP_FAIL;
    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, on_mqtt_event, nullptr);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        return err;
    }
    AC_LOGI(TAG, "started: uri='%s' base='%s'",
            s_cfg.uri.c_str(), s_cfg.base_topic.c_str());
    return ESP_OK;
}

void stop() {
    if (!s_client) return;
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client    = nullptr;
    s_connected.store(false, std::memory_order_release);
}

bool connected() { return s_connected.load(std::memory_order_acquire); }

void set_command_callback(CommandCallback cb) { s_cmd_cb = std::move(cb); }
void set_connect_callback(ConnectCallback cb) { s_connect_cb = std::move(cb); }

esp_err_t publish_device_state(const aqua::devices::IDevice& dev) {
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "id",      dev.id);
    cJSON_AddStringToObject(root, "name",    dev.name.c_str());
    cJSON_AddBoolToObject  (root, "active",  dev.current_active());
    cJSON_AddBoolToObject  (root, "enabled", dev.enabled);
    cJSON_AddBoolToObject  (root, "ovr",     dev.has_override());
    char* payload = cJSON_PrintUnformatted(root);
    std::string topic = topic_state(dev.id);
    int msg_id = esp_mqtt_client_publish(s_client, topic.c_str(),
                                         payload, 0, 0, 1);
    cJSON_free(payload);
    cJSON_Delete(root);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t publish_sensor(uint8_t sensor_id, float temp_c, float humid_pct) {
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"id\":%u,\"temp_c\":%.2f,\"humid\":%.2f}",
             (unsigned)sensor_id, temp_c, humid_pct);
    std::string topic = topic_sensor(sensor_id);
    int msg_id = esp_mqtt_client_publish(s_client, topic.c_str(),
                                         buf, 0, 0, 1);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t publish_ha_sensor_discovery(uint8_t sensor_id, const char* name) {
    if (!s_connected || !s_cfg.ha_discovery) return ESP_ERR_INVALID_STATE;

    char entity_id[64];
    snprintf(entity_id, sizeof(entity_id), "%s_sensor_%u",
             s_cfg.node_id.c_str(), (unsigned)sensor_id);

    std::string state_topic = topic_sensor(sensor_id);

    // Temperature sensor entity
    char disco_topic[128];
    snprintf(disco_topic, sizeof(disco_topic),
             "homeassistant/sensor/%s_temp/config", entity_id);
    cJSON* root = cJSON_CreateObject();
    char full_name[64];
    snprintf(full_name, sizeof(full_name), "%s Temp", name);
    char uid_temp[72]; snprintf(uid_temp, sizeof(uid_temp), "%s_temp", entity_id);
    cJSON_AddStringToObject(root, "name",                full_name);
    cJSON_AddStringToObject(root, "unique_id",           uid_temp);
    cJSON_AddStringToObject(root, "state_topic",         state_topic.c_str());
    cJSON_AddStringToObject(root, "value_template",      "{{ value_json.temp_c | round(1) }}");
    cJSON_AddStringToObject(root, "unit_of_measurement", "°C");
    cJSON_AddStringToObject(root, "device_class",        "temperature");
    cJSON_AddStringToObject(root, "state_class",         "measurement");
    add_ha_device_block(root);
    char* payload = cJSON_PrintUnformatted(root);
    int r1 = esp_mqtt_client_publish(s_client, disco_topic, payload, 0, 1, 1);
    cJSON_free(payload);
    cJSON_Delete(root);

    // Humidity sensor entity
    snprintf(disco_topic, sizeof(disco_topic),
             "homeassistant/sensor/%s_hum/config", entity_id);
    root = cJSON_CreateObject();
    snprintf(full_name, sizeof(full_name), "%s Humidity", name);
    char uid_hum[72]; snprintf(uid_hum, sizeof(uid_hum), "%s_hum", entity_id);
    cJSON_AddStringToObject(root, "name",                full_name);
    cJSON_AddStringToObject(root, "unique_id",           uid_hum);
    cJSON_AddStringToObject(root, "state_topic",         state_topic.c_str());
    cJSON_AddStringToObject(root, "value_template",      "{{ value_json.humid | round(1) }}");
    cJSON_AddStringToObject(root, "unit_of_measurement", "%");
    cJSON_AddStringToObject(root, "device_class",        "humidity");
    cJSON_AddStringToObject(root, "state_class",         "measurement");
    add_ha_device_block(root);
    payload = cJSON_PrintUnformatted(root);
    int r2 = esp_mqtt_client_publish(s_client, disco_topic, payload, 0, 1, 1);
    cJSON_free(payload);
    cJSON_Delete(root);

    return (r1 >= 0 && r2 >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t publish_ha_discovery(const aqua::devices::IDevice& dev) {
    if (!s_connected || !s_cfg.ha_discovery) return ESP_ERR_INVALID_STATE;

    // All devices are published as "switch" — the HA MQTT switch component
    // reliably handles JSON payloads + value_template for state detection.
    // (The "light" component requires a different schema for proper state
    // sync; brightness control can be added later as a separate enhancement.)
    char entity_id[64];
    snprintf(entity_id, sizeof(entity_id), "%s_dev_%u",
             s_cfg.node_id.c_str(), (unsigned)dev.id);

    // Remove any stale "light" retained discovery that may exist in the
    // broker from an earlier firmware (publishing zero-byte retained clears it).
    if (dev.get_type() == aqua::devices::DeviceType::PWM ||
        dev.get_type() == aqua::devices::DeviceType::RGB) {
        char old_topic[128];
        snprintf(old_topic, sizeof(old_topic),
                 "homeassistant/light/%s/config", entity_id);
        esp_mqtt_client_publish(s_client, old_topic, NULL, 0, 1, 1);
    }

    char disco_topic[128];
    snprintf(disco_topic, sizeof(disco_topic),
             "homeassistant/switch/%s/config", entity_id);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name",          dev.name.c_str());
    cJSON_AddStringToObject(root, "unique_id",     entity_id);
    cJSON_AddStringToObject(root, "state_topic",   topic_state(dev.id).c_str());
    cJSON_AddStringToObject(root, "command_topic", topic_cmd(dev.id).c_str());
    cJSON_AddStringToObject(root, "value_template",
                            "{% if value_json.active %}ON{% else %}OFF{% endif %}");
    cJSON_AddStringToObject(root, "payload_on",  "ON");
    cJSON_AddStringToObject(root, "payload_off", "OFF");
    add_ha_device_block(root);

    char* payload = cJSON_PrintUnformatted(root);
    int msg_id = esp_mqtt_client_publish(s_client, disco_topic,
                                         payload, 0, 1, 1);  // QoS1, retain
    cJSON_free(payload);
    cJSON_Delete(root);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

// ---------------------------------------------------------------------------
// System status, events, and HA system sensor discovery
// ---------------------------------------------------------------------------

esp_err_t publish_system_status(uint32_t uptime_s, uint32_t heap_free,
                                bool wifi_connected, int8_t wifi_rssi,
                                const char* wifi_ip, uint16_t active_faults) {
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime_s",       (double)uptime_s);
    cJSON_AddNumberToObject(root, "heap_free",      (double)heap_free);
    cJSON_AddBoolToObject  (root, "wifi_connected", wifi_connected);
    cJSON_AddNumberToObject(root, "wifi_rssi",      (double)wifi_rssi);
    cJSON_AddStringToObject(root, "wifi_ip",        wifi_ip ? wifi_ip : "");
    cJSON_AddNumberToObject(root, "active_faults",  (double)active_faults);
    char* payload = cJSON_PrintUnformatted(root);
    std::string t = topic_status();
    int msg_id = esp_mqtt_client_publish(s_client, t.c_str(), payload, 0, 1, 1);
    cJSON_free(payload);
    cJSON_Delete(root);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t publish_event(const char* type, const char* detail) {
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type",   type   ? type   : "");
    cJSON_AddStringToObject(root, "detail", detail ? detail : "");
    uint64_t uptime_s = (uint64_t)(esp_timer_get_time() / 1000000LL);
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s);
    char* payload = cJSON_PrintUnformatted(root);
    std::string t = topic_event();
    // Not retained — events are transient.
    int msg_id = esp_mqtt_client_publish(s_client, t.c_str(), payload, 0, 0, 0);
    cJSON_free(payload);
    cJSON_Delete(root);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t publish_ha_system_discovery() {
    if (!s_connected || !s_cfg.ha_discovery) return ESP_ERR_INVALID_STATE;

    struct SensorDef {
        const char* suffix;       // entity_id suffix
        const char* name;         // friendly name
        const char* value_tpl;   // Jinja2 value_template
        const char* unit;         // nullptr if no unit
        const char* dev_class;    // nullptr if none
        const char* state_class;  // nullptr if none
        const char* icon;         // nullptr if none
    };
    static const SensorDef defs[] = {
        { "uptime",  "Uptime",        "{{ value_json.uptime_s }}",      "s",   nullptr,       "total_increasing", "mdi:timer-outline" },
        { "heap",    "Free Heap",     "{{ value_json.heap_free }}",     "B",   nullptr,       "measurement",      "mdi:memory" },
        { "rssi",    "WiFi RSSI",     "{{ value_json.wifi_rssi }}",     "dBm", "signal_strength", "measurement", nullptr },
        { "ip",      "WiFi IP",       "{{ value_json.wifi_ip }}",       nullptr, nullptr,     nullptr,            "mdi:ip-network" },
        { "faults",  "Active Faults", "{{ value_json.active_faults }}", nullptr, nullptr,     "measurement",      "mdi:alert-circle" },
    };

    std::string state_topic = topic_status();
    int failures = 0;
    for (const auto& d : defs) {
        char entity_id[72];
        snprintf(entity_id, sizeof(entity_id), "%s_sys_%s",
                 s_cfg.node_id.c_str(), d.suffix);
        char disco_topic[128];
        snprintf(disco_topic, sizeof(disco_topic),
                 "homeassistant/sensor/%s/config", entity_id);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "name",           d.name);
        cJSON_AddStringToObject(root, "unique_id",      entity_id);
        cJSON_AddStringToObject(root, "state_topic",    state_topic.c_str());
        cJSON_AddStringToObject(root, "value_template", d.value_tpl);
        if (d.unit)        cJSON_AddStringToObject(root, "unit_of_measurement", d.unit);
        if (d.dev_class)   cJSON_AddStringToObject(root, "device_class",        d.dev_class);
        if (d.state_class) cJSON_AddStringToObject(root, "state_class",         d.state_class);
        if (d.icon)        cJSON_AddStringToObject(root, "icon",                d.icon);
        add_ha_device_block(root);
        char* payload = cJSON_PrintUnformatted(root);
        int r = esp_mqtt_client_publish(s_client, disco_topic, payload, 0, 1, 1);
        if (r < 0) ++failures;
        cJSON_free(payload);
        cJSON_Delete(root);
    }
    return (failures == 0) ? ESP_OK : ESP_FAIL;
}

// ---------------------------------------------------------------------------
// NVS persistence
// ---------------------------------------------------------------------------

static constexpr const char* kNvsUri       = "mqtt_uri";
static constexpr const char* kNvsUser      = "mqtt_user";
static constexpr const char* kNvsPass      = "mqtt_pass";
static constexpr const char* kNvsTopic     = "mqtt_topic";
static constexpr const char* kNvsHaDisc    = "mqtt_ha";

esp_err_t save_config(const Config& cfg) {
    esp_err_t r;
    r = aqua::storage::NvsStore::set_str(kNvsUri,   cfg.uri);        if (r != ESP_OK) return r;
    r = aqua::storage::NvsStore::set_str(kNvsUser,  cfg.user);       if (r != ESP_OK) return r;
    r = aqua::storage::NvsStore::set_str(kNvsPass,  cfg.pass);       if (r != ESP_OK) return r;
    r = aqua::storage::NvsStore::set_str(kNvsTopic, cfg.base_topic); if (r != ESP_OK) return r;
    return aqua::storage::NvsStore::set_u8(kNvsHaDisc,
                                           cfg.ha_discovery ? 1u : 0u);
}

esp_err_t load_config(Config* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t r = aqua::storage::NvsStore::get_str(kNvsUri, &out->uri);
    if (r != ESP_OK) return r;  // absent = not configured
    // Remaining fields are optional; ignore individual NOT_FOUND errors.
    aqua::storage::NvsStore::get_str(kNvsUser,  &out->user);
    aqua::storage::NvsStore::get_str(kNvsPass,  &out->pass);
    aqua::storage::NvsStore::get_str(kNvsTopic, &out->base_topic);
    uint8_t ha = 1;
    if (aqua::storage::NvsStore::get_u8(kNvsHaDisc, &ha) == ESP_OK)
        out->ha_discovery = (ha != 0);
    return ESP_OK;
}

}  // namespace aqua::mqtt
