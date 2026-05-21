// AquaControl — Time & Location settings screen implementation.
//
// Allows the user to configure:
//   * UTC offset in minutes (via drum_roller::signed_offset — –360..+360, step 5)
//   * NTP server 1 and 2 (text inputs)
//   * Latitude and Longitude (text inputs; used by SolarCalculator)
//
// Save — persists to SystemConfig via storage::save_system_config() and
// applies the new UTC offset immediately via TimeManager.
#include "time_location_screen.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "ac_logger.h"
#include "chrome.h"
#include "drum_roller.h"
#include "ntp_sync.h"
#include "screen_manager.h"
#include "solar_calc.h"
#include "system_config.h"
#include "theme.h"
#include "time_manager.h"
#include "i18n.h"
#include "ui_context.h"

namespace aqua::ui::time_location_screen {

namespace {

constexpr const char* TAG = "TimeLocSettings";

struct State {
    lv_obj_t* roller_utc  = nullptr;   // drum_roller::signed_offset
    lv_obj_t* ta_ntp1     = nullptr;
    lv_obj_t* ta_ntp2     = nullptr;
    lv_obj_t* ta_lat      = nullptr;
    lv_obj_t* ta_lon      = nullptr;
    lv_obj_t* keyboard    = nullptr;
    lv_obj_t* scroll_view = nullptr;   // main scrollable container
    lv_obj_t* kb_overlay  = nullptr;   // transparent dismiss overlay on lv_layer_top()
    lv_obj_t* lbl_sync_result = nullptr;  // shows NTP sync feedback

    // Calendar for date picking + drum rollers for H/M
    lv_obj_t* cal         = nullptr;   // lv_calendar date picker
    int sel_year  = 2026;              // currently selected calendar date
    int sel_month = 1;
    int sel_day   = 1;
    lv_obj_t* roller_time = nullptr;   // drum_roller::time_hhmm container
    lv_obj_t* lbl_cur_time = nullptr;  // shows current system time

    // keep a copy of the loaded config so we can carry over fields we don't
    // expose here (e.g. brightness settings).
    aqua::storage::SystemConfig cfg;
};

// ---------------------------------------------------------------------------
// Shared textarea factory (same style as network_settings_screen)
// ---------------------------------------------------------------------------

static lv_obj_t* make_textarea(lv_obj_t* parent,
                                const char* placeholder,
                                const char* initial) {
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, 44);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (initial && initial[0]) lv_textarea_set_text(ta, initial);

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

static lv_obj_t* make_field_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
    return lbl;
}

static lv_obj_t* make_section_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_title(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_accent(), 0);
    return lbl;
}

// ---------------------------------------------------------------------------
// Keyboard binding
// ---------------------------------------------------------------------------

static void on_ta_clicked(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st || !st->keyboard) return;
    lv_obj_t* ta = lv_event_get_target_obj(e);
    // Don't show keyboard for disabled fields (wifi-off NTP fields, etc.)
    if (lv_obj_has_state(ta, LV_STATE_DISABLED)) return;
    lv_keyboard_set_textarea(st->keyboard, ta);
    lv_obj_remove_flag(st->keyboard, LV_OBJ_FLAG_HIDDEN);
    // Resize viewport above the keyboard. update_layout() flushes child
    // positions before scroll_to_view so the scroll target is accurate.
    if (st->scroll_view) {
        lv_obj_set_size(st->scroll_view, LV_PCT(100), 480 - chrome::kHeaderH - 220);
        lv_obj_update_layout(st->scroll_view);
    }
    lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
}

// (kb_overlay removed — it was on lv_layer_top() and blocked all scroll
// gestures in the content area.  Keyboard is now dismissed via OK/Cancel.)

static void on_kb_ready(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st || !st->keyboard) return;
    lv_obj_add_flag(st->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(st->keyboard, nullptr);
    if (st->scroll_view)
        lv_obj_set_size(st->scroll_view, LV_PCT(100), 480 - chrome::kHeaderH);
}

static void bind_textarea(lv_obj_t* ta, State* st) {
    // LV_EVENT_CLICKED (not PRESSED): firing on PRESSED would call
    // lv_obj_scroll_to_view() while the user's finger is still down,
    // which fights any in-progress scroll gesture and produces a
    // visible "jump" of the entire form. CLICKED only fires when the
    // finger lifts without movement, so a scroll-drag that happens to
    // start on a textarea no longer hijacks the screen.
    lv_obj_add_event_cb(ta, on_ta_clicked, LV_EVENT_CLICKED, st);
}

// Visually disable an lv_obj and prevent all touch interaction.
static void disable_widget(lv_obj_t* w) {
    lv_obj_add_state(w, LV_STATE_DISABLED);
    lv_obj_set_style_opa(w, LV_OPA_40, 0);
    lv_obj_clear_flag(w, LV_OBJ_FLAG_CLICKABLE);
}

// ---------------------------------------------------------------------------
// Apply manual time
// ---------------------------------------------------------------------------

static void apply_manual_time(State* st) {
    int year  = st->sel_year;
    int month = st->sel_month;
    int day   = st->sel_day;
    int hour  = 0, min = 0;
    drum_roller::get_time_hhmm(st->roller_time, &hour, &min);

    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = 0;

    esp_err_t r = aqua::time_mgr::TimeManager::set_time(t);
    if (r == ESP_OK) {
        if (st->lbl_cur_time) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Set: %04d-%02d-%02d  %02d:%02d",
                     year, month, day, hour, min);
            lv_label_set_text(st->lbl_cur_time, buf);
            lv_obj_set_style_text_color(st->lbl_cur_time,
                                        theme::color_success(), 0);
        }
        AC_LOGI(TAG, "Manual time set: %04d-%02d-%02d %02d:%02d",
                year, month, day, hour, min);
    } else {
        AC_LOGE(TAG, "set_time failed: %s", esp_err_to_name(r));
    }
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

static void do_save(State* st) {
    // UTC offset from roller.
    int new_offset = aqua::ui::drum_roller::get_signed_offset(st->roller_utc);
    st->cfg.utc_offset_min = (int16_t)new_offset;

    // NTP servers.
    const char* ntp1 = lv_textarea_get_text(st->ta_ntp1);
    const char* ntp2 = lv_textarea_get_text(st->ta_ntp2);
    if (ntp1 && ntp1[0]) st->cfg.ntp1 = ntp1;
    if (ntp2 && ntp2[0]) st->cfg.ntp2 = ntp2;

    // Location (atof — safe for boundary input, no abort).
    const char* lat_s = lv_textarea_get_text(st->ta_lat);
    const char* lon_s = lv_textarea_get_text(st->ta_lon);
    if (lat_s) st->cfg.latitude  = (float)atof(lat_s);
    if (lon_s) st->cfg.longitude = (float)atof(lon_s);

    // Persist.
    esp_err_t r = aqua::storage::save_system_config(st->cfg);
    if (r != ESP_OK) {
        AC_LOGE(TAG, "save_system_config failed: %s", esp_err_to_name(r));
    }

    // Apply UTC offset immediately (no reboot needed).
    aqua::time_mgr::TimeManager::set_utc_offset_minutes(st->cfg.utc_offset_min);
    // Request a solar recalculation — lat/lon or timezone may have changed.
    aqua::solar::request_recalc();
    AC_LOGI(TAG, "Time/location settings saved; UTC offset=%d min", (int)st->cfg.utc_offset_min);
}

static void on_save(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st) return;
    if (st->keyboard) lv_obj_add_flag(st->keyboard, LV_OBJ_FLAG_HIDDEN);
    do_save(st);
    screen_manager::pop(screen_manager::Transition::SLIDE_RIGHT);
}

// ---------------------------------------------------------------------------
// Screen lifecycle
// ---------------------------------------------------------------------------

static void on_screen_delete(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (st && st->kb_overlay) {
        lv_obj_del(st->kb_overlay);
        st->kb_overlay = nullptr;
    }
    delete st;
}

}  // namespace

// ---------------------------------------------------------------------------
// build()
// ---------------------------------------------------------------------------

lv_obj_t* build() {
    auto* st = new State();

    // Load current config (ignore errors — fields keep defaults).
    aqua::storage::load_system_config(&st->cfg);

    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, on_screen_delete, LV_EVENT_DELETE, st);

    // Header.
    chrome::Header hdr = chrome::build(root, i18n::tr(i18n::LangKey::NAV_TIME),
                                       chrome::pop_on_back,
                                       i18n::tr(i18n::LangKey::BTN_SAVE),
                                       on_save);
    if (hdr.btn_action) {
        // chrome::build registers on_save with user_data == nullptr; swap it
        // for our State pointer so the handler can locate widgets.
        lv_obj_remove_event_cb(hdr.btn_action, on_save);
        lv_obj_add_event_cb(hdr.btn_action, on_save, LV_EVENT_CLICKED, st);
    }

    // Scroll area.
    lv_obj_t* scroll = lv_obj_create(root);
    lv_obj_set_size(scroll, LV_PCT(100),
                    480 - chrome::kHeaderH);
    // TOP anchor so shrinking height keeps top pinned below the header.
    lv_obj_align(scroll, LV_ALIGN_TOP_MID, 0, chrome::kHeaderH);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, theme::PAD_MD, 0);
    lv_obj_set_style_pad_row(scroll, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    // This screen has the tallest scrollable content in the app
    // (date+time spinboxes, UTC roller, NTP, lat/lon, note ~600 px in a
    // 424 px viewport).  A full-viewport repaint every frame during the
    // momentum animation saturates the PSRAM bus that the LCD bounce
    // buffer also reads from, and the panel loses hsync alignment — what
    // the user sees as a left-shifted block that "remains in that
    // position".  Killing momentum/elastic and the auto-hide scrollbar
    // drops the heavy work to only the actual finger-drag frames.
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(scroll, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_remove_flag(scroll, LV_OBJ_FLAG_SCROLL_ELASTIC);
    st->scroll_view = scroll;

    // ------- Set Time Manually section -------
    make_section_label(scroll, i18n::tr(i18n::LangKey::WIZ_SET_TIME));

    // Current time display.
    {
        struct tm now_t = aqua::time_mgr::TimeManager::now_local();
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "%s %04d-%02d-%02d  %02d:%02d",
                 i18n::tr(i18n::LangKey::TIME_CURRENT),
                 now_t.tm_year + 1900, now_t.tm_mon + 1, now_t.tm_mday,
                 now_t.tm_hour, now_t.tm_min);
        st->lbl_cur_time = lv_label_create(scroll);
        lv_label_set_text(st->lbl_cur_time, tbuf);
        lv_obj_set_style_text_font(st->lbl_cur_time, theme::font_body(), 0);
        lv_obj_set_style_text_color(st->lbl_cur_time,
                                    theme::color_text_disabled(), 0);
    }

    // Calendar (left) + drum-roller time pickers (right) — side by side.
    {
        struct tm now_t = aqua::time_mgr::TimeManager::now_local();
        st->sel_year  = now_t.tm_year + 1900;
        st->sel_month = now_t.tm_mon + 1;
        st->sel_day   = now_t.tm_mday;

        // Static day-name arrays (Sunday-first, matches LVGL default).
        static const char* kDayEn[7] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
        static const char* kDayRu[7] = {
            "\xd0\x92\xd1\x81", "\xd0\x9f\xd0\xbd", "\xd0\x92\xd1\x82",
            "\xd0\xa1\xd1\x80", "\xd0\xa7\xd1\x82", "\xd0\x9f\xd1\x82",
            "\xd0\xa1\xd0\xb1"};  // Вс Пн Вт Ср Чт Пт Сб
        bool is_ru = (i18n::get_language() == i18n::Language::RU);

        // Outer row: calendar on the left, time-picker panel on the right.
        lv_obj_t* row = lv_obj_create(scroll);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, theme::PAD_MD, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        // ---- Calendar (fixed width, left child) ----
        st->cal = lv_calendar_create(row);
        lv_obj_set_size(st->cal, 430, 265);
        lv_calendar_set_today_date(st->cal, (uint32_t)st->sel_year,
                                    (uint32_t)st->sel_month, (uint32_t)st->sel_day);
        lv_calendar_set_month_shown(st->cal, (uint32_t)st->sel_year,
                                     (uint32_t)st->sel_month);
        lv_calendar_set_day_names(st->cal, is_ru ? kDayRu : kDayEn);
        lv_calendar_add_header_arrow(st->cal);

        // Style: match app theme.
        lv_obj_set_style_bg_color(st->cal, theme::color_surface(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(st->cal, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(st->cal, theme::color_outline(), LV_PART_MAIN);
        lv_obj_set_style_border_width(st->cal, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(st->cal, theme::RADIUS_MD, LV_PART_MAIN);
        lv_obj_set_style_pad_all(st->cal, theme::PAD_SM, LV_PART_MAIN);
        // Individual day cells
        lv_obj_set_style_text_font(st->cal, theme::font_body(), LV_PART_ITEMS);
        lv_obj_set_style_text_color(st->cal, theme::color_text_primary(), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(st->cal, LV_OPA_TRANSP, LV_PART_ITEMS);
        lv_obj_set_style_border_width(st->cal, 0, LV_PART_ITEMS);
        // Today: accent-colored text + bold font
        lv_obj_set_style_text_color(st->cal, theme::color_accent(),
                                     LV_PART_ITEMS | LV_STATE_FOCUSED);
        lv_obj_set_style_text_font(st->cal, theme::font_title(),
                                    LV_PART_ITEMS | LV_STATE_FOCUSED);
        // Selected (tapped) day: accent fill
        lv_obj_set_style_bg_color(st->cal, theme::color_accent(),
                                   LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(st->cal, LV_OPA_COVER,
                                 LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(st->cal, theme::color_background(),
                                     LV_PART_ITEMS | LV_STATE_CHECKED);
        // Press feedback
        lv_obj_set_style_bg_color(st->cal, theme::color_accent(),
                                   LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(st->cal, LV_OPA_COVER,
                                 LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(st->cal, theme::color_background(),
                                     LV_PART_ITEMS | LV_STATE_PRESSED);

        // Update sel_* whenever the user taps a day.
        lv_obj_add_event_cb(st->cal, [](lv_event_t* ev) {
            auto* s = static_cast<State*>(lv_event_get_user_data(ev));
            if (!s || !s->cal) return;
            lv_calendar_date_t d;
            if (lv_calendar_get_pressed_date(s->cal, &d) == LV_RESULT_OK) {
                s->sel_year  = (int)d.year;
                s->sel_month = (int)d.month;
                s->sel_day   = (int)d.day;
            }
        }, LV_EVENT_VALUE_CHANGED, st);

        // ---- Time-picker panel (flex-grow, right child) ----
        lv_obj_t* time_panel = lv_obj_create(row);
        lv_obj_set_flex_grow(time_panel, 1);
        lv_obj_set_height(time_panel, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(time_panel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(time_panel, 0, 0);
        lv_obj_set_style_pad_all(time_panel, 0, 0);
        lv_obj_set_style_pad_row(time_panel, theme::PAD_SM, 0);
        lv_obj_set_flex_flow(time_panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(time_panel, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(time_panel, LV_OBJ_FLAG_SCROLLABLE);

        // Sub-label "HH : MM"
        lv_obj_t* time_lbl = lv_label_create(time_panel);
        lv_label_set_text(time_lbl, i18n::tr(i18n::LangKey::TIME_HOUR));
        lv_obj_set_style_text_font(time_lbl, theme::font_caption(), 0);
        lv_obj_set_style_text_color(time_lbl, theme::color_text_secondary(), 0);

        // Drum rollers for HH:MM.
        st->roller_time = drum_roller::time_hhmm(time_panel,
                                                  now_t.tm_hour, now_t.tm_min);

        // Apply button — full width of the right panel.
        lv_obj_t* btn = lv_btn_create(time_panel);
        lv_obj_set_size(btn, LV_PCT(100), theme::TOUCH_MIN);
        lv_obj_set_style_bg_color(btn, theme::color_accent(), 0);
        lv_obj_set_style_radius(btn, theme::RADIUS_MD, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_t* btn_lbl = lv_label_create(btn);
        lv_label_set_text(btn_lbl, i18n::tr(i18n::LangKey::WIZ_APPLY));
        lv_obj_set_style_text_font(btn_lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(btn_lbl, theme::color_text_primary(), 0);
        lv_obj_center(btn_lbl);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* s = static_cast<State*>(lv_event_get_user_data(e));
            apply_manual_time(s);
        }, LV_EVENT_CLICKED, st);
    }

    // ------- Timezone section -------
    make_section_label(scroll, i18n::tr(i18n::LangKey::TZ_TITLE));

    make_field_label(scroll, i18n::tr(i18n::LangKey::TZ_UTC_OFFSET));
    // Roller shows –6:00…+6:00 in 5-min steps (H:MM format).
    st->roller_utc = drum_roller::signed_offset(scroll, st->cfg.utc_offset_min);

    // Helper: show "UTC+03:00" etc. as an informational label that updates
    // as the user scrolls.  Kept static for now — users can read the roller.

    // ------- NTP section -------
    make_section_label(scroll, i18n::tr(i18n::LangKey::TZ_NTP_TITLE));

    make_field_label(scroll, i18n::tr(i18n::LangKey::TZ_NTP1));
    st->ta_ntp1 = make_textarea(scroll, "pool.ntp.org", st->cfg.ntp1.c_str());
    bind_textarea(st->ta_ntp1, st);

    make_field_label(scroll, i18n::tr(i18n::LangKey::TZ_NTP2));
    st->ta_ntp2 = make_textarea(scroll, "time.google.com", st->cfg.ntp2.c_str());
    bind_textarea(st->ta_ntp2, st);

    // "Sync Now" button + result label.
    {
        lv_obj_t* row = lv_obj_create(scroll);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, theme::PAD_MD, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* btn = lv_btn_create(row);
        lv_obj_set_size(btn, 160, theme::TOUCH_MIN);
        lv_obj_set_style_bg_color(btn, theme::color_accent(), 0);
        lv_obj_set_style_bg_color(btn, theme::color_surface_alt(), LV_STATE_DISABLED);
        lv_obj_set_style_radius(btn, theme::RADIUS_MD, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_t* btn_lbl = lv_label_create(btn);
        lv_label_set_text(btn_lbl, i18n::tr(i18n::LangKey::TZ_SYNC_NOW));
        lv_obj_set_style_text_font(btn_lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(btn_lbl, theme::color_text_primary(), 0);
        lv_obj_center(btn_lbl);
        lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
            auto* s = static_cast<State*>(lv_event_get_user_data(ev));
            if (!s) return;
            // Save current NTP servers and UTC offset first.
            do_save(s);
            auto* sys = aqua::ui::ui_context().sys_cfg;
            bool wifi_on = sys && sys->wifi_enabled;
            if (!wifi_on) {
                lv_label_set_text(s->lbl_sync_result,
                                  i18n::tr(i18n::LangKey::TZ_SYNC_WIFI_OFF));
                lv_obj_set_style_text_color(s->lbl_sync_result,
                                            theme::color_error(), 0);
                return;
            }
            aqua::ntp::start(s->cfg.ntp1, s->cfg.ntp2);
            lv_label_set_text(s->lbl_sync_result,
                              i18n::tr(i18n::LangKey::TZ_SYNC_STARTED));
            lv_obj_set_style_text_color(s->lbl_sync_result,
                                        theme::color_warning(), 0);
        }, LV_EVENT_CLICKED, st);

        st->lbl_sync_result = lv_label_create(row);
        lv_label_set_text(st->lbl_sync_result, "");
        lv_obj_set_style_text_font(st->lbl_sync_result, theme::font_body(), 0);
        lv_obj_set_flex_grow(st->lbl_sync_result, 1);
    }

    // Grey out NTP + timezone fields when WiFi is disabled.
    {
        auto* sys = aqua::ui::ui_context().sys_cfg;
        if (sys && !sys->wifi_enabled) {
            disable_widget(st->roller_utc);
            disable_widget(st->ta_ntp1);
            disable_widget(st->ta_ntp2);
            lv_obj_t* note = lv_label_create(scroll);
            lv_label_set_text(note, i18n::tr(i18n::LangKey::NET_WIFI_DISABLED_NOTE));
            lv_obj_set_width(note, LV_PCT(100));
            lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(note, theme::font_caption(), 0);
            lv_obj_set_style_text_color(note, theme::color_text_disabled(), 0);
        }
    }

    // ------- Location section -------
    make_section_label(scroll, i18n::tr(i18n::LangKey::TZ_LOC_TITLE));

    make_field_label(scroll, i18n::tr(i18n::LangKey::TZ_LAT));
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.4f", (double)st->cfg.latitude);
        st->ta_lat = make_textarea(scroll, "0.0000", buf);
    }
    bind_textarea(st->ta_lat, st);

    make_field_label(scroll, i18n::tr(i18n::LangKey::TZ_LON));
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.4f", (double)st->cfg.longitude);
        st->ta_lon = make_textarea(scroll, "0.0000", buf);
    }
    bind_textarea(st->ta_lon, st);

    // ------- Keyboard -------
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
    lv_obj_add_event_cb(st->keyboard, on_kb_ready, LV_EVENT_READY,  st);
    lv_obj_add_event_cb(st->keyboard, on_kb_ready, LV_EVENT_CANCEL, st);

    return root;
}

}  // namespace aqua::ui::time_location_screen
