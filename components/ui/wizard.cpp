// AquaControl — First-run setup wizard.
//
// Single-screen, multi-step design:
//   Step 0: Language
//   Step 1: Welcome
//   Step 2: WiFi (SSID scan + manual entry)
//   Step 3: Time zone + NTP servers
//   Step 4: Done / Apply
//
// A fixed header (48 px), scrollable content area (360 px) and fixed nav bar
// (72 px) fill the 800×480 display.  The LVGL keyboard is an overlay child of
// the root and does not belong to the scrollable area.

#include "wizard.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ac_logger.h"
#include "drum_roller.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "screen_manager.h"
#include "system_config.h"
#include "theme.h"
#include "ui_context.h"
#include "wifi_manager.h"
#include "i18n.h"
#include "mqtt_client_aqua.h"

namespace aqua::ui::wizard {

namespace {

static const char* TAG = "Wizard";

// ---------------------------------------------------------------------------
// Display constants (800 × 480 landscape)
// ---------------------------------------------------------------------------
constexpr int16_t kDispW    = 800;
constexpr int16_t kDispH    = 480;
constexpr int16_t kHdrH     = 48;   // step header bar
constexpr int16_t kNavH     = 72;   // bottom nav bar
constexpr int16_t kContentH = kDispH - kHdrH - kNavH;  // 360 px
constexpr int16_t kKbdY     = kDispH - 220;            // keyboard top when visible
// Steps: 0=Language 1=Welcome 2=WiFi+MQTT 3=TimeZone 4=Location 5=Done
constexpr int      kSteps   = 6;

// ---------------------------------------------------------------------------
// Wizard state (heap-allocated for the duration of the wizard screen)
// ---------------------------------------------------------------------------
struct WizState {
    // Root + structural widgets
    lv_obj_t*  root          = nullptr;
    lv_obj_t*  hdr_bar       = nullptr;
    lv_obj_t*  hdr_lbl       = nullptr;   // step title in header
    lv_obj_t*  content       = nullptr;   // scrollable 360 px body
    lv_obj_t*  nav_bar       = nullptr;
    lv_obj_t*  btn_back      = nullptr;
    lv_obj_t*  btn_next      = nullptr;
    lv_obj_t*  btn_skip      = nullptr;   // WiFi step only
    lv_obj_t*  dots[6]       = {};        // step indicator dots
    lv_obj_t*  keyboard      = nullptr;

    // Language selection (Step 0)
    aqua::ui::i18n::Language lang = aqua::ui::i18n::Language::EN;

    // Step 2 – WiFi / MQTT widget handles
    bool       wifi_en             = true;
    bool       mqtt_en             = false;
    lv_obj_t*  sw_wifi_en          = nullptr;
    lv_obj_t*  wifi_fields_cont    = nullptr;
    lv_obj_t*  sw_mqtt_en          = nullptr;
    lv_obj_t*  mqtt_fields_cont    = nullptr;
    lv_obj_t*  ta_ssid             = nullptr;
    lv_obj_t*  ta_pw               = nullptr;
    lv_obj_t*  scan_btn            = nullptr;
    lv_obj_t*  scan_list           = nullptr;
    lv_obj_t*  scan_status_lbl     = nullptr;
    lv_obj_t*  ta_mqtt_uri         = nullptr;
    lv_obj_t*  ta_mqtt_user        = nullptr;
    lv_obj_t*  ta_mqtt_pw          = nullptr;

    // Step 3 – Time zone
    lv_obj_t*  roller_utc         = nullptr;
    lv_obj_t*  ta_ntp1            = nullptr;
    lv_obj_t*  ta_ntp2            = nullptr;

    // Step 4 – Location
    float      latitude            = 54.7826f;
    float      longitude           = 32.0453f;
    lv_obj_t*  ta_lat              = nullptr;
    lv_obj_t*  ta_lon              = nullptr;

    // Collected data
    std::string  ssid;
    std::string  wifi_pw;
    std::string  mqtt_uri;
    std::string  mqtt_user;
    std::string  mqtt_pw;
    int16_t      utc_offset_min = 0;
    std::string  ntp1 = "pool.ntp.org";
    std::string  ntp2 = "time.google.com";

    int  step     = 0;
    bool scanning = false;
};

static WizState* g_wiz = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static lv_obj_t* make_ta(lv_obj_t* parent, const char* placeholder,
                          const char* init) {
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, 52);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (init && init[0]) lv_textarea_set_text(ta, init);
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

static lv_obj_t* make_field_lbl(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
    lv_obj_set_style_pad_top(lbl, theme::PAD_SM, 0);
    return lbl;
}

static lv_obj_t* make_note(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lbl, theme::font_caption(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_disabled(), 0);
    lv_obj_set_style_pad_top(lbl, theme::PAD_SM, 0);
    return lbl;
}

static void bind_ta_keyboard(lv_obj_t* ta) {
    // Mirror the exact pattern used by network_settings_screen, which works:
    // a TA inside a scrollable column flex container receives LV_EVENT_FOCUSED
    // on tap via the pointer indev's click-focusable path. Avoid extras here
    // (current_target_obj swap, scroll_to_view) which previously seemed
    // harmless but in practice the wizard's nested scrollable containers
    // are more fragile than the flat network screen.
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
        lv_obj_t* ta_obj = lv_event_get_target_obj(e);
        AC_LOGI(TAG, "wiz TA FOCUSED ta=%p g_wiz=%p kbd=%p",
                ta_obj, g_wiz, g_wiz ? g_wiz->keyboard : nullptr);
        if (!g_wiz || !g_wiz->keyboard) return;
        if (!ta_obj || lv_obj_has_state(ta_obj, LV_STATE_DISABLED)) return;
        lv_keyboard_set_textarea(g_wiz->keyboard, ta_obj);
        lv_obj_remove_flag(g_wiz->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_wiz->keyboard);
        lv_area_t a;
        lv_obj_get_coords(g_wiz->keyboard, &a);
        AC_LOGI(TAG, "wiz KBD shown parent=%p coords=%d,%d,%d,%d hidden=%d",
                lv_obj_get_parent(g_wiz->keyboard),
                (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2,
                (int)lv_obj_has_flag(g_wiz->keyboard, LV_OBJ_FLAG_HIDDEN));
    }, LV_EVENT_FOCUSED, nullptr);

    lv_obj_add_event_cb(ta, [](lv_event_t* /*e*/) {
        AC_LOGI(TAG, "wiz TA DEFOCUSED");
        if (!g_wiz || !g_wiz->keyboard) return;
        lv_obj_add_flag(g_wiz->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(g_wiz->keyboard, nullptr);
    }, LV_EVENT_DEFOCUSED, nullptr);

    // Safety net: a CLICKED handler that shows the keyboard even if the
    // pointer indev did not deliver FOCUSED (e.g. when an upstream
    // parent has consumed/cancelled the focus through a scroll gesture).
    // CLICKED only fires when the press was *not* interpreted as a scroll,
    // so this cannot misfire while the user is scrolling the form.
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
        lv_obj_t* ta_obj = lv_event_get_target_obj(e);
        AC_LOGI(TAG, "wiz TA CLICKED ta=%p g_wiz=%p kbd=%p",
                ta_obj, g_wiz, g_wiz ? g_wiz->keyboard : nullptr);
        if (!g_wiz || !g_wiz->keyboard) return;
        if (!ta_obj || lv_obj_has_state(ta_obj, LV_STATE_DISABLED)) return;
        lv_keyboard_set_textarea(g_wiz->keyboard, ta_obj);
        lv_obj_remove_flag(g_wiz->keyboard, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr);

    // Also log raw PRESSED so we can confirm the touch event hits the TA
    // at all (i.e. that no parent is consuming the press as a scroll).
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
        AC_LOGI(TAG, "wiz TA PRESSED ta=%p", lv_event_get_target_obj(e));
    }, LV_EVENT_PRESSED, nullptr);
}

// ---------------------------------------------------------------------------
// Save current step's entered values into WizState
// ---------------------------------------------------------------------------
static void collect_step_values(WizState* wiz) {
    switch (wiz->step) {
        case 2:  // WiFi + MQTT step
            wiz->wifi_en = wiz->sw_wifi_en &&
                           lv_obj_has_state(wiz->sw_wifi_en, LV_STATE_CHECKED);
            if (wiz->ta_ssid)     wiz->ssid      = lv_textarea_get_text(wiz->ta_ssid);
            if (wiz->ta_pw)       wiz->wifi_pw   = lv_textarea_get_text(wiz->ta_pw);
            wiz->mqtt_en = wiz->sw_mqtt_en &&
                           lv_obj_has_state(wiz->sw_mqtt_en, LV_STATE_CHECKED);
            if (wiz->ta_mqtt_uri)  wiz->mqtt_uri  = lv_textarea_get_text(wiz->ta_mqtt_uri);
            if (wiz->ta_mqtt_user) wiz->mqtt_user = lv_textarea_get_text(wiz->ta_mqtt_user);
            if (wiz->ta_mqtt_pw)   wiz->mqtt_pw   = lv_textarea_get_text(wiz->ta_mqtt_pw);
            break;
        case 3:  // Time zone step
            if (wiz->roller_utc)
                wiz->utc_offset_min =
                    (int16_t)drum_roller::get_signed_offset(wiz->roller_utc);
            if (wiz->ta_ntp1) wiz->ntp1 = lv_textarea_get_text(wiz->ta_ntp1);
            if (wiz->ta_ntp2) wiz->ntp2 = lv_textarea_get_text(wiz->ta_ntp2);
            break;
        case 4:  // Location step
            if (wiz->ta_lat) wiz->latitude  = (float)atof(lv_textarea_get_text(wiz->ta_lat));
            if (wiz->ta_lon) wiz->longitude = (float)atof(lv_textarea_get_text(wiz->ta_lon));
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Update dot indicators and nav buttons
// ---------------------------------------------------------------------------
static void update_dots(WizState* wiz) {
    for (int i = 0; i < kSteps; ++i) {
        if (!wiz->dots[i]) continue;
        bool active = (i == wiz->step);
        lv_obj_set_size(wiz->dots[i], active ? 14 : 10, active ? 14 : 10);
        lv_obj_set_style_bg_color(wiz->dots[i],
            active ? theme::color_accent() : theme::color_outline(), 0);
    }
}

static void update_nav(WizState* wiz) {
    if (wiz->btn_back) {
        if (wiz->step == 0)
            lv_obj_add_flag(wiz->btn_back, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(wiz->btn_back, LV_OBJ_FLAG_HIDDEN);
    }
    if (wiz->btn_skip) {
        if (wiz->step == 2)   // WiFi+MQTT step
            lv_obj_clear_flag(wiz->btn_skip, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(wiz->btn_skip, LV_OBJ_FLAG_HIDDEN);
    }
    if (wiz->btn_next) {
        lv_obj_t* lbl = lv_obj_get_child(wiz->btn_next, 0);
        if (lbl) {
            lv_label_set_text(lbl,
                wiz->step == kSteps - 1
                    ? LV_SYMBOL_HOME "  Dashboard"
                    : i18n::tr(i18n::LangKey::BTN_NEXT));
        }
    }
    if (wiz->hdr_lbl) {
        using K = i18n::LangKey;
        const char* titles[kSteps] = {
            i18n::tr(K::WIZ_LANG),
            i18n::tr(K::WIZ_WELCOME),
            i18n::tr(K::WIZ_WIFI),
            i18n::tr(K::WIZ_TIME),
            i18n::tr(K::WIZ_LOCATION),
            i18n::tr(K::WIZ_DONE),
        };
        lv_label_set_text(wiz->hdr_lbl, titles[wiz->step]);
    }
}

// Forward declaration: build_content_language needs to trigger a step rebuild.
static void build_step_content(WizState* wiz);

// ---------------------------------------------------------------------------
// Step 0 — Language selection
// ---------------------------------------------------------------------------
static void build_content_language(WizState* wiz) {
    using K = i18n::LangKey;
    lv_obj_t* c = wiz->content;

    lv_obj_t* title = lv_label_create(c);
    lv_label_set_text(title, i18n::tr(K::WIZ_CHOOSE_LANG));
    lv_obj_set_style_text_font(title, theme::font_heading(), 0);
    lv_obj_set_style_text_color(title, theme::color_text_primary(), 0);

    lv_obj_t* sub = lv_label_create(c);
    lv_label_set_text(sub, i18n::tr(K::WIZ_LANG_BODY));
    lv_obj_set_style_text_font(sub, theme::font_body(), 0);
    lv_obj_set_style_text_color(sub, theme::color_text_secondary(), 0);
    lv_obj_set_width(sub, LV_PCT(100));
    lv_obj_set_style_pad_bottom(sub, theme::PAD_LG, 0);

    struct LangOpt { const char* lbl; aqua::ui::i18n::Language lang; };
    const LangOpt kOpts[] = {
        { "English (EN)", aqua::ui::i18n::Language::EN },
        { "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9 (RU)",
          aqua::ui::i18n::Language::RU },
    };
    for (const auto& opt : kOpts) {
        bool active = (opt.lang == wiz->lang);
        lv_obj_t* btn = lv_btn_create(c);
        lv_obj_set_size(btn, LV_PCT(100), 64);
        lv_obj_set_style_bg_color(btn,
            active ? theme::color_accent() : theme::color_surface_alt(), 0);
        lv_obj_set_style_border_color(btn, theme::color_accent(), 0);
        lv_obj_set_style_border_width(btn, active ? 0 : 2, 0);
        lv_obj_set_style_radius(btn, theme::RADIUS_MD, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, opt.lbl);
        lv_obj_set_style_text_font(lbl, theme::font_title(), 0);
        lv_obj_set_style_text_color(lbl,
            active ? theme::color_text_primary() : theme::color_text_secondary(),
            0);
        lv_obj_center(lbl);

        struct LCtx { aqua::ui::i18n::Language lang; };
        auto* ctx = new LCtx{opt.lang};
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* lc = static_cast<LCtx*>(lv_event_get_user_data(e));
            if (!g_wiz) return;
            g_wiz->lang = lc->lang;
            aqua::ui::i18n::set_language(lc->lang);
            // Defer rebuild: cannot call lv_obj_clean on parent from a
            // child's event handler (would delete the event target mid-callback).
            lv_async_call([](void*) {
                if (!g_wiz || g_wiz->step != 0) return;
                build_step_content(g_wiz);
                update_dots(g_wiz);
                update_nav(g_wiz);
            }, nullptr);
        }, LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            delete static_cast<LCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, ctx);
    }
}

// ---------------------------------------------------------------------------
// Step 1 — Welcome
// ---------------------------------------------------------------------------
static void build_content_welcome(WizState* wiz) {
    using K = i18n::LangKey;
    lv_obj_t* c = wiz->content;

    lv_obj_t* icon = lv_label_create(c);
    lv_label_set_text(icon, LV_SYMBOL_HOME);
    lv_obj_set_style_text_font(icon, theme::font_heading(), 0);
    lv_obj_set_style_text_color(icon, theme::color_accent(), 0);

    lv_obj_t* title = lv_label_create(c);
    lv_label_set_text(title, i18n::tr(K::WIZ_WELCOME));
    lv_obj_set_style_text_font(title, theme::font_heading(), 0);
    lv_obj_set_style_text_color(title, theme::color_text_primary(), 0);

    lv_obj_t* sep = lv_obj_create(c);
    lv_obj_set_size(sep, 80, 3);
    lv_obj_set_style_bg_color(sep, theme::color_accent(), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 3, 0);
    lv_obj_set_style_pad_top(sep, theme::PAD_SM, 0);

    lv_obj_t* sub = lv_label_create(c);
    lv_label_set_text(sub, i18n::tr(K::WIZ_WELCOME_BODY));
    lv_obj_set_style_text_font(sub, theme::font_body(), 0);
    lv_obj_set_style_text_color(sub, theme::color_text_secondary(), 0);
    lv_obj_set_width(sub, LV_PCT(100));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(sub, 4, 0);
    lv_obj_set_style_pad_top(sub, theme::PAD_LG, 0);
}

// ---------------------------------------------------------------------------
// SSID scan: background task → lv_async_call
// ---------------------------------------------------------------------------

static void scan_results_cb(void* data) {
    // Called on LVGL thread via lv_async_call.
    auto* results = static_cast<std::vector<aqua::wifi::ScanResult>*>(data);
    if (!g_wiz || !g_wiz->scan_list || !g_wiz->scan_status_lbl) {
        delete results;
        return;
    }
    g_wiz->scanning = false;

    if (results->empty()) {
        lv_label_set_text(g_wiz->scan_status_lbl,
            i18n::tr(i18n::LangKey::WIZ_NO_NETWORKS));
        lv_obj_clear_flag(g_wiz->scan_status_lbl, LV_OBJ_FLAG_HIDDEN);
        delete results;
        return;
    }

    lv_obj_clean(g_wiz->scan_list);
    lv_obj_clear_flag(g_wiz->scan_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_wiz->scan_status_lbl, LV_OBJ_FLAG_HIDDEN);

    for (auto& r : *results) {
        lv_obj_t* btn = lv_btn_create(g_wiz->scan_list);
        lv_obj_set_size(btn, LV_PCT(100), 44);
        lv_obj_set_style_bg_color(btn, theme::color_surface_alt(), 0);
        lv_obj_set_style_bg_color(btn, theme::color_accent(), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, theme::RADIUS_SM, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(btn, theme::PAD_SM, 0);

        lv_obj_t* ssid_lbl = lv_label_create(btn);
        lv_label_set_text(ssid_lbl, r.ssid.c_str());
        lv_obj_set_style_text_font(ssid_lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(ssid_lbl, theme::color_text_primary(), 0);
        lv_obj_set_flex_grow(ssid_lbl, 1);

        char info[24];
        snprintf(info, sizeof(info), "%d dBm  %s", (int)r.rssi,
                 r.open ? "" : LV_SYMBOL_CLOSE);
        lv_obj_t* info_lbl = lv_label_create(btn);
        lv_label_set_text(info_lbl, info);
        lv_obj_set_style_text_font(info_lbl, theme::font_caption(), 0);
        lv_obj_set_style_text_color(info_lbl, theme::color_text_disabled(), 0);

        // Tap row → fill SSID textarea
        struct SelCtx { std::string ssid; };
        auto* sc = new SelCtx{r.ssid};
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* ctx = static_cast<SelCtx*>(lv_event_get_user_data(e));
            if (g_wiz && g_wiz->ta_ssid)
                lv_textarea_set_text(g_wiz->ta_ssid, ctx->ssid.c_str());
            if (g_wiz && g_wiz->scan_list)
                lv_obj_add_flag(g_wiz->scan_list, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_CLICKED, sc);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            delete static_cast<SelCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, sc);
    }
    delete results;
}

static void scan_task(void* /*arg*/) {
    auto* results = new std::vector<aqua::wifi::ScanResult>(
        aqua::wifi::scan_networks_blocking(20, 8000));
    lv_async_call(scan_results_cb, results);
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Step 2 — WiFi & MQTT
// ---------------------------------------------------------------------------

// Helper: makes a switch-row [Label ........... toggle]
static lv_obj_t* make_toggle_row(lv_obj_t* parent, const char* label, bool checked) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 52);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, theme::font_title(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
    lv_obj_t* sw = lv_switch_create(row);
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, theme::color_accent(),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    return sw;
}

static void build_content_wifi(WizState* wiz) {
    using K = i18n::LangKey;
    lv_obj_t* c = wiz->content;

    // --- Wi-Fi enable toggle ---
    wiz->sw_wifi_en = make_toggle_row(c, i18n::tr(K::WIZ_WIFI_ENABLE), wiz->wifi_en);

    // --- WiFi fields (hidden when disabled) ---
    wiz->wifi_fields_cont = lv_obj_create(c);
    lv_obj_set_size(wiz->wifi_fields_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wiz->wifi_fields_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wiz->wifi_fields_cont, 0, 0);
    lv_obj_set_style_pad_all(wiz->wifi_fields_cont, 0, 0);
    lv_obj_set_style_pad_row(wiz->wifi_fields_cont, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(wiz->wifi_fields_cont, LV_FLEX_FLOW_COLUMN);
    if (!wiz->wifi_en) lv_obj_add_flag(wiz->wifi_fields_cont, LV_OBJ_FLAG_HIDDEN);

    {
        lv_obj_t* fc = wiz->wifi_fields_cont;
        make_field_lbl(fc, i18n::tr(K::NET_SSID));

        lv_obj_t* ssid_row = lv_obj_create(fc);
        lv_obj_set_size(ssid_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(ssid_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ssid_row, 0, 0);
        lv_obj_set_style_pad_all(ssid_row, 0, 0);
        lv_obj_set_style_pad_column(ssid_row, theme::PAD_SM, 0);
        lv_obj_set_flex_flow(ssid_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(ssid_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(ssid_row, LV_OBJ_FLAG_SCROLLABLE);

        wiz->ta_ssid = make_ta(ssid_row, i18n::tr(K::NET_SSID), wiz->ssid.c_str());
        lv_obj_set_flex_grow(wiz->ta_ssid, 1);
        bind_ta_keyboard(wiz->ta_ssid);

        lv_obj_t* scan_btn = lv_btn_create(ssid_row);
        lv_obj_set_size(scan_btn, 110, 52);
        lv_obj_set_style_bg_color(scan_btn, theme::color_surface_alt(), 0);
        lv_obj_set_style_border_color(scan_btn, theme::color_accent(), 0);
        lv_obj_set_style_border_width(scan_btn, 2, 0);
        lv_obj_set_style_radius(scan_btn, theme::RADIUS_SM, 0);
        lv_obj_t* scan_lbl_w = lv_label_create(scan_btn);
        {
            char scan_lbl_buf[32];
            snprintf(scan_lbl_buf, sizeof(scan_lbl_buf),
                     LV_SYMBOL_WIFI "  %s", i18n::tr(K::WIZ_SCAN));
            lv_label_set_text(scan_lbl_w, scan_lbl_buf);
        }
        lv_obj_set_style_text_font(scan_lbl_w, theme::font_body(), 0);
        lv_obj_set_style_text_color(scan_lbl_w, theme::color_accent(), 0);
        lv_obj_center(scan_lbl_w);
        wiz->scan_btn = scan_btn;
        lv_obj_add_event_cb(scan_btn, [](lv_event_t* /*e*/) {
            if (!g_wiz || g_wiz->scanning) return;
            g_wiz->scanning = true;
            if (g_wiz->scan_status_lbl) {
                char scan_buf[48];
                snprintf(scan_buf, sizeof(scan_buf),
                         LV_SYMBOL_REFRESH "  %s", i18n::tr(K::WIZ_SCANNING));
                lv_label_set_text(g_wiz->scan_status_lbl, scan_buf);
                lv_obj_clear_flag(g_wiz->scan_status_lbl, LV_OBJ_FLAG_HIDDEN);
            }
            if (g_wiz->scan_list)
                lv_obj_add_flag(g_wiz->scan_list, LV_OBJ_FLAG_HIDDEN);
            xTaskCreate(scan_task, "wiz_scan", 8192, nullptr, 5, nullptr);
        }, LV_EVENT_CLICKED, nullptr);

        make_field_lbl(fc, i18n::tr(K::NET_PASSWORD));
        wiz->ta_pw = make_ta(fc, i18n::tr(K::WIZ_WIFI_PW_PH), wiz->wifi_pw.c_str());
        lv_textarea_set_password_mode(wiz->ta_pw, true);
        bind_ta_keyboard(wiz->ta_pw);

        lv_obj_t* status_lbl = lv_label_create(fc);
        lv_label_set_text(status_lbl, "");
        lv_obj_set_style_text_font(status_lbl, theme::font_caption(), 0);
        lv_obj_set_style_text_color(status_lbl, theme::color_accent(), 0);
        lv_obj_add_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);
        wiz->scan_status_lbl = status_lbl;

        lv_obj_t* scan_list = lv_obj_create(fc);
        lv_obj_set_size(scan_list, LV_PCT(100), 180);
        lv_obj_set_style_bg_color(scan_list, theme::color_surface(), 0);
        lv_obj_set_style_bg_opa(scan_list, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(scan_list, theme::color_outline(), 0);
        lv_obj_set_style_border_width(scan_list, 1, 0);
        lv_obj_set_style_radius(scan_list, theme::RADIUS_SM, 0);
        lv_obj_set_style_pad_all(scan_list, theme::PAD_SM, 0);
        lv_obj_set_style_pad_row(scan_list, 4, 0);
        lv_obj_set_flex_flow(scan_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(scan_list, LV_OBJ_FLAG_HIDDEN);
        wiz->scan_list = scan_list;
    }

    // Divider
    lv_obj_t* div = lv_obj_create(c);
    lv_obj_set_size(div, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(div, theme::color_outline(), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_pad_ver(div, theme::PAD_SM, 0);

    // --- MQTT enable toggle ---
    wiz->sw_mqtt_en = make_toggle_row(c, i18n::tr(K::WIZ_MQTT_ENABLE), wiz->mqtt_en);
    if (!wiz->wifi_en) lv_obj_add_state(wiz->sw_mqtt_en, LV_STATE_DISABLED);

    // --- MQTT fields (hidden when disabled or WiFi off) ---
    wiz->mqtt_fields_cont = lv_obj_create(c);
    lv_obj_set_size(wiz->mqtt_fields_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wiz->mqtt_fields_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wiz->mqtt_fields_cont, 0, 0);
    lv_obj_set_style_pad_all(wiz->mqtt_fields_cont, 0, 0);
    lv_obj_set_style_pad_row(wiz->mqtt_fields_cont, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(wiz->mqtt_fields_cont, LV_FLEX_FLOW_COLUMN);
    if (!wiz->mqtt_en || !wiz->wifi_en)
        lv_obj_add_flag(wiz->mqtt_fields_cont, LV_OBJ_FLAG_HIDDEN);

    {
        lv_obj_t* mc = wiz->mqtt_fields_cont;
        make_field_lbl(mc, i18n::tr(K::WIZ_MQTT_URI_LBL));
        wiz->ta_mqtt_uri = make_ta(mc, "mqtt://host:1883", wiz->mqtt_uri.c_str());
        bind_ta_keyboard(wiz->ta_mqtt_uri);

        make_field_lbl(mc, i18n::tr(K::WIZ_OPTIONAL_USER));
        wiz->ta_mqtt_user = make_ta(mc, "", wiz->mqtt_user.c_str());
        bind_ta_keyboard(wiz->ta_mqtt_user);

        make_field_lbl(mc, i18n::tr(K::WIZ_OPTIONAL_PW));
        wiz->ta_mqtt_pw = make_ta(mc, "", wiz->mqtt_pw.c_str());
        lv_textarea_set_password_mode(wiz->ta_mqtt_pw, true);
        bind_ta_keyboard(wiz->ta_mqtt_pw);
    }

    make_note(c, i18n::tr(K::WIZ_AP_NOTE));

    // --- WiFi toggle event ---
    lv_obj_add_event_cb(wiz->sw_wifi_en, [](lv_event_t* e) {
        if (!g_wiz) return;
        bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
        g_wiz->wifi_en = on;
        if (g_wiz->wifi_fields_cont) {
            if (on) lv_obj_clear_flag(g_wiz->wifi_fields_cont, LV_OBJ_FLAG_HIDDEN);
            else    lv_obj_add_flag (g_wiz->wifi_fields_cont, LV_OBJ_FLAG_HIDDEN);
        }
        if (g_wiz->sw_mqtt_en) {
            if (on) lv_obj_remove_state(g_wiz->sw_mqtt_en, LV_STATE_DISABLED);
            else {
                lv_obj_add_state(g_wiz->sw_mqtt_en, LV_STATE_DISABLED);
                lv_obj_remove_state(g_wiz->sw_mqtt_en, LV_STATE_CHECKED);
                if (g_wiz->mqtt_fields_cont)
                    lv_obj_add_flag(g_wiz->mqtt_fields_cont, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // --- MQTT toggle event ---
    lv_obj_add_event_cb(wiz->sw_mqtt_en, [](lv_event_t* e) {
        if (!g_wiz || !g_wiz->mqtt_fields_cont) return;
        bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
        if (on) lv_obj_clear_flag(g_wiz->mqtt_fields_cont, LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag (g_wiz->mqtt_fields_cont, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ---------------------------------------------------------------------------
// Step 3 — Time zone
// ---------------------------------------------------------------------------
static void build_content_time(WizState* wiz) {
    using K = i18n::LangKey;
    lv_obj_t* c = wiz->content;

    make_field_lbl(c, i18n::tr(K::WIZ_UTC_LABEL));
    wiz->roller_utc = drum_roller::signed_offset(c, (int)wiz->utc_offset_min);

    make_field_lbl(c, i18n::tr(K::WIZ_NTP1_LBL));
    wiz->ta_ntp1 = make_ta(c, "e.g. pool.ntp.org", wiz->ntp1.c_str());
    bind_ta_keyboard(wiz->ta_ntp1);

    make_field_lbl(c, i18n::tr(K::WIZ_NTP2_LBL));
    wiz->ta_ntp2 = make_ta(c, "e.g. time.google.com", wiz->ntp2.c_str());
    bind_ta_keyboard(wiz->ta_ntp2);

    make_note(c, i18n::tr(K::NET_WIFI_DISABLED_NOTE));
}

// ---------------------------------------------------------------------------
// Step 4 — Location
// ---------------------------------------------------------------------------
static void build_content_location(WizState* wiz) {
    using K = i18n::LangKey;
    lv_obj_t* c = wiz->content;

    lv_obj_t* title = lv_label_create(c);
    lv_label_set_text(title, i18n::tr(K::WIZ_LOCATION));
    lv_obj_set_style_text_font(title, theme::font_heading(), 0);
    lv_obj_set_style_text_color(title, theme::color_text_primary(), 0);

    make_note(c, i18n::tr(K::WIZ_LOC_NOTE));

    char buf[24];

    make_field_lbl(c, i18n::tr(K::WIZ_LAT));
    snprintf(buf, sizeof(buf), "%.4f", (double)wiz->latitude);
    wiz->ta_lat = make_ta(c, "0.0000", buf);
    bind_ta_keyboard(wiz->ta_lat);

    make_field_lbl(c, i18n::tr(K::WIZ_LON));
    snprintf(buf, sizeof(buf), "%.4f", (double)wiz->longitude);
    wiz->ta_lon = make_ta(c, "0.0000", buf);
    bind_ta_keyboard(wiz->ta_lon);
}

// ---------------------------------------------------------------------------
// Step 5 — Done
// ---------------------------------------------------------------------------
static void build_content_done(WizState* wiz) {
    using K = i18n::LangKey;
    lv_obj_t* c = wiz->content;

    lv_obj_t* icon = lv_label_create(c);
    lv_label_set_text(icon, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(icon, theme::font_heading(), 0);
    lv_obj_set_style_text_color(icon, theme::color_success(), 0);

    lv_obj_t* title = lv_label_create(c);
    lv_label_set_text(title, i18n::tr(i18n::LangKey::WIZ_DONE));
    lv_obj_set_style_text_font(title, theme::font_heading(), 0);
    lv_obj_set_style_text_color(title, theme::color_text_primary(), 0);

    // Summary card
    lv_obj_t* card = lv_obj_create(c);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, theme::color_outline(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, theme::RADIUS_MD, 0);
    lv_obj_set_style_pad_all(card, theme::PAD_MD, 0);
    lv_obj_set_style_pad_row(card, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(card, theme::PAD_MD, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    char buf[128];
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  WiFi: %s",
             wiz->ssid.empty() ? i18n::tr(K::WIZ_AP_MODE_ONLY) : wiz->ssid.c_str());
    lv_obj_t* r1 = lv_label_create(card);
    lv_label_set_text(r1, buf);
    lv_obj_set_style_text_font(r1, theme::font_body(), 0);
    lv_obj_set_style_text_color(r1, theme::color_text_secondary(), 0);

    int hh = wiz->utc_offset_min / 60;
    int mm = wiz->utc_offset_min % 60;
    if (mm < 0) mm = -mm;
    snprintf(buf, sizeof(buf),
             LV_SYMBOL_REFRESH "  UTC %+d:%02d", hh, mm);
    lv_obj_t* r2 = lv_label_create(card);
    lv_label_set_text(r2, buf);
    lv_obj_set_style_text_font(r2, theme::font_body(), 0);
    lv_obj_set_style_text_color(r2, theme::color_text_secondary(), 0);

    snprintf(buf, sizeof(buf), LV_SYMBOL_CALL "  NTP: %s", wiz->ntp1.c_str());
    lv_obj_t* r3 = lv_label_create(card);
    lv_label_set_text(r3, buf);
    lv_obj_set_style_text_font(r3, theme::font_body(), 0);
    lv_obj_set_style_text_color(r3, theme::color_text_secondary(), 0);

    make_note(c, i18n::tr(K::WIZ_READY_NOTE));
}

// ---------------------------------------------------------------------------
// Step content dispatcher — clears content area and rebuilds for new step
// ---------------------------------------------------------------------------
static void build_step_content(WizState* wiz) {
    wiz->sw_wifi_en          = nullptr;
    wiz->wifi_fields_cont    = nullptr;
    wiz->sw_mqtt_en          = nullptr;
    wiz->mqtt_fields_cont    = nullptr;
    wiz->ta_ssid             = nullptr;
    wiz->ta_pw               = nullptr;
    wiz->scan_btn            = nullptr;
    wiz->scan_list           = nullptr;
    wiz->scan_status_lbl     = nullptr;
    wiz->ta_mqtt_uri         = nullptr;
    wiz->ta_mqtt_user        = nullptr;
    wiz->ta_mqtt_pw          = nullptr;
    wiz->roller_utc          = nullptr;
    wiz->ta_ntp1             = nullptr;
    wiz->ta_ntp2             = nullptr;
    wiz->ta_lat              = nullptr;
    wiz->ta_lon              = nullptr;

    if (wiz->keyboard) {
        lv_obj_add_flag(wiz->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(wiz->keyboard, nullptr);
    }

    lv_obj_clean(wiz->content);

    switch (wiz->step) {
        case 0: build_content_language(wiz); break;
        case 1: build_content_welcome(wiz);  break;
        case 2: build_content_wifi(wiz);     break;
        case 3: build_content_time(wiz);     break;
        case 4: build_content_location(wiz); break;
        case 5: build_content_done(wiz);     break;
        default: break;
    }

    lv_obj_scroll_to_y(wiz->content, 0, LV_ANIM_OFF);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

static void apply_and_dismiss(WizState* wiz) {
    auto* ctx = aqua::ui::ui_context().sys_cfg;

    // --- WiFi ---
    if (ctx) ctx->wifi_enabled = wiz->wifi_en;
    if (wiz->wifi_en) {
        if (!wiz->ssid.empty()) {
            aqua::wifi::StationCfg scfg;
            scfg.ssid     = wiz->ssid;
            scfg.password = wiz->wifi_pw;
            aqua::wifi::save_station_cfg(scfg);
            aqua::wifi::stop();
            aqua::wifi::start_station(scfg);
        } else {
            aqua::wifi::stop();
            aqua::wifi::start_ap_fallback();
        }
    } else {
        aqua::wifi::stop();
    }

    // --- MQTT ---
    if (ctx) ctx->mqtt_enabled = wiz->mqtt_en && wiz->wifi_en;
    if (wiz->mqtt_en && wiz->wifi_en && !wiz->mqtt_uri.empty()) {
        aqua::mqtt::Config mc;
        mc.uri        = wiz->mqtt_uri;
        mc.user       = wiz->mqtt_user;
        mc.pass       = wiz->mqtt_pw;
        mc.base_topic = "aquacontrol";
        mc.ha_discovery = true;
        aqua::mqtt::save_config(mc);
        aqua::mqtt::start(mc);
    }

    // --- System config ---
    if (ctx) {
        ctx->utc_offset_min     = wiz->utc_offset_min;
        ctx->ntp1               = wiz->ntp1;
        ctx->ntp2               = wiz->ntp2;
        ctx->latitude           = wiz->latitude;
        ctx->longitude          = wiz->longitude;
        ctx->first_run_complete = true;
        ctx->language           = static_cast<aqua::storage::Language>(
                                      static_cast<uint8_t>(wiz->lang));
        aqua::storage::save_system_config(*ctx);
    }
    screen_manager::pop(screen_manager::Transition::FADE);
}

static void go_to_step(WizState* wiz, int new_step) {
    collect_step_values(wiz);
    wiz->step = new_step;
    build_step_content(wiz);
    update_dots(wiz);
    update_nav(wiz);
}

static void on_back(lv_event_t* /*e*/) {
    if (!g_wiz || g_wiz->step <= 0) return;
    go_to_step(g_wiz, g_wiz->step - 1);
}

static void on_next(lv_event_t* /*e*/) {
    if (!g_wiz) return;
    if (g_wiz->step == kSteps - 1)
        apply_and_dismiss(g_wiz);
    else
        go_to_step(g_wiz, g_wiz->step + 1);
}

static void on_skip(lv_event_t* /*e*/) {
    if (!g_wiz) return;
    go_to_step(g_wiz, g_wiz->step + 1);
}

// ---------------------------------------------------------------------------
// Nav-bar button factory
// ---------------------------------------------------------------------------
static lv_obj_t* make_nav_btn(lv_obj_t* parent, const char* text,
                               lv_color_t bg, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 52);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, theme::RADIUS_MD, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, theme::PAD_LG, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

lv_obj_t* build() {
    // Must hold the LVGL mutex: build() is called from Core 0 (app_main)
    // while the LVGL task runs on Core 1.  Any lv_obj_create without the
    // lock races with the LVGL timer → heap metadata corruption.
    if (!lvgl_port_lock(1000)) {
        AC_LOGE(TAG, "build: LVGL lock timeout");
        return nullptr;
    }
    if (g_wiz) { delete g_wiz; g_wiz = nullptr; }

    g_wiz = new WizState();

    // Pre-populate from saved config.
    auto* cfg = aqua::ui::ui_context().sys_cfg;
    if (cfg) {
        g_wiz->utc_offset_min = cfg->utc_offset_min;
        g_wiz->ntp1           = cfg->ntp1;
        g_wiz->ntp2           = cfg->ntp2;
        g_wiz->lang           = static_cast<aqua::ui::i18n::Language>(
                                    static_cast<uint8_t>(cfg->language));
        g_wiz->wifi_en        = cfg->wifi_enabled;
        g_wiz->mqtt_en        = cfg->mqtt_enabled;
        g_wiz->latitude       = cfg->latitude;
        g_wiz->longitude      = cfg->longitude;
    }
    {
        aqua::wifi::StationCfg sc;
        if (aqua::wifi::load_station_cfg(&sc) == ESP_OK) {
            g_wiz->ssid    = sc.ssid;
            g_wiz->wifi_pw = sc.password;
        }
    }
    {
        aqua::mqtt::Config mc;
        if (aqua::mqtt::load_config(&mc) == ESP_OK) {
            g_wiz->mqtt_uri  = mc.uri;
            g_wiz->mqtt_user = mc.user;
            g_wiz->mqtt_pw   = mc.pass;
        }
    }

    // ---- Root screen ----
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, kDispW, kDispH);
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_wiz->root = root;

    // Free state when root is deleted.
    lv_obj_add_event_cb(root, [](lv_event_t* /*e*/) {
        // Tear down the keyboard overlay (lives on lv_layer_top, not on
        // the wizard root, so it would otherwise leak).
        if (g_wiz && g_wiz->keyboard) {
            lv_obj_del(g_wiz->keyboard);
            g_wiz->keyboard = nullptr;
        }
        delete g_wiz;
        g_wiz = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    // ---- Header bar (48 px) ----
    lv_obj_t* hdr = lv_obj_create(root);
    lv_obj_set_size(hdr, kDispW, kHdrH);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, theme::color_outline(), 0);
    lv_obj_set_style_pad_hor(hdr, theme::PAD_LG, 0);
    lv_obj_set_style_pad_ver(hdr, 0, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    g_wiz->hdr_bar = hdr;

    lv_obj_t* fixed_lbl = lv_label_create(hdr);
    lv_label_set_text(fixed_lbl, i18n::tr(i18n::LangKey::WIZ_FIRST_RUN));
    lv_obj_set_style_text_font(fixed_lbl, theme::font_caption(), 0);
    lv_obj_set_style_text_color(fixed_lbl, theme::color_text_disabled(), 0);

    lv_obj_t* step_lbl = lv_label_create(hdr);
    lv_label_set_text(step_lbl, "Welcome");
    lv_obj_set_style_text_font(step_lbl, theme::font_title(), 0);
    lv_obj_set_style_text_color(step_lbl, theme::color_accent(), 0);
    g_wiz->hdr_lbl = step_lbl;

    // ---- Scrollable content area (360 px) ----
    lv_obj_t* cont = lv_obj_create(root);
    lv_obj_set_size(cont, kDispW, kContentH);
    lv_obj_set_pos(cont, 0, kHdrH);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_hor(cont, 160, 0);  // wide horizontal margins
    lv_obj_set_style_pad_top(cont, theme::PAD_LG, 0);
    lv_obj_set_style_pad_bottom(cont, theme::PAD_LG, 0);
    lv_obj_set_style_pad_row(cont, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    g_wiz->content = cont;

    // ---- Nav bar (72 px) ----
    lv_obj_t* nav = lv_obj_create(root);
    lv_obj_set_size(nav, kDispW, kNavH);
    lv_obj_set_pos(nav, 0, kHdrH + kContentH);
    lv_obj_set_style_bg_color(nav, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav, 1, 0);
    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(nav, theme::color_outline(), 0);
    lv_obj_set_style_pad_hor(nav, theme::PAD_LG, 0);
    lv_obj_set_style_pad_ver(nav, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    g_wiz->nav_bar = nav;

    // Back button (hidden on step 0)
    g_wiz->btn_back = make_nav_btn(nav, LV_SYMBOL_LEFT "  Back",
                                   theme::color_surface_alt(), on_back);
    lv_obj_add_flag(g_wiz->btn_back, LV_OBJ_FLAG_HIDDEN);

    // Centre: Skip button + step dots
    lv_obj_t* centre = lv_obj_create(nav);
    lv_obj_set_height(centre, kNavH);
    lv_obj_set_flex_grow(centre, 1);
    lv_obj_set_style_bg_opa(centre, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(centre, 0, 0);
    lv_obj_set_style_pad_all(centre, 0, 0);
    lv_obj_set_flex_flow(centre, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(centre, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(centre, theme::PAD_SM, 0);

    // Skip button (WiFi step only)
    g_wiz->btn_skip = lv_btn_create(centre);
    lv_obj_set_size(g_wiz->btn_skip, 80, 40);
    lv_obj_set_style_bg_color(g_wiz->btn_skip, theme::color_surface_alt(), 0);
    lv_obj_set_style_radius(g_wiz->btn_skip, theme::RADIUS_SM, 0);
    lv_obj_set_style_border_width(g_wiz->btn_skip, 0, 0);
    lv_obj_t* skip_lbl = lv_label_create(g_wiz->btn_skip);
    lv_label_set_text(skip_lbl, i18n::tr(i18n::LangKey::WIZ_SKIP));
    lv_obj_set_style_text_font(skip_lbl, theme::font_caption(), 0);
    lv_obj_set_style_text_color(skip_lbl, theme::color_text_secondary(), 0);
    lv_obj_center(skip_lbl);
    lv_obj_add_event_cb(g_wiz->btn_skip, on_skip, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(g_wiz->btn_skip, LV_OBJ_FLAG_HIDDEN);

    // Dots
    for (int i = 0; i < kSteps; ++i) {
        lv_obj_t* dot = lv_obj_create(centre);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, theme::color_outline(), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        g_wiz->dots[i] = dot;
    }

    // Next / Finish button
    g_wiz->btn_next = make_nav_btn(nav, "Next  " LV_SYMBOL_RIGHT,
                                   theme::color_accent(), on_next);

    // ---- Keyboard overlay (hidden by default) ----
    //
    // Created on lv_layer_top() so it is rendered above any wizard
    // content regardless of z-order, screen transitions or parent
    // scroll/clip rules. Earlier attempts with the keyboard as a child
    // of `root` produced the bug where FOCUSED/CLICKED fire on the TA
    // and `LV_OBJ_FLAG_HIDDEN` is cleared, but the keyboard remains
    // invisible (most likely because something in the wizard's nested
    // scrollable layout or the FADE_IN screen transition was covering
    // or clipping it). The top layer is exempt from all of that.
    lv_obj_t* kbd = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(kbd, kDispW, 220);
    // Use align rather than set_pos: set_pos is parent-relative and
    // lv_layer_top() is NOT guaranteed to sit at display (0,0) under
    // every theme/screen-transition state (we observed it being offset
    // by +260 px, putting the keyboard entirely below the screen).
    // ALIGN_BOTTOM_MID pins the keyboard to the bottom of its parent
    // regardless of where the parent itself is.
    lv_obj_align(kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
    // Use theme colors matching the rest of the UI.
    lv_obj_set_style_bg_color(kbd, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(kbd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kbd, 1, 0);
    lv_obj_set_style_border_color(kbd, theme::color_outline(), 0);
    // Button key styling.
    lv_obj_set_style_bg_color(kbd, theme::color_surface_alt(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kbd, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kbd, theme::color_text_primary(), LV_PART_ITEMS);
    lv_obj_set_style_border_color(kbd, theme::color_outline(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kbd, 1, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kbd, theme::color_accent(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kbd, theme::color_background(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_flag(kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(kbd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(kbd, [](lv_event_t* /*ev*/) {
        if (g_wiz && g_wiz->keyboard) {
            lv_obj_add_flag(g_wiz->keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(g_wiz->keyboard, nullptr);
        }
    }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(kbd, [](lv_event_t* /*ev*/) {
        if (g_wiz && g_wiz->keyboard) {
            lv_obj_add_flag(g_wiz->keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(g_wiz->keyboard, nullptr);
        }
    }, LV_EVENT_CANCEL, nullptr);
    g_wiz->keyboard = kbd;

    // ---- Build initial step ----
    build_step_content(g_wiz);
    update_dots(g_wiz);
    update_nav(g_wiz);

    AC_LOGI(TAG, "Wizard built (single-screen)");
    lvgl_port_unlock();
    return root;
}

}  // namespace aqua::ui::wizard
