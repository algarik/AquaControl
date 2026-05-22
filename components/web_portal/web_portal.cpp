// AquaControl — HTTP portal implementation.
#include "web_portal.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

#include "ac_logger.h"
#include "cJSON.h"
#include "device_manager.h"
#include "device_types.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "sensor_sampler.h"
#include "wifi_manager.h"

namespace aqua::web_portal {

static const char* TAG = "web_portal";

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

static httpd_handle_t             s_server  = nullptr;
static aqua::devices::DeviceManager* s_devices = nullptr;

// ---------------------------------------------------------------------------
// HTML page (served as static chunks)
// ---------------------------------------------------------------------------

static const char HTML_A[] =
    "<!DOCTYPE html><html lang=\"en\">"
    "<head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>AquaControl Setup</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:sans-serif;background:#0a1620;color:#f1f5f9;"
         "padding:16px;max-width:520px;margin:0 auto}"
    "h1{color:#22d3ee;margin-bottom:16px;font-size:1.4rem}"
    "h2{color:#67e8f9;margin:14px 0 8px;font-size:1rem;border-bottom:1px solid #2b4055;padding-bottom:4px}"
    ".card{background:#152433;border-radius:10px;padding:16px;margin-bottom:14px;"
           "border:1px solid #2b4055}"
    "label{display:block;margin-bottom:10px;font-size:.9rem;color:#94a3b8}"
    "label span{display:block;margin-bottom:4px;color:#f1f5f9}"
    "input[type=text],input[type=password]{width:100%;padding:9px 10px;"
    "border-radius:6px;border:1px solid #2b4055;background:#1e3247;"
    "color:#f1f5f9;font-size:1rem}"
    ".btn{display:inline-block;padding:10px 20px;border:none;border-radius:6px;"
         "cursor:pointer;font-size:.9rem;margin-top:8px}"
    ".btn-primary{background:#0369a1;color:#fff}"
    ".btn-on{background:#065f46;color:#34d399;font-size:.85rem;padding:7px 14px}"
    ".btn-off{background:#7f1d1d;color:#f87171;font-size:.85rem;padding:7px 14px}"
    ".row{display:flex;justify-content:space-between;align-items:center;"
         "padding:8px 0;border-bottom:1px solid #1e3247}"
    ".row:last-child{border-bottom:none}"
    ".ok{color:#34d399}.err{color:#f87171}"
    ".note{font-size:.78rem;color:#475569;margin-top:8px}"
    ".ap-box{background:#1c1400;border-left:3px solid #fbbf24;padding:10px 12px;"
             "border-radius:0 6px 6px 0;margin-bottom:12px;line-height:1.9}"
    ".ap-box .lbl{color:#fbbf24;font-weight:bold;font-size:.85rem}"
    ".ap-box .val{color:#f1f5f9;font-family:monospace;font-size:.95rem}"
    "</style></head><body>"
    "<h1>\xF0\x9F\x93\xA1  AquaControl</h1>";

static const char HTML_B[] =
    "<div class=\"card\">"
    "<h2>Wi-Fi Setup</h2>"
    "<form method=\"post\" action=\"/wifi\">"
    "<label><span>Network name (SSID)</span>"
    "<input type=\"text\" name=\"ssid\" autocomplete=\"off\" placeholder=\"Your Wi-Fi network\"></label>"
    "<label><span>Password</span>"
    "<input type=\"password\" name=\"password\" autocomplete=\"off\" placeholder=\"Leave blank for open networks\"></label>"
    "<button class=\"btn btn-primary\" type=\"submit\">Save &amp; Connect</button>"
    "</form>"
    "<p class=\"note\">The device will switch to station mode. "
    "If it cannot connect within 60 s it will return to AP mode.</p>"
    "</div>"
    "<div class=\"card\">"
    "<h2>Status &nbsp;<small id=\"ts\" style=\"color:#475569;font-size:.75rem\"></small></h2>"
    "<div id=\"sens\">Loading&hellip;</div>"
    "<h2>Devices</h2>"
    "<div id=\"devs\">Loading&hellip;</div>"
    "</div>"
    // C-3: Use DOM methods (textContent) for user-controlled strings so that
    // device names with HTML/JS content cannot execute in the browser.
    "<script>"
    "function upd(){"
      "fetch('/status.json').then(function(r){return r.json();}).then(function(d){"
        "document.getElementById('ts').textContent=new Date().toLocaleTimeString();"
        "var sensEl=document.getElementById('sens');sensEl.innerHTML='';"
        "var sl=d.sensors||[];"
        "if(!sl.length){"
          "var sp=document.createElement('span');sp.className='err';"
          "sp.textContent='No sensors';sensEl.appendChild(sp);"
        "}else{"
          "sl.forEach(function(x){"
            "var row=document.createElement('div');row.className='row';"
            "var nm=document.createElement('span');nm.textContent=x.name;"
            "var val=document.createElement('span');"
            "if(x.valid){"
              "var ok=document.createElement('span');ok.className='ok';"
              "ok.textContent=x.temp.toFixed(1)+'\\u00B0C\\u00A0\\u00A0'+x.rh.toFixed(0)+'% RH';"
              "val.appendChild(ok);"
            "}else{"
              "var er=document.createElement('span');er.className='err';"
              "er.textContent='offline';val.appendChild(er);"
            "}"
            "row.appendChild(nm);row.appendChild(val);sensEl.appendChild(row);"
          "});"
        "}"
        "var devsEl=document.getElementById('devs');devsEl.innerHTML='';"
        "var dl=d.devices||[];"
        "if(!dl.length){"
          "var sp2=document.createElement('span');sp2.style.color='#475569';"
          "sp2.textContent='No devices configured';devsEl.appendChild(sp2);"
        "}else{"
          "dl.forEach(function(x){"
            "var row=document.createElement('div');row.className='row';"
            "var nm=document.createElement('span');nm.textContent=x.name;"
            "var btn=document.createElement('button');"
            "btn.className='btn '+(x.on?'btn-off':'btn-on');"
            "btn.textContent=x.on?'Turn OFF':'Turn ON';"
            "(function(id,on){btn.onclick=function(){tog(id,on);};})(x.id,!x.on);"
            "row.appendChild(nm);row.appendChild(btn);devsEl.appendChild(row);"
          "});"
        "}"
      "}).catch(function(){"
        "document.getElementById('sens').innerHTML='<span class=\"err\">Error loading status</span>';"
      "});"
    "}"
    "function tog(id,on){"
      "fetch('/device',{method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({id:id,on:on})"
      "}).then(upd);"
    "}"
    "upd();setInterval(upd,5000);"
    "</script></body></html>";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Decode a single hex digit.
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// URL-decode src → dst (null-terminated, truncated to dst_len-1).
static void url_decode(const char* src, char* dst, size_t dst_len) {
    size_t i = 0;
    while (*src && i < dst_len - 1) {
        if (*src == '%' && hex_val(src[1]) >= 0 && hex_val(src[2]) >= 0) {
            dst[i++] = (char)((hex_val(src[1]) << 4) | hex_val(src[2]));
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            ++src;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// Extract a URL-form-encoded field from body into out (URL-decoded).
static void form_field(const char* body, const char* key,
                       char* out, size_t out_len) {
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char* p = strstr(body, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    const char* end = strchr(p, '&');
    size_t raw_len = end ? (size_t)(end - p) : strlen(p);
    // Copy raw value into a temporary buffer for URL-decoding.
    char tmp[256] = {};
    if (raw_len >= sizeof(tmp)) raw_len = sizeof(tmp) - 1;
    memcpy(tmp, p, raw_len);
    tmp[raw_len] = '\0';
    url_decode(tmp, out, out_len);
}

// C-3: Escape HTML special characters to prevent stored XSS.
// Applied to all user-controlled strings embedded in HTML responses.
static void html_escape(const char* src, char* dst, size_t dst_len) {
    size_t i = 0;
    while (*src && i + 7 < dst_len) {
        switch (*src) {
            case '&':  memcpy(&dst[i], "&amp;",  5); i += 5; break;
            case '<':  memcpy(&dst[i], "&lt;",   4); i += 4; break;
            case '>':  memcpy(&dst[i], "&gt;",   4); i += 4; break;
            case '"':  memcpy(&dst[i], "&#34;",  5); i += 5; break;
            case '\'': memcpy(&dst[i], "&#39;",  5); i += 5; break;
            default:   dst[i++] = *src;              break;
        }
        ++src;
    }
    dst[i] = '\0';
}

// C-3: Escape a string for embedding as a JSON string value (between the
// enclosing double-quotes). Prevents JSON structure breakage on device names
// containing backslash or quote characters.
static void json_escape_str(const char* src, char* dst, size_t dst_len) {
    size_t i = 0;
    while (*src && i + 7 < dst_len) {
        unsigned char c = (unsigned char)*src;
        if (c == '"')       { memcpy(&dst[i], "\\\"", 2); i += 2; }
        else if (c == '\\') { memcpy(&dst[i], "\\\\", 2); i += 2; }
        else if (c < 0x20)  { snprintf(&dst[i], dst_len - i, "\\u%04x", c); i += 6; }
        else                { dst[i++] = (char)c; }
        ++src;
    }
    dst[i] = '\0';
}

// H-5: cJSON-based command parser — robust against whitespace and key ordering.
static bool parse_device_cmd(const char* body, size_t body_len,
                              uint8_t* id_out, bool* on_out) {
    cJSON* root = cJSON_ParseWithLength(body, body_len);
    if (!root) return false;
    cJSON* id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON* on_item = cJSON_GetObjectItemCaseSensitive(root, "on");
    bool ok = false;
    if (cJSON_IsNumber(id_item) && cJSON_IsBool(on_item)) {
        double raw_id = id_item->valuedouble;
        if (raw_id >= 1 && raw_id <= 255) {
            *id_out = (uint8_t)raw_id;
            *on_out = cJSON_IsTrue(on_item);
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------

static esp_err_t handle_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    // AP info block — shown only when AP is actually active.
    std::string ap_ssid_str = aqua::wifi::ap_ssid();
    std::string ap_pw_str   = aqua::wifi::ap_password();

    httpd_resp_send_chunk(req, HTML_A, HTTPD_RESP_USE_STRLEN);

    if (!ap_ssid_str.empty()) {
        // C-3: Escape AP credentials before inserting into HTML to prevent XSS.
        char ap_ssid_esc[256];
        char ap_pw_esc[256];
        html_escape(ap_ssid_str.c_str(), ap_ssid_esc, sizeof(ap_ssid_esc));
        html_escape(ap_pw_str.c_str(),   ap_pw_esc,   sizeof(ap_pw_esc));

        char ap_blk[1024];  // must fit up to 255+255 bytes of escaped user data + ~380 bytes static HTML
        snprintf(ap_blk, sizeof(ap_blk),
            "<div class=\"ap-box\">"
            "<span class=\"lbl\">&#x1F4E1;  Access Point active</span><br>"
            "SSID:&nbsp;&nbsp;&nbsp;&nbsp;<span class=\"val\">%s</span><br>"
            "Password:&nbsp;<span class=\"val\">%s</span><br>"
            "<span style=\"color:#94a3b8;font-size:.8rem\">"
              "Connect to the above network, then open "
              "<a href=\"http://192.168.4.1\" style=\"color:#22d3ee\">"
              "http://192.168.4.1</a></span>"
            "</div>",
            ap_ssid_esc, ap_pw_esc);
        httpd_resp_send_chunk(req, ap_blk, HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_send_chunk(req, HTML_B, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, nullptr, 0);  // end of chunked response
    return ESP_OK;
}

static esp_err_t handle_status_json(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");

    // Build JSON in a stack buffer.  Worst-case with 16 devices: ~800 bytes.
    char buf[1280];
    int  pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"sensors\":[");

    // Water sensor
    aqua::sensors::Reading w = aqua::sensors::get(aqua::sensors::Role::WATER);
    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "{\"name\":\"Water\",\"valid\":%s,\"temp\":%.2f,\"rh\":%.1f}",
        w.valid ? "true" : "false", w.temp_c, w.humidity);

    // Ambient sensor
    aqua::sensors::Reading a = aqua::sensors::get(aqua::sensors::Role::AMBIENT);
    pos += snprintf(buf + pos, sizeof(buf) - pos,
        ",{\"name\":\"Ambient\",\"valid\":%s,\"temp\":%.2f,\"rh\":%.1f}",
        a.valid ? "true" : "false", a.temp_c, a.humidity);

    pos += snprintf(buf + pos, sizeof(buf) - pos, "],\"devices\":[");

    // Device list
    bool first = true;
    if (s_devices) {
        s_devices->for_each([&](aqua::devices::IDevice& dev) {
            if (pos >= (int)sizeof(buf) - 80) return;  // buffer guard
            // C-3: Proper JSON string escaping for device names (backslash, quote, controls).
            char safe_name[128];
            json_escape_str(dev.name.c_str(), safe_name, sizeof(safe_name));
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                "%s{\"id\":%u,\"name\":\"%s\",\"on\":%s}",
                first ? "" : ",",
                (unsigned)dev.id, safe_name,
                dev.current_active() ? "true" : "false");
            first = false;
        });
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");

    httpd_resp_send(req, buf, pos);
    return ESP_OK;
}

static esp_err_t handle_wifi_post(httpd_req_t* req) {
    // Read POST body.
    char body[512] = {};
    int  received  = httpd_req_recv(req, body,
                                    std::min((int)req->content_len,
                                             (int)sizeof(body) - 1));
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[64]    = {};
    char password[64] = {};
    form_field(body, "ssid",     ssid,     sizeof(ssid));
    form_field(body, "password", password, sizeof(password));

    if (ssid[0] == '\0') {
        // No SSID — just show success page without changing WiFi.
        const char* resp =
            "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<meta http-equiv=\"refresh\" content=\"3;url=/\"></head>"
            "<body style=\"font-family:sans-serif;background:#0a1620;color:#f1f5f9;"
                         "padding:20px;text-align:center\">"
            "<h2 style=\"color:#f87171\">SSID cannot be empty.</h2>"
            "<p>Redirecting back&hellip;</p></body></html>";
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Send "Saved — connecting…" page before restarting WiFi.
    // C-3: Escape ssid before inserting into HTML to prevent XSS.
    char ssid_esc[256];
    html_escape(ssid, ssid_esc, sizeof(ssid_esc));
    char resp_buf[1024];
    snprintf(resp_buf, sizeof(resp_buf),
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<meta http-equiv=\"refresh\" content=\"15;url=/\"></head>"
        "<body style=\"font-family:sans-serif;background:#0a1620;color:#f1f5f9;"
                     "padding:20px;text-align:center\">"
        "<h2 style=\"color:#34d399\">Saved! Connecting to &ldquo;%s&rdquo;&hellip;</h2>"
        "<p style=\"color:#94a3b8;margin-top:12px\">"
          "If the connection succeeds the AP will disappear. "
          "If it fails the device will return to AP mode within 60 s.</p>"
        "<p style=\"margin-top:16px;color:#475569\">Refreshing in 15 s&hellip;</p>"
        "</body></html>", ssid_esc);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, resp_buf, HTTPD_RESP_USE_STRLEN);

    // Defer WiFi restart so the HTTP response is fully transmitted first.
    struct DeferCtx {
        char ssid[64];
        char password[64];
    };
    auto* dc = new DeferCtx{};
    strlcpy(dc->ssid,     ssid,     sizeof(dc->ssid));
    strlcpy(dc->password, password, sizeof(dc->password));

    esp_timer_handle_t t = nullptr;
    const esp_timer_create_args_t ta = {
        [](void* arg) {
            auto* dc = static_cast<DeferCtx*>(arg);
            aqua::wifi::StationCfg cfg;
            cfg.ssid     = dc->ssid;
            cfg.password = dc->password;
            aqua::wifi::save_station_cfg(cfg);
            aqua::wifi::stop();
            aqua::wifi::start_station(cfg);
            AC_LOGI("web_portal", "WiFi restarted in STA mode for SSID '%s'", dc->ssid);
            delete dc;
        },
        dc, ESP_TIMER_TASK, "wp_wifi_restart", false
    };
    if (esp_timer_create(&ta, &t) == ESP_OK) {
        esp_timer_start_once(t, 600 * 1000);  // 600 ms
    } else {
        delete dc;
    }

    return ESP_OK;
}

static esp_err_t handle_device_post(httpd_req_t* req) {
    char body[128] = {};
    int  received  = httpd_req_recv(req, body,
                                    std::min((int)req->content_len,
                                             (int)sizeof(body) - 1));
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    uint8_t dev_id = 0;
    bool    on     = false;
    if (!parse_device_cmd(body, (size_t)received, &dev_id, &on)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
        return ESP_FAIL;
    }

    if (s_devices) {
        aqua::devices::IDevice* dev = s_devices->find(dev_id);
        if (dev) {
            dev->set_override(aqua::devices::OverrideMode::INDEFINITE, on, 0);
            AC_LOGI(TAG, "device %u set %s via web portal", (unsigned)dev_id,
                    on ? "ON" : "OFF");
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t start(const Context& ctx) {
    if (s_server) {
        AC_LOGW(TAG, "already running");
        return ESP_OK;
    }

    s_devices = ctx.devices;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80;
    cfg.max_uri_handlers = 8;
    cfg.stack_size       = 6144;
    cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t uris[] = {
        { "/",            HTTP_GET,  handle_root,        nullptr },
        { "/status.json", HTTP_GET,  handle_status_json, nullptr },
        { "/wifi",        HTTP_POST, handle_wifi_post,   nullptr },
        { "/device",      HTTP_POST, handle_device_post, nullptr },
    };
    for (auto& u : uris) httpd_register_uri_handler(s_server, &u);

    AC_LOGI(TAG, "portal started on http://192.168.4.1");
    return ESP_OK;
}

void stop() {
    if (!s_server) return;
    httpd_stop(s_server);
    s_server  = nullptr;
    s_devices = nullptr;
    AC_LOGI(TAG, "portal stopped");
}

void stop_deferred() {
    if (!s_server) return;
    esp_timer_handle_t t = nullptr;
    const esp_timer_create_args_t ta = {
        [](void*) { stop(); }, nullptr, ESP_TIMER_TASK, "wp_stop", false
    };
    if (esp_timer_create(&ta, &t) == ESP_OK) {
        esp_timer_start_once(t, 100 * 1000);  // 100 ms
    }
}

bool is_running() { return s_server != nullptr; }

}  // namespace aqua::web_portal
