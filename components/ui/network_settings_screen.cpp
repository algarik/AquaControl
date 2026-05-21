// AquaControl — Network settings screen implementation.
//
// Layout:
//   Header bar  ("Network"  |  "Save")
//   ┌─ Wi-Fi section ─────────────────────────────────────────────────────┐
//   │  SSID:    [textarea]                                                 │
//   │  Password:[textarea  ●●●●]                                          │
//   │  Status:  Connected / Disconnected  (IP x.x.x.x)                   │
//   └─────────────────────────────────────────────────────────────────────┘
//   ┌─ MQTT section ──────────────────────────────────────────────────────┐
//   │  Broker URI:   [textarea]                                           │
//   │  User:         [textarea]                                           │
//   │  Password:     [textarea  ●●●●]                                     │
//   │  Base topic:   [textarea]                                           │
//   │  HA Discovery: [toggle]                                             │
//   └─────────────────────────────────────────────────────────────────────┘
//   [Keyboard — shown when a textarea is focused, hidden otherwise]
//
// Save: persists credentials to NVS via wifi_manager::save_station_cfg()
//       and mqtt::save_config(), then restarts both connections.
#include "network_settings_screen.h"

#include <cstring>
#include <vector>

#include "ac_logger.h"
#include "chrome.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i18n.h"
#include "mqtt_client_aqua.h"
#include "screen_manager.h"
#include "system_config.h"
#include "theme.h"
#include "ui_context.h"
#include "wifi_manager.h"

namespace aqua::ui::network_settings_screen {

namespace {

constexpr const char* TAG = "NetSettings";

// ---------------------------------------------------------------------------
// Per-screen heap state
// ---------------------------------------------------------------------------

struct State {
    // Wi-Fi
    lv_obj_t* sw_wifi_enabled  = nullptr;  // toggle: wifi on/off
    lv_obj_t* wifi_fields_wrap = nullptr;  // container shown/hidden with toggle
    lv_obj_t* ta_ssid          = nullptr;
    lv_obj_t* ta_sta_pw        = nullptr;
    lv_obj_t* lbl_status       = nullptr;

    // MQTT
    lv_obj_t* ta_mqtt_uri   = nullptr;
    lv_obj_t* ta_mqtt_user  = nullptr;
    lv_obj_t* ta_mqtt_pw    = nullptr;
    lv_obj_t* ta_mqtt_topic = nullptr;
    lv_obj_t* sw_ha_disc    = nullptr;

    // Keyboard (shared; shown/hidden on focus)
    lv_obj_t* keyboard    = nullptr;
    lv_obj_t* scroll_view = nullptr;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static lv_obj_t* make_section_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_title(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_accent(), 0);
    return lbl;
}

static lv_obj_t* make_field_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
    return lbl;
}

static lv_obj_t* make_textarea(lv_obj_t* parent,
                                const char* placeholder,
                                const char* initial,
                                bool        password = false) {
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, 44);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (initial && initial[0]) lv_textarea_set_text(ta, initial);
    if (password) lv_textarea_set_password_mode(ta, true);

    lv_obj_set_style_bg_color(ta, theme::color_surface_alt(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, theme::color_outline(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, theme::font_body(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, theme::color_text_primary(), LV_PART_MAIN);
    lv_obj_set_style_radius(ta, theme::RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, theme::PAD_SM, LV_PART_MAIN);
    return ta;
}

// ---------------------------------------------------------------------------
// Keyboard show/hide on textarea focus/defocus
// ---------------------------------------------------------------------------

static void on_ta_focused(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st || !st->keyboard) return;
    lv_obj_t* ta = lv_event_get_target_obj(e);
    lv_keyboard_set_textarea(st->keyboard, ta);
    lv_obj_remove_flag(st->keyboard, LV_OBJ_FLAG_HIDDEN);
    if (st->scroll_view) {
        lv_obj_set_size(st->scroll_view, LV_PCT(100), 480 - chrome::kHeaderH - 220);
        lv_obj_update_layout(st->scroll_view);
    }
    lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
}

static void on_ta_defocused(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st || !st->keyboard) return;
    lv_obj_add_flag(st->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(st->keyboard, nullptr);
    if (st->scroll_view)
        lv_obj_set_size(st->scroll_view, LV_PCT(100), 480 - chrome::kHeaderH);
}

static void on_kb_ready(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st || !st->keyboard) return;
    lv_obj_add_flag(st->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(st->keyboard, nullptr);
    if (st->scroll_view)
        lv_obj_set_size(st->scroll_view, LV_PCT(100), 480 - chrome::kHeaderH);
}

// Connect textarea focus/defocus → keyboard visibility.
static void bind_textarea(lv_obj_t* ta, State* st) {
    lv_obj_add_event_cb(ta, on_ta_focused,   LV_EVENT_FOCUSED,   st);
    lv_obj_add_event_cb(ta, on_ta_defocused, LV_EVENT_DEFOCUSED, st);
}

// ---------------------------------------------------------------------------
// Save action
// ---------------------------------------------------------------------------

static void do_save(State* st) {
    // — WiFi enabled/disabled —
    bool wifi_en = lv_obj_has_state(st->sw_wifi_enabled, LV_STATE_CHECKED);
    auto* sys = aqua::ui::ui_context().sys_cfg;
    if (sys) {
        sys->wifi_enabled = wifi_en;
        aqua::storage::save_system_config(*sys);
    }

    aqua::wifi::StationCfg wc;
    wc.ssid     = lv_textarea_get_text(st->ta_ssid);
    wc.password = lv_textarea_get_text(st->ta_sta_pw);

    if (!wifi_en) {
        aqua::wifi::stop();
        aqua::mqtt::stop();  // MQTT needs WiFi — kill it immediately
        AC_LOGI(TAG, "WiFi disabled — MQTT stopped");
    } else if (!wc.ssid.empty()) {
        aqua::wifi::save_station_cfg(wc);
        // Restart station in STA mode (fire-and-forget; event group tracks
        // progress asynchronously).
        aqua::wifi::stop();
        aqua::wifi::start_station(wc);
        AC_LOGI(TAG, "WiFi credentials saved and reconnect started");
    } else {
        // No SSID → AP fallback.
        aqua::wifi::stop();
        aqua::wifi::start_ap_fallback();
        AC_LOGI(TAG, "WiFi SSID cleared; AP fallback started");
    }

    // — MQTT —
    aqua::mqtt::Config mc;
    mc.uri        = lv_textarea_get_text(st->ta_mqtt_uri);
    mc.user       = lv_textarea_get_text(st->ta_mqtt_user);
    mc.pass       = lv_textarea_get_text(st->ta_mqtt_pw);
    mc.base_topic = lv_textarea_get_text(st->ta_mqtt_topic);
    mc.ha_discovery = lv_obj_has_state(st->sw_ha_disc, LV_STATE_CHECKED);
    if (!mc.uri.empty()) {
        aqua::mqtt::save_config(mc);
    }
    if (!mc.uri.empty() && wifi_en) {
        aqua::mqtt::stop();
        aqua::mqtt::start(mc);
        AC_LOGI(TAG, "MQTT config saved and reconnect started");
    }
}

static void on_save(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st) return;
    // Dismiss keyboard if open.
    if (st->keyboard) lv_obj_add_flag(st->keyboard, LV_OBJ_FLAG_HIDDEN);
    do_save(st);
    // Pop back to menu after saving.
    screen_manager::pop(screen_manager::Transition::SLIDE_RIGHT);
}

// ---------------------------------------------------------------------------
// Screen lifecycle
// ---------------------------------------------------------------------------

static void on_screen_delete(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    delete st;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public build()
// ---------------------------------------------------------------------------

lv_obj_t* build() {
    // Load current persisted values.
    aqua::wifi::StationCfg wc;
    aqua::wifi::load_station_cfg(&wc);  // ignore NOT_FOUND — fields stay empty

    aqua::mqtt::Config mc;
    aqua::mqtt::load_config(&mc);  // ignore NOT_FOUND

    // Load system config for wifi_enabled flag.
    aqua::storage::SystemConfig sys_cfg;
    aqua::storage::load_system_config(&sys_cfg);

    auto* st = new State();

    // Root screen.
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(root, on_screen_delete, LV_EVENT_DELETE, st);

    // Header.
    chrome::build(root, i18n::tr(i18n::LangKey::NAV_NETWORK), chrome::pop_on_back, i18n::tr(i18n::LangKey::BTN_SAVE), on_save);
    // Patch Save button user-data with st.
    // chrome::build stores callbacks by pointer — the Save callback already
    // receives (e) from LVGL. We need to set user_data on the Save event.
    // Because chrome::build wires on_action with nullptr user_data, we re-wire
    // the action button that was just created as the last child of the header.
    {
        lv_obj_t* hdr = lv_obj_get_child(root, 0);  // first child = header
        uint32_t n = lv_obj_get_child_count(hdr);
        if (n > 0) {
            lv_obj_t* btn_save = lv_obj_get_child(hdr, (int32_t)(n - 1));
            lv_obj_remove_event_cb(btn_save, on_save);
            lv_obj_add_event_cb(btn_save, on_save, LV_EVENT_CLICKED, st);
        }
    }

    // Scrollable content area below the header.
    lv_obj_t* scroll = lv_obj_create(root);
    lv_obj_set_size(scroll, LV_PCT(100),
                    480 - chrome::kHeaderH);
    // TOP anchor so shrinking height keeps top pinned below the header.
    lv_obj_align(scroll, LV_ALIGN_TOP_MID, 0, chrome::kHeaderH);
    st->scroll_view = scroll;
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, theme::PAD_MD, 0);
    lv_obj_set_style_pad_row(scroll, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // ------- Wi-Fi section -------
    make_section_label(scroll, i18n::tr(i18n::LangKey::NET_WIFI));

    // WiFi enabled/disabled toggle.
    {
        lv_obj_t* row = lv_obj_create(scroll);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, i18n::tr(i18n::LangKey::WIZ_WIFI_ENABLE));
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
        st->sw_wifi_enabled = lv_switch_create(row);
        if (sys_cfg.wifi_enabled)
            lv_obj_add_state(st->sw_wifi_enabled, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(st->sw_wifi_enabled, theme::color_accent(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        // Show/hide SSID+PW fields dynamically when toggled.
        lv_obj_add_event_cb(st->sw_wifi_enabled, [](lv_event_t* e) {
            auto* s = static_cast<State*>(lv_event_get_user_data(e));
            if (!s || !s->wifi_fields_wrap) return;
            bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
            if (on) lv_obj_clear_flag(s->wifi_fields_wrap, LV_OBJ_FLAG_HIDDEN);
            else    lv_obj_add_flag (s->wifi_fields_wrap, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_VALUE_CHANGED, st);
    }

    // WiFi SSID / password wrapped in a container (shown/hidden with toggle).
    {
        lv_obj_t* wrap = lv_obj_create(scroll);
        lv_obj_set_size(wrap, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(wrap, 0, 0);
        lv_obj_set_style_pad_all(wrap, 0, 0);
        lv_obj_set_style_pad_row(wrap, 4, 0);
        lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
        if (!sys_cfg.wifi_enabled) lv_obj_add_flag(wrap, LV_OBJ_FLAG_HIDDEN);
        st->wifi_fields_wrap = wrap;

        make_field_label(wrap, i18n::tr(i18n::LangKey::NET_SSID));
        st->ta_ssid = make_textarea(wrap, "network-name", wc.ssid.c_str());
        bind_textarea(st->ta_ssid, st);

        make_field_label(wrap, i18n::tr(i18n::LangKey::NET_PASSWORD));
        st->ta_sta_pw = make_textarea(wrap, i18n::tr(i18n::LangKey::NET_OPEN_HINT), wc.password.c_str(), true);
        bind_textarea(st->ta_sta_pw, st);

        // Status row.
        bool connected = aqua::wifi::is_connected();
        char buf[48];
        if (connected)
            snprintf(buf, sizeof(buf), "%s  %s",
                     i18n::tr(i18n::LangKey::NET_CONNECTED),
                     aqua::wifi::ip_string().c_str());
        else
            snprintf(buf, sizeof(buf), "%s",
                     i18n::tr(i18n::LangKey::NET_DISCONNECTED));
        st->lbl_status = lv_label_create(wrap);
        lv_label_set_text(st->lbl_status, buf);
        lv_obj_set_style_text_font(st->lbl_status, theme::font_body(), 0);
        lv_obj_set_style_text_color(st->lbl_status,
            connected ? theme::color_success() : theme::color_text_disabled(), 0);

        // RSSI display (only when connected).
        if (connected) {
            int8_t rssi = aqua::wifi::sta_rssi();
            if (rssi != 0) {
                char rssi_buf[32];
                snprintf(rssi_buf, sizeof(rssi_buf),
                         i18n::tr(i18n::LangKey::NET_SIGNAL), (int)rssi);
                lv_obj_t* lbl_rssi = lv_label_create(wrap);
                lv_label_set_text(lbl_rssi, rssi_buf);
                lv_obj_set_style_text_font(lbl_rssi, theme::font_caption(), 0);
                lv_obj_set_style_text_color(lbl_rssi, theme::color_text_secondary(), 0);
            }
        }

        // Action row: Forget + Rescan buttons.
        {
            lv_obj_t* btn_row = lv_obj_create(wrap);
            lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(btn_row, 0, 0);
            lv_obj_set_style_pad_all(btn_row, 0, 0);
            lv_obj_set_style_pad_column(btn_row, theme::PAD_SM, 0);
            lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // Forget Network button.
            lv_obj_t* btn_forget = lv_btn_create(btn_row);
            lv_obj_set_height(btn_forget, 40);
            lv_obj_set_style_bg_color(btn_forget, theme::color_error(), 0);
            lv_obj_set_style_bg_color(btn_forget, theme::color_error(), LV_STATE_PRESSED);
            lv_obj_set_style_radius(btn_forget, theme::RADIUS_SM, 0);
            lv_obj_set_style_pad_hor(btn_forget, theme::PAD_MD, 0);
            {
                lv_obj_t* lbl = lv_label_create(btn_forget);
                lv_label_set_text(lbl, i18n::tr(i18n::LangKey::NET_FORGET));
                lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
                lv_obj_set_style_text_color(lbl, theme::color_background(), 0);
            }
            lv_obj_add_event_cb(btn_forget, [](lv_event_t* e) {
                auto* s = static_cast<State*>(lv_event_get_user_data(e));
                if (!s) return;
                // Clear saved credentials, switch to AP fallback.
                aqua::wifi::save_station_cfg({"", ""});
                if (s->ta_ssid)  lv_textarea_set_text(s->ta_ssid, "");
                if (s->ta_sta_pw) lv_textarea_set_text(s->ta_sta_pw, "");
                aqua::wifi::stop();
                aqua::wifi::start_ap_fallback();
                if (s->lbl_status) {
                    lv_label_set_text(s->lbl_status,
                        i18n::tr(i18n::LangKey::NET_DISCONNECTED));
                    lv_obj_set_style_text_color(s->lbl_status,
                        theme::color_text_disabled(), 0);
                }
            }, LV_EVENT_CLICKED, st);

            // Rescan button.
            lv_obj_t* btn_scan = lv_btn_create(btn_row);
            lv_obj_set_height(btn_scan, 40);
            lv_obj_set_style_bg_color(btn_scan, theme::color_surface_alt(), 0);
            lv_obj_set_style_radius(btn_scan, theme::RADIUS_SM, 0);
            lv_obj_set_style_pad_hor(btn_scan, theme::PAD_MD, 0);
            {
                lv_obj_t* lbl = lv_label_create(btn_scan);
                lv_label_set_text(lbl, i18n::tr(i18n::LangKey::NET_RESCAN));
                lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
                lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
            }
            lv_obj_add_event_cb(btn_scan, [](lv_event_t* e) {
                auto* s = static_cast<State*>(lv_event_get_user_data(e));
                if (!s || !s->ta_ssid) return;
                // Launch background scan task.
                struct ScanCtx { lv_obj_t* ta_ssid; };
                auto* ctx = new ScanCtx{s->ta_ssid};
                xTaskCreate([](void* arg) {
                    auto* c = static_cast<ScanCtx*>(arg);
                    auto* res = new std::vector<aqua::wifi::ScanResult>(
                        aqua::wifi::scan_networks_blocking(20, 8000));
                    struct CB { lv_obj_t* ta; std::vector<aqua::wifi::ScanResult>* results; };
                    auto* cb = new CB{c->ta_ssid, res};
                    delete c;
                    lv_async_call([](void* data) {
                        auto* d = static_cast<CB*>(data);
                        if (!d->results->empty() && lv_obj_is_valid(d->ta)) {
                            lv_textarea_set_text(d->ta, d->results->front().ssid.c_str());
                        }
                        delete d->results;
                        delete d;
                    }, cb);
                    vTaskDelete(nullptr);
                }, "net_scan", 4096, ctx, 2, nullptr);
            }, LV_EVENT_CLICKED, st);
        }
    }

    // Spacer.
    lv_obj_t* spacer1 = lv_obj_create(scroll);
    lv_obj_set_size(spacer1, LV_PCT(100), theme::PAD_SM);
    lv_obj_set_style_bg_opa(spacer1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer1, 0, 0);

    // ------- MQTT section -------
    make_section_label(scroll, i18n::tr(i18n::LangKey::NET_MQTT));

    make_field_label(scroll, i18n::tr(i18n::LangKey::WIZ_MQTT_URI_LBL));
    st->ta_mqtt_uri = make_textarea(scroll, "mqtt://...", mc.uri.c_str());
    bind_textarea(st->ta_mqtt_uri, st);

    make_field_label(scroll, i18n::tr(i18n::LangKey::NET_MQTT_USER));
    st->ta_mqtt_user = make_textarea(scroll, "", mc.user.c_str());
    bind_textarea(st->ta_mqtt_user, st);

    make_field_label(scroll, i18n::tr(i18n::LangKey::NET_MQTT_PW));
    st->ta_mqtt_pw = make_textarea(scroll, "", mc.pass.c_str(), true);
    bind_textarea(st->ta_mqtt_pw, st);

    make_field_label(scroll, i18n::tr(i18n::LangKey::NET_BASE_TOPIC));
    st->ta_mqtt_topic = make_textarea(scroll, "aquacontrol",
                                      mc.base_topic.empty() ? "aquacontrol"
                                                            : mc.base_topic.c_str());
    bind_textarea(st->ta_mqtt_topic, st);

    // HA Discovery toggle.
    {
        lv_obj_t* row = lv_obj_create(scroll);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);

        lv_obj_t* lbl = make_field_label(row, i18n::tr(i18n::LangKey::NET_HA_DISC));
        (void)lbl;

        st->sw_ha_disc = lv_switch_create(row);
        if (mc.ha_discovery) lv_obj_add_state(st->sw_ha_disc, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(st->sw_ha_disc, theme::color_accent(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
    }

    // ------- Keyboard (hidden until a textarea is tapped) -------
    st->keyboard = lv_keyboard_create(root);
    lv_obj_set_style_bg_color(st->keyboard, theme::color_surface(), 0);
    lv_obj_set_style_text_color(st->keyboard, theme::color_text_primary(), 0);
    lv_obj_set_style_bg_color(st->keyboard, theme::color_surface_alt(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(st->keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(st->keyboard, theme::color_text_primary(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(st->keyboard, theme::font_body(), LV_PART_ITEMS);
    lv_obj_set_style_border_color(st->keyboard, theme::color_outline(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(st->keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(st->keyboard, theme::color_accent(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(st->keyboard, theme::color_background(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_flag(st->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(st->keyboard, on_kb_ready, LV_EVENT_READY,    st);
    lv_obj_add_event_cb(st->keyboard, on_kb_ready, LV_EVENT_CANCEL,   st);

    return root;
}

}  // namespace aqua::ui::network_settings_screen
