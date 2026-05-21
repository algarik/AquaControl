// AquaControl — History log implementation (SPIFFS append-only).
#include "history_log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <errno.h>

#include "ac_logger.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace aqua::history {

static const char* TAG = "history";

static constexpr const char* kBasePath = "/spiffs";
static constexpr const char* kLabel    = "spiffs";
static constexpr const char* kFile     = "/spiffs/events.log";
static constexpr const char* kFileBak  = "/spiffs/events.bak";
static constexpr size_t      kFileMaxBytes = 96 * 1024;  // ~96 KB per file

static bool                s_mounted = false;
static SemaphoreHandle_t   s_mtx     = nullptr;

namespace {

void ensure_mutex() {
    if (s_mtx == nullptr) s_mtx = xSemaphoreCreateMutex();
}

struct Lock {
    Lock()  { if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY); }
    ~Lock() { if (s_mtx) xSemaphoreGive(s_mtx); }
};

const char* type_name(EventType t) {
    switch (t) {
        case EventType::TRIG_EDGE:    return "trig";
        case EventType::DEV_OVERRIDE: return "ovr";
        case EventType::FAULT_RAISE:  return "flt+";
        case EventType::FAULT_CLEAR:  return "flt-";
        case EventType::BOOT:         return "boot";
        case EventType::NTP_SYNC:     return "ntp";
        case EventType::WIFI_CONNECT: return "wifi+";
        case EventType::WIFI_LOST:    return "wifi-";
        case EventType::SENSOR:       return "sens";
        default:                      return "other";
    }
}

EventType type_from_name(const char* s) {
    if (!s) return EventType::OTHER;
    if (!strcmp(s, "trig"))  return EventType::TRIG_EDGE;
    if (!strcmp(s, "ovr"))   return EventType::DEV_OVERRIDE;
    if (!strcmp(s, "flt+"))  return EventType::FAULT_RAISE;
    if (!strcmp(s, "flt-"))  return EventType::FAULT_CLEAR;
    if (!strcmp(s, "boot"))  return EventType::BOOT;
    if (!strcmp(s, "ntp"))   return EventType::NTP_SYNC;
    if (!strcmp(s, "wifi+")) return EventType::WIFI_CONNECT;
    if (!strcmp(s, "wifi-")) return EventType::WIFI_LOST;
    if (!strcmp(s, "sens"))  return EventType::SENSOR;
    return EventType::OTHER;
}

bool rotate_if_needed_locked() {
    FILE* f = fopen(kFile, "rb");
    if (!f) return true;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    if (sz < 0 || (size_t)sz < kFileMaxBytes) return true;
    remove(kFileBak);
    if (rename(kFile, kFileBak) != 0) {
        AC_LOGW(TAG, "rotate rename failed (errno=%d)", errno);
        return false;
    }
    AC_LOGI(TAG, "rotated events.log (size=%ld -> .bak)", sz);
    return true;
}

bool parse_line(const char* line, Event& out) {
    if (!line || !*line) return false;
    char type_buf[16] = {0};
    int  dev = 0, trig = 0;
    long long ts = 0;
    int consumed = 0;
    int n = sscanf(line, "%lld|%15[^|]|%d|%d|%n",
                   &ts, type_buf, &dev, &trig, &consumed);
    if (n < 4 || consumed == 0) return false;
    out.ts      = (time_t)ts;
    out.type    = type_from_name(type_buf);
    out.dev_id  = (uint8_t)dev;
    out.trig_id = (uint8_t)trig;
    out.msg     = line + consumed;
    while (!out.msg.empty() && (out.msg.back() == '\n' || out.msg.back() == '\r')) {
        out.msg.pop_back();
    }
    return true;
}

void read_all_lines(const char* path, std::vector<Event>& out) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        Event ev;
        if (parse_line(buf, ev)) out.push_back(std::move(ev));
    }
    fclose(f);
}

}  // namespace

esp_err_t init() {
    if (s_mounted) return ESP_OK;
    ensure_mutex();
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = kBasePath,
        .partition_label        = kLabel,
        .max_files              = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }
    size_t total = 0, used = 0;
    if (esp_spiffs_info(kLabel, &total, &used) == ESP_OK) {
        AC_LOGI(TAG, "SPIFFS mounted: %u/%u bytes used",
                (unsigned)used, (unsigned)total);
    }
    s_mounted = true;
    return ESP_OK;
}

esp_err_t append(const Event& ev) {
    if (!s_mounted) return ESP_ERR_INVALID_STATE;
    ensure_mutex();
    Lock _g;
    rotate_if_needed_locked();
    FILE* f = fopen(kFile, "a");
    if (!f) {
        AC_LOGW(TAG, "fopen append failed (errno=%d)", errno);
        return ESP_FAIL;
    }
    fprintf(f, "%lld|%s|%u|%u|%s\n",
            (long long)ev.ts, type_name(ev.type),
            (unsigned)ev.dev_id, (unsigned)ev.trig_id,
            ev.msg.c_str());
    fclose(f);
    return ESP_OK;
}

esp_err_t append(EventType type, uint8_t dev_id, uint8_t trig_id, const char* msg) {
    Event ev{};
    ev.ts      = time(nullptr);
    ev.type    = type;
    ev.dev_id  = dev_id;
    ev.trig_id = trig_id;
    ev.msg     = (msg != nullptr) ? msg : "";
    return append(ev);
}

std::vector<Event> recent(size_t max_events) {
    std::vector<Event> all;
    if (!s_mounted) return all;
    ensure_mutex();
    Lock _g;
    read_all_lines(kFileBak, all);
    read_all_lines(kFile,    all);
    if (all.size() > max_events) {
        all.erase(all.begin(), all.begin() + (all.size() - max_events));
    }
    std::reverse(all.begin(), all.end());
    return all;
}

esp_err_t clear_all() {
    if (!s_mounted) return ESP_ERR_INVALID_STATE;
    ensure_mutex();
    Lock _g;
    remove(kFile);
    remove(kFileBak);
    AC_LOGI(TAG, "history cleared");
    return ESP_OK;
}

}  // namespace aqua::history
