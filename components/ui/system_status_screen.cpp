// AquaControl — System Status screen implementation.
//
// Read-only information screen; shows:
//   * Firmware version (IDF_VER + project version from esp_app_desc)
//   * Active faults (from faults::active())
//   * Recent event history (latest 20 entries from history::recent())
//   * [Restart] and [Factory Reset] action buttons
//     - Restart: calls esp_restart() after a 1 s delay via esp_timer
//     - Factory Reset: shows a confirmation msgbox; on confirm erases NVS
//       and reboots
#include "system_status_screen.h"

#include <cstdio>
#include <ctime>

#include "ac_logger.h"
#include "chrome.h"
#include "device_manager.h"
#include "esp_app_desc.h"
#include "i18n.h"
#include "esp_idf_version.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "faults.h"
#include "history_log.h"
#include "nvs_flash.h"
#include "screen_manager.h"
#include "theme.h"
#include "trigger_manager.h"
#include "ui_context.h"
#include "wifi_manager.h"

namespace aqua::ui::system_status_screen {

namespace {

using aqua::ui::i18n::LangKey;
using aqua::ui::i18n::tr;

constexpr const char* TAG = "SysStatus";

// ---- helpers ---------------------------------------------------------------

static lv_obj_t* make_section_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_title(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_accent(), 0);
    return lbl;
}

static lv_obj_t* make_body_label(lv_obj_t* parent, const char* text,
                                  lv_color_t col) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl, col, 0);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    return lbl;
}

static lv_obj_t* make_action_button(lv_obj_t* parent, const char* text,
                                     lv_color_t bg, lv_event_cb_t cb, void* ud) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, theme::RADIUS_MD, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
    lv_obj_center(lbl);

    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

// ---- restart ---------------------------------------------------------------

static void restart_timer_cb(void* /*arg*/) {
    esp_restart();
}

static void on_restart(lv_event_t* /*e*/) {
    AC_LOGW(TAG, "User-requested restart");
    static esp_timer_handle_t t = nullptr;
    if (!t) {
        esp_timer_create_args_t a = {restart_timer_cb, nullptr,
                                     ESP_TIMER_TASK, "restart_ui", false};
        esp_timer_create(&a, &t);
    }
    esp_timer_start_once(t, 1000 * 1000);  // 1 s
}

// ---- factory reset ---------------------------------------------------------

static void on_factory_confirm(lv_event_t* e) {
    // Close the msgbox first.
    lv_obj_t* msgbox = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    if (msgbox) lv_msgbox_close(msgbox);
    AC_LOGW(TAG, "Factory reset: erasing NVS");
    nvs_flash_erase();
    // Reboot after short delay.
    static esp_timer_handle_t t = nullptr;
    if (!t) {
        esp_timer_create_args_t a = {restart_timer_cb, nullptr,
                                     ESP_TIMER_TASK, "frsrst_ui", false};
        esp_timer_create(&a, &t);
    }
    esp_timer_start_once(t, 1000 * 1000);
}

static void on_factory_reset(lv_event_t* /*e*/) {
    static const char* btns[] = {"Cancel", "Reset", ""};
    lv_obj_t* mb = lv_msgbox_create(lv_layer_top());
    lv_msgbox_add_title(mb, tr(LangKey::SYS_FACTORY_RESET));
    lv_msgbox_add_text(mb, tr(LangKey::SYS_FACTORY_CONFIRM));
    lv_obj_t* btn_cancel  = lv_msgbox_add_footer_button(mb, tr(LangKey::BTN_CANCEL));
    lv_obj_t* btn_confirm = lv_msgbox_add_footer_button(mb, tr(LangKey::SYS_FACTORY_RESET));
    (void)btns;

    // Style the confirm button red.
    lv_obj_set_style_bg_color(btn_confirm, theme::color_error(), 0);

    // Cancel just closes.
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* ev) {
        lv_msgbox_close(static_cast<lv_obj_t*>(lv_event_get_user_data(ev)));
    }, LV_EVENT_CLICKED, mb);

    lv_obj_add_event_cb(btn_confirm, on_factory_confirm, LV_EVENT_CLICKED, mb);
}

}  // namespace

// ---------------------------------------------------------------------------
// populate_scroll() — fills the scroll container with fresh data.
// Called both from build() and from on_screen_loaded().
// ---------------------------------------------------------------------------

static void populate_scroll(lv_obj_t* scroll) {
    lv_obj_clean(scroll);

    // ------- Firmware / system info section -------
    make_section_label(scroll, tr(LangKey::SYS_FIRMWARE));
    {
        const esp_app_desc_t* desc = esp_app_get_description();
        uint64_t up_us = esp_timer_get_time();
        uint32_t up_s  = (uint32_t)(up_us / 1000000ULL);
        uint32_t up_m  = up_s / 60;  up_s %= 60;
        uint32_t up_h  = up_m / 60;  up_m %= 60;

        int8_t rssi = aqua::wifi::sta_rssi();
        char wifi_buf[32];
        if (rssi == 0) {
            snprintf(wifi_buf, sizeof(wifi_buf), "%s", tr(LangKey::SYS_NOT_CONNECTED));
        } else {
            snprintf(wifi_buf, sizeof(wifi_buf), "%d dBm", (int)rssi);
        }

        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Version: %s\nIDF: %s\nDate: %s %s\n"
                 "Uptime: %lu h %02lu m %02lu s\n"
                 "Free heap: %lu B\n"
                 "WiFi signal: %s",
                 desc->version, desc->idf_ver, desc->date, desc->time,
                 (unsigned long)up_h, (unsigned long)up_m, (unsigned long)up_s,
                 (unsigned long)esp_get_free_heap_size(),
                 wifi_buf);
        make_body_label(scroll, buf, theme::color_text_secondary());
    }

    // ------- Resources section -------
    make_section_label(scroll, tr(LangKey::SYS_RESOURCES));
    {
        const auto& uic = ui_context();
        size_t dev_count  = uic.devices  ? uic.devices->size()  : 0;
        size_t trg_count  = uic.triggers ? uic.triggers->size() : 0;

        // Count free relay and PWM channels.
        int relay_used = 0, pwm_used = 0;
        if (uic.devices) {
            for (const auto& d : uic.devices->all()) {
                if (!d) continue;
                if (d->get_type() == aqua::devices::DeviceType::RELAY) ++relay_used;
                if (d->get_type() == aqua::devices::DeviceType::PWM)   ++pwm_used;
                if (d->get_type() == aqua::devices::DeviceType::RGB)   pwm_used += 3;
            }
        }
        int relay_free = 16 - relay_used;
        int pwm_free   = 16 - pwm_used;

        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Devices: %u  (relay slots free: %d, PWM slots free: %d)\n"
                 "Triggers: %u\n"
                 "Min free heap: %lu B\n"
                 "PSRAM free: %lu B",
                 (unsigned)dev_count, relay_free, pwm_free,
                 (unsigned)trg_count,
                 (unsigned long)esp_get_minimum_free_heap_size(),
                 (unsigned long)esp_get_free_heap_size());
        lv_color_t col = (relay_free <= 2 || pwm_free <= 2)
            ? theme::color_warning()
            : theme::color_text_secondary();
        make_body_label(scroll, buf, col);
    }

    // ------- Active Faults section -------
    make_section_label(scroll, tr(LangKey::SYS_ACTIVE_FAULTS));
    {
        auto flist = aqua::faults::active();
        if (flist.empty()) {
            make_body_label(scroll, tr(LangKey::SYS_NO_FAULTS), theme::color_success());
        } else {
            for (const auto& f : flist) {
                char row[96];
                snprintf(row, sizeof(row), "[%04X] %s", (unsigned)f.code,
                         f.label.c_str());
                make_body_label(scroll, row, theme::color_error());
            }
        }
    }

    // ------- Recent Events -------
    make_section_label(scroll, tr(LangKey::SYS_RECENT_EVENTS));
    {
        // M-7: cap at 200 events to avoid OOM from loading the entire 96 KB
        // events.log into heap on devices with fragmented memory after 12+ hours.
        auto evts = aqua::history::recent(200);
        if (evts.empty()) {
            make_body_label(scroll, tr(LangKey::SYS_NO_EVENTS), theme::color_text_disabled());
        } else {
            for (const auto& ev : evts) {
                char row[120];
                if (ev.ts == 0) {
                    snprintf(row, sizeof(row), "(no time)  %s", ev.msg.c_str());
                } else {
                    struct tm tm_s{};
                    localtime_r(&ev.ts, &tm_s);
                    char ts[20];
                    strftime(ts, sizeof(ts), "%m-%d %H:%M:%S", &tm_s);
                    snprintf(row, sizeof(row), "%s  %s", ts, ev.msg.c_str());
                }
                make_body_label(scroll, row, theme::color_text_secondary());
            }
        }
    }

    // ------- Actions -------
    make_section_label(scroll, tr(LangKey::SYS_ACTIONS));

    {
        char restart_lbl[64];
        snprintf(restart_lbl, sizeof(restart_lbl), "%s  %s", LV_SYMBOL_REFRESH, tr(LangKey::SYS_RESTART));
        make_action_button(scroll, restart_lbl,
                           theme::color_surface_alt(),
                           on_restart, nullptr);
    }
    {
        char factory_lbl[64];
        snprintf(factory_lbl, sizeof(factory_lbl),
                 LV_SYMBOL_TRASH "  %s", tr(LangKey::SYS_FACTORY_RESET));
        make_action_button(scroll, factory_lbl,
                           theme::color_error(),
                           on_factory_reset, nullptr);
    }
}

static void on_screen_loaded(lv_event_t* e) {
    lv_obj_t* scroll = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    populate_scroll(scroll);
}

// ---------------------------------------------------------------------------
// build()
// ---------------------------------------------------------------------------

lv_obj_t* build() {
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    chrome::build(root, tr(LangKey::NAV_SYSTEM), chrome::pop_on_back);

    // Scroll area.
    lv_obj_t* scroll = lv_obj_create(root);
    lv_obj_set_size(scroll, LV_PCT(100), 480 - chrome::kHeaderH);
    lv_obj_align(scroll, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, theme::PAD_MD, 0);
    lv_obj_set_style_pad_row(scroll, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Populate once at build, then refresh on every screen load.
    populate_scroll(scroll);
    lv_obj_add_event_cb(root, on_screen_loaded, LV_EVENT_SCREEN_LOADED, scroll);

    return root;
}

}  // namespace aqua::ui::system_status_screen

