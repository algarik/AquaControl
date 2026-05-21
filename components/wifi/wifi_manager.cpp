// AquaControl — Wi-Fi manager implementation.
#include "wifi_manager.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ac_logger.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_store.h"

namespace aqua::wifi {

static const char* TAG = "wifi";

static constexpr const char* kNvsApPw = "ap_pw";
static constexpr int kBitConnected = BIT0;
static constexpr int kBitGotIp     = BIT1;

static bool              s_inited     = false;
static bool              s_sta_mode   = false;
static bool              s_ap_mode    = false;
static EventGroupHandle_t s_events    = nullptr;
static esp_netif_t*      s_netif_sta  = nullptr;
static esp_netif_t*      s_netif_ap   = nullptr;
// s_ip is written from the sys_event task and read from any task (e.g. the
// LVGL task for the dashboard header).  Guard with a portMUX spinlock so that
// ip_string() never reads a partially-constructed std::string.
static portMUX_TYPE      s_ip_mux     = portMUX_INITIALIZER_UNLOCKED;
static char              s_ip[16]     = "0.0.0.0";  // dotted-decimal, always NUL-terminated
static std::string       s_ap_ssid;
static std::string       s_ap_pw;

// H5 — exponential backoff state for STA reconnects.
static uint8_t            s_retry_count    = 0;
static esp_timer_handle_t s_retry_timer    = nullptr;
static bool               s_sta_exhausted  = false;
static sta_failure_fn     s_failure_cb     = nullptr;
static void*              s_failure_cb_arg = nullptr;
static got_ip_fn          s_got_ip_cb      = nullptr;
static void*              s_got_ip_cb_arg  = nullptr;

// After this many failed connection attempts STA mode is abandoned.
static constexpr uint8_t kMaxStaRetries = 10;

namespace {

static void retry_connect_cb(void* /*arg*/) {
    if (s_sta_mode) {
        AC_LOGI(TAG, "STA retry connect (attempt %u)", (unsigned)(s_retry_count));
        esp_wifi_connect();
    }
}

// Deferred fallback: runs from esp_timer task so it's safe to call
// esp_wifi_stop() without risk of deadlocking the wifi event loop.
static void fallback_timer_cb(void* /*arg*/) {
    AC_LOGW(TAG, "STA max retries reached — switching to AP mode");
    esp_wifi_stop();
    start_ap_fallback();
    if (s_failure_cb) s_failure_cb(s_failure_cb_arg);
}

static esp_timer_handle_t s_fallback_timer = nullptr;

void on_event(void* /*arg*/, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                s_retry_count = 0;
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                xEventGroupClearBits(s_events, kBitConnected | kBitGotIp);
                taskENTER_CRITICAL(&s_ip_mux);
                s_ip[0] = '\0';
                taskEXIT_CRITICAL(&s_ip_mux);

                // Gave up — schedule AP fallback via a separate timer so we
                // don't call esp_wifi_stop() from inside the wifi event handler
                // (which would deadlock).
                if (s_retry_count >= kMaxStaRetries) {
                    AC_LOGW(TAG,
                        "STA gave up after %u attempts — scheduling AP fallback",
                        (unsigned)s_retry_count);
                    s_sta_exhausted = true;
                    s_sta_mode      = false;
                    if (!s_fallback_timer) {
                        const esp_timer_create_args_t ta = {
                            fallback_timer_cb, nullptr,
                            ESP_TIMER_TASK, "wifi_fallback", false
                        };
                        esp_timer_create(&ta, &s_fallback_timer);
                    }
                    esp_timer_start_once(s_fallback_timer, 0);  // fire ASAP
                    break;
                }

                // Exponential backoff: 5 s → 15 s → 30 s (capped)
                static const uint32_t kDelays[] = {5000, 15000, 30000};
                const uint8_t idx = (s_retry_count < 2u) ? s_retry_count : 2u;
                const uint32_t delay_ms = kDelays[idx];
                if (s_retry_count < 255u) ++s_retry_count;
                if (!s_retry_timer) {
                    const esp_timer_create_args_t ta = {
                        retry_connect_cb, nullptr,
                        ESP_TIMER_TASK, "wifi_retry", false
                    };
                    esp_timer_create(&ta, &s_retry_timer);
                }
                esp_timer_stop(s_retry_timer);  // no-op if not running
                esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000u);
                AC_LOGW(TAG, "STA disconnected; retry in %u ms (attempt %u/%u)",
                        (unsigned)delay_ms, (unsigned)s_retry_count,
                        (unsigned)kMaxStaRetries);
                break;
            }
            case WIFI_EVENT_STA_CONNECTED:
                xEventGroupSetBits(s_events, kBitConnected);
                break;
            default: break;
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            auto* ev = static_cast<ip_event_got_ip_t*>(data);
            char buf[16];
            esp_ip4addr_ntoa(&ev->ip_info.ip, buf, sizeof(buf));
            taskENTER_CRITICAL(&s_ip_mux);
            strlcpy(s_ip, buf, sizeof(s_ip));
            taskEXIT_CRITICAL(&s_ip_mux);
            xEventGroupSetBits(s_events, kBitGotIp);
            s_retry_count = 0;  // reset backoff on successful connection
            AC_LOGI(TAG, "got IP: %s", buf);
            if (s_got_ip_cb) s_got_ip_cb(s_got_ip_cb_arg);
        }
    }
}

std::string load_or_make_ap_password() {
    std::string pw;
    if (aqua::storage::NvsStore::get_str(kNvsApPw, &pw) == ESP_OK && pw.size() >= 8) {
        return pw;
    }
    // Generate 8 random chars from a URL-safe alphabet.
    static const char alpha[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    pw.resize(8);
    for (size_t i = 0; i < 8; ++i) {
        pw[i] = alpha[esp_random() % (sizeof(alpha) - 1)];
    }
    aqua::storage::NvsStore::set_str(kNvsApPw, pw);
    AC_LOGI(TAG, "generated new AP password");
    return pw;
}

std::string make_ap_ssid() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char buf[24];
    snprintf(buf, sizeof(buf), "AquaControl-%02X%02X", mac[4], mac[5]);
    return buf;
}

}  // namespace

esp_err_t init() {
    if (s_inited) return ESP_OK;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_netif_sta = esp_netif_create_default_wifi_sta();
    s_netif_ap  = esp_netif_create_default_wifi_ap();
    // Set a recognisable mDNS/DHCP hostname so the device is easy to find
    // on the local network (Issue #20).
    esp_netif_set_hostname(s_netif_sta, "AquaControl");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;

    s_events = xEventGroupCreate();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        on_event, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        on_event, nullptr, nullptr);

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    s_inited = true;
    AC_LOGI(TAG, "initialized");
    return ESP_OK;
}

esp_err_t start_station(const StationCfg& cfg) {
    if (!s_inited) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }
    if (cfg.ssid.empty()) return ESP_ERR_INVALID_ARG;

    esp_wifi_stop();
    s_ap_mode      = false;
    s_sta_mode     = true;
    s_sta_exhausted = false;  // reset on new connection attempt
    s_retry_count  = 0;

    wifi_config_t wc = {};
    std::snprintf(reinterpret_cast<char*>(wc.sta.ssid),     sizeof(wc.sta.ssid),     "%s", cfg.ssid.c_str());
    std::snprintf(reinterpret_cast<char*>(wc.sta.password), sizeof(wc.sta.password), "%s", cfg.password.c_str());
    wc.sta.threshold.authmode = cfg.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) return err;
    AC_LOGI(TAG, "STA started for SSID='%s'", cfg.ssid.c_str());
    return ESP_OK;
}

esp_err_t start_ap_fallback() {
    if (!s_inited) {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }
    esp_wifi_stop();
    s_sta_mode = false;
    s_ap_mode  = true;

    s_ap_ssid = make_ap_ssid();
    s_ap_pw   = load_or_make_ap_password();

    wifi_config_t wc = {};
    std::snprintf(reinterpret_cast<char*>(wc.ap.ssid),     sizeof(wc.ap.ssid),     "%s", s_ap_ssid.c_str());
    std::snprintf(reinterpret_cast<char*>(wc.ap.password), sizeof(wc.ap.password), "%s", s_ap_pw.c_str());
    wc.ap.ssid_len       = (uint8_t)s_ap_ssid.size();
    wc.ap.max_connection = 4;
    wc.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    wc.ap.channel        = 1;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wc);
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) return err;
    // Log only the SSID; do NOT log the AP password — it appears in plaintext
    // on the serial console which may be accessible in deployed units.
    AC_LOGI(TAG, "AP started: SSID='%s'", s_ap_ssid.c_str());
    return ESP_OK;
}

void stop() {
    if (!s_inited) return;
    esp_wifi_stop();
    s_sta_mode = false;
    s_ap_mode  = false;
    taskENTER_CRITICAL(&s_ip_mux);
    s_ip[0] = '\0';
    taskEXIT_CRITICAL(&s_ip_mux);
    if (s_events) xEventGroupClearBits(s_events, kBitConnected | kBitGotIp);
}

bool is_connected() {
    if (!s_events) return false;
    EventBits_t b = xEventGroupGetBits(s_events);
    return (b & kBitGotIp) != 0;
}

std::string ip_string() {
    taskENTER_CRITICAL(&s_ip_mux);
    char buf[16];
    strlcpy(buf, s_ip, sizeof(buf));
    taskEXIT_CRITICAL(&s_ip_mux);
    return (buf[0] == '\0') ? "0.0.0.0" : buf;
}
std::string ap_ssid()      { return s_ap_mode ? s_ap_ssid : std::string(); }
std::string ap_password()  { return s_ap_mode ? s_ap_pw   : std::string(); }

void set_got_ip_callback(got_ip_fn fn, void* arg) {
    s_got_ip_cb     = fn;
    s_got_ip_cb_arg = arg;
}

int8_t sta_rssi() {
    if (!s_sta_mode || !is_connected()) return 0;
    wifi_ap_record_t info = {};
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) return 0;
    return info.rssi;
}

// NVS keys for station credentials.
static constexpr const char* kNvsStaSsid = "sta_ssid";
static constexpr const char* kNvsStaPw   = "sta_pw";

esp_err_t save_station_cfg(const StationCfg& cfg) {
    esp_err_t r = aqua::storage::NvsStore::set_str(kNvsStaSsid, cfg.ssid);
    if (r != ESP_OK) return r;
    return aqua::storage::NvsStore::set_str(kNvsStaPw, cfg.password);
}

esp_err_t load_station_cfg(StationCfg* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t r = aqua::storage::NvsStore::get_str(kNvsStaSsid, &out->ssid);
    if (r != ESP_OK) return r;
    // password key may be absent for open networks — treat as empty string.
    if (aqua::storage::NvsStore::get_str(kNvsStaPw, &out->password) != ESP_OK)
        out->password.clear();
    return ESP_OK;
}

std::vector<ScanResult> scan_networks_blocking(uint8_t max_count,
                                               uint32_t /*timeout_ms*/) {
    std::vector<ScanResult> out;
    if (!s_inited) {
        if (init() != ESP_OK) return out;
    }

    // We need STA or APSTA mode active to scan.
    wifi_mode_t prev_mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&prev_mode);

    bool started_temporarily = false;
    if (prev_mode == WIFI_MODE_NULL) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        if (esp_wifi_start() != ESP_OK) return out;
        started_temporarily = true;
        vTaskDelay(pdMS_TO_TICKS(100));  // let STA interface settle
    } else if (prev_mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        vTaskDelay(pdMS_TO_TICKS(100));  // AP→APSTA mode change needs time before scan
    }

    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    esp_err_t err = esp_wifi_scan_start(&sc, /*block=*/true);
    if (err == ESP_OK) {
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        if (n > max_count) n = max_count;
        if (n > 0) {
            std::vector<wifi_ap_record_t> aps(n);
            if (esp_wifi_scan_get_ap_records(&n, aps.data()) == ESP_OK) {
                out.reserve(n);
                for (uint16_t i = 0; i < n; ++i) {
                    if (aps[i].ssid[0] == '\0') continue;  // skip hidden/empty
                    ScanResult r;
                    r.ssid = reinterpret_cast<const char*>(aps[i].ssid);
                    r.rssi = aps[i].rssi;
                    r.open = (aps[i].authmode == WIFI_AUTH_OPEN);
                    out.push_back(std::move(r));
                }
            }
        }
    } else {
        AC_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
    }

    // Restore previous mode.
    // NOTE: esp_wifi_set_mode(WIFI_MODE_NULL) is invalid after start and
    //       silently fails, leaving mode=STA while WiFi is stopped. That
    //       causes the *next* scan to skip esp_wifi_start() (mode != NULL)
    //       and then fail with ESP_ERR_WIFI_NOT_STARTED. Fix: just stop
    //       without resetting the mode.
    if (started_temporarily) {
        esp_wifi_stop();
    } else if (prev_mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_AP);
    }

    AC_LOGI(TAG, "scan done: %u networks found", (unsigned)out.size());
    return out;
}

void set_sta_failure_callback(sta_failure_fn fn, void* arg) {
    s_failure_cb     = fn;
    s_failure_cb_arg = arg;
}

bool is_sta_exhausted() {
    return s_sta_exhausted;
}

}  // namespace aqua::wifi
