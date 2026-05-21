// AquaControl - Dashboard ("watch" face).
//
// The dashboard is glanceable: an always-on summary screen that the user
// only briefly looks at. It does NOT list devices anymore - tap the
// "Devices" pill to open device_list_screen.
//
// Layout (800 x 480):
//
//   |--------------------------------------------------------------|
//   | AquaControl     Mon 19 May 2026         WiFi MQTT (!) (gear) |  56 px header
//   |--------------------------------------------------------------|
//   |                                                              |
//   |                       1 4 : 3 2                              |  giant clock
//   |                       Monday                                 |
//   |                                                              |
//   |--------------------------------------------------------------|
//   |  WATER          AMBIENT        SUN                           |
//   |  24.5 C         22.1 C  58%    up 06:12  down 21:08          |
//   |--------------------------------------------------------------|
//   |  [ Devices  3 / 5 ON ]      [ Triggers  2 active ]           |  status pills
//   |--------------------------------------------------------------|
//
// A 1 Hz LVGL timer keeps the labels fresh.
#include "dashboard.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "ac_logger.h"
#include "device_manager.h"
#include "device_types.h"
#include "device_list_screen.h"
#include "esp_lvgl_port.h"
#include "faults.h"
#include "i18n.h"
#include "menu_screen.h"
#include "mqtt_client_aqua.h"
#include "screen_manager.h"
#include "sensor_sampler.h"
#include "solar_calc.h"
#include "system_config.h"
#include "theme.h"
#include "time_manager.h"
#include "trigger_manager.h"
#include "triggers_screen.h"
#include "ui_context.h"
#include "wifi_manager.h"

namespace aqua::ui::dashboard {

namespace {

constexpr const char* TAG = "Dashboard";

constexpr int16_t kHeaderH = 56;

struct State {
    lv_obj_t* root           = nullptr;

    // Header
    lv_obj_t* lbl_date       = nullptr;
    lv_obj_t* lbl_net_wifi   = nullptr;
    lv_obj_t* lbl_net_mqtt   = nullptr;
    lv_obj_t* lbl_fault_icon = nullptr;

    // Warning banner (Slice 7)
    lv_obj_t* warn_banner    = nullptr;
    lv_obj_t* warn_label     = nullptr;
    bool      warn_visible   = false;

    // Clock area (three labels in a flex row: hh | ":" blinks | mm)
    lv_obj_t* lbl_clock      = nullptr;   // hours "HH"
    lv_obj_t* lbl_clock_sep  = nullptr;   // colon ":" — blinks every second
    lv_obj_t* lbl_mm         = nullptr;   // minutes "MM"
    lv_obj_t* lbl_weekday    = nullptr;

    // Sensor row
    lv_obj_t* lbl_water_val  = nullptr;
    lv_obj_t* lbl_water_age  = nullptr;
    lv_obj_t* lbl_amb_val    = nullptr;
    lv_obj_t* lbl_amb_age    = nullptr;
    lv_obj_t* lbl_sun_val    = nullptr;
    // Sensor progress bars (show value position in range).
    lv_obj_t* bar_water      = nullptr;   // 15..35 °C → 0..100
    lv_obj_t* bar_amb        = nullptr;   // 10..30 °C → 0..100
    lv_obj_t* bar_sun        = nullptr;   // sunrise..sunset → 0..100

    // Status pills
    lv_obj_t* lbl_dev_pill   = nullptr;   // static name label
    lv_obj_t* lbl_dev_count  = nullptr;   // dynamic "3/5" count
    lv_obj_t* dot_dev        = nullptr;   // colored health indicator
    lv_obj_t* lbl_trg_pill   = nullptr;   // static name label
    lv_obj_t* lbl_trg_count  = nullptr;   // dynamic "2/4" count
    lv_obj_t* dot_trg        = nullptr;   // colored health indicator

    // Refresh timer
    lv_timer_t* refresh_timer = nullptr;

    // Solar cache for "today"
    int    solar_yday        = -1;
    int    solar_sunrise_min = -1;
    int    solar_sunset_min  = -1;
    bool   solar_valid       = false;
};

// M3: live dashboard state pointer — set in build(), cleared in on_screen_delete().
// Accessed exclusively from Core 1 (LVGL task), so no extra locking needed.
static State* g_dash = nullptr;

static State* state_of(lv_obj_t* root) {
    return static_cast<State*>(lv_obj_get_user_data(root));
}

static void label_set_if_changed(lv_obj_t* l, const char* s) {
    const char* cur = lv_label_get_text(l);
    if (cur && std::strcmp(cur, s) == 0) return;
    lv_label_set_text(l, s);
}

static void label_set_color_if_changed(lv_obj_t* l, lv_color_t c) {
    lv_color_t cur = lv_obj_get_style_text_color(l, LV_PART_MAIN);
    if (cur.red == c.red && cur.green == c.green && cur.blue == c.blue) {
        return;
    }
    lv_obj_set_style_text_color(l, c, 0);
}

static lv_obj_t* make_label(lv_obj_t* parent, const char* text,
                            const lv_font_t* font, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

// --- builders ---------------------------------------------------------------

static void build_header(State* st, lv_obj_t* parent) {
    lv_obj_t* hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), kHeaderH);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, theme::color_outline(), 0);
    lv_obj_set_style_border_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, theme::PAD_MD, 0);
    lv_obj_set_style_pad_ver(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* brand = make_label(hdr, "AquaControl",
                                 theme::font_title(),
                                 theme::color_accent());
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 0, 0);

    st->lbl_date = make_label(hdr, "",
                              theme::font_body(),
                              theme::color_text_secondary());
    lv_obj_align(st->lbl_date, LV_ALIGN_CENTER, 0, 0);

    // Settings (gear) button at the far right.
    lv_obj_t* btn_settings = lv_btn_create(hdr);
    lv_obj_set_size(btn_settings, theme::TOUCH_MIN, theme::TOUCH_MIN);
    lv_obj_set_style_radius(btn_settings, theme::RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(btn_settings, theme::color_surface_alt(), 0);
    lv_obj_set_style_bg_opa(btn_settings, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn_settings, theme::color_outline(), 0);
    lv_obj_set_style_border_width(btn_settings, 1, 0);
    lv_obj_set_style_shadow_width(btn_settings, 0, 0);
    lv_obj_set_style_bg_color(btn_settings, theme::color_accent(),
                              LV_STATE_PRESSED);
    lv_obj_align(btn_settings, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t* lbl_cog = lv_label_create(btn_settings);
    lv_label_set_text(lbl_cog, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(lbl_cog, theme::font_title(), 0);
    lv_obj_set_style_text_color(lbl_cog, theme::color_text_primary(), 0);
    lv_obj_center(lbl_cog);
    lv_obj_add_event_cb(btn_settings, [](lv_event_t*) {
        screen_manager::push(menu_screen::build());
    }, LV_EVENT_CLICKED, nullptr);

    // Status icon strip: wifi, mqtt, fault. Each is a compact label
    // (no button) using FA glyphs.
    st->lbl_fault_icon = make_label(hdr, "",
                                    theme::font_title(),
                                    theme::color_warning());
    lv_obj_align_to(st->lbl_fault_icon, btn_settings,
                    LV_ALIGN_OUT_LEFT_MID, -theme::PAD_MD, 0);

    st->lbl_net_mqtt = make_label(hdr, LV_SYMBOL_UPLOAD,
                                  theme::font_title(),
                                  theme::color_text_disabled());
    lv_obj_align_to(st->lbl_net_mqtt, st->lbl_fault_icon,
                    LV_ALIGN_OUT_LEFT_MID, -theme::PAD_SM, 0);

    st->lbl_net_wifi = make_label(hdr, LV_SYMBOL_WIFI,
                                  theme::font_title(),
                                  theme::color_text_disabled());
    lv_obj_align_to(st->lbl_net_wifi, st->lbl_net_mqtt,
                    LV_ALIGN_OUT_LEFT_MID, -theme::PAD_SM, 0);
}

static void build_clock(State* st, lv_obj_t* parent) {
    // Flex row: [HH][:][MM] — colon blinks to show device is alive.
    // Fixed width (240 px >= max "00:00" at 48 px Montserrat + letter-space);
    // height is content-driven so the row hugs the glyphs exactly.
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, 240, LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, kHeaderH + 30);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    st->lbl_clock = make_label(row, "00", theme::font_clock(),
                               theme::color_text_primary());
    lv_obj_set_style_text_letter_space(st->lbl_clock, 4, 0);

    st->lbl_clock_sep = make_label(row, ":", theme::font_clock(),
                                   theme::color_text_primary());
    lv_obj_set_style_text_letter_space(st->lbl_clock_sep, 4, 0);

    st->lbl_mm = make_label(row, "00", theme::font_clock(),
                            theme::color_text_primary());
    lv_obj_set_style_text_letter_space(st->lbl_mm, 4, 0);

    st->lbl_weekday = make_label(parent, "",
                                 theme::font_title(),
                                 theme::color_text_secondary());
    lv_obj_align(st->lbl_weekday, LV_ALIGN_TOP_MID, 0, kHeaderH + 30 + 64);
}

// One sensor card: caption + big value + sub-line + bottom range bar.
struct SensorCard {
    lv_obj_t* root      = nullptr;
    lv_obj_t* lbl_cap   = nullptr;
    lv_obj_t* lbl_value = nullptr;
    lv_obj_t* lbl_sub   = nullptr;
    lv_obj_t* bar       = nullptr;   // bottom progress bar (range indicator)
};

static SensorCard make_sensor_card(lv_obj_t* parent,
                                   const char* icon,
                                   const char* caption,
                                   lv_color_t icon_color,
                                   int16_t x, int16_t y, int16_t w) {
    SensorCard c{};
    c.root = lv_obj_create(parent);
    lv_obj_set_size(c.root, w, 130);
    lv_obj_set_pos(c.root, x, y);
    lv_obj_set_style_bg_color(c.root, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(c.root, LV_OPA_COVER, 0);
    // Colored border matching the card's data type — subtle tint, not harsh.
    lv_obj_set_style_border_color(c.root, icon_color, 0);
    lv_obj_set_style_border_width(c.root, 1, 0);
    lv_obj_set_style_border_opa(c.root, LV_OPA_40, 0);
    lv_obj_set_style_radius(c.root, theme::RADIUS_LG, 0);
    lv_obj_set_style_pad_hor(c.root, theme::PAD_MD, 0);
    lv_obj_set_style_pad_top(c.root, theme::PAD_SM, 0);
    lv_obj_set_style_pad_bottom(c.root, theme::PAD_MD, 0);
    lv_obj_clear_flag(c.root, LV_OBJ_FLAG_SCROLLABLE);
    // Elevation shadow for a layered / floating look.
    lv_obj_set_style_shadow_width(c.root, 20, 0);
    lv_obj_set_style_shadow_spread(c.root, 0, 0);
    lv_obj_set_style_shadow_ofs_y(c.root, 4, 0);
    lv_obj_set_style_shadow_color(c.root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(c.root, LV_OPA_40, 0);

    // Icon + caption row at top.
    lv_obj_t* lbl_icon = make_label(c.root, icon,
                                    theme::font_title(),
                                    icon_color);
    lv_obj_align(lbl_icon, LV_ALIGN_TOP_LEFT, 0, 0);

    c.lbl_cap = make_label(c.root, caption,
                           theme::font_caption(),
                           theme::color_text_secondary());
    lv_obj_align_to(c.lbl_cap, lbl_icon, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    // Big value — offset up from the bottom to leave room for the bar.
    c.lbl_value = make_label(c.root, "--",
                             theme::font_value_xl(),
                             theme::color_text_primary());
    lv_obj_align(c.lbl_value, LV_ALIGN_BOTTOM_LEFT, 0, -10);

    // Optional secondary info (humidity, age, etc.).
    c.lbl_sub = make_label(c.root, "",
                           theme::font_caption(),
                           theme::color_text_secondary());
    lv_obj_align(c.lbl_sub, LV_ALIGN_BOTTOM_RIGHT, 0, -10);

    // Bottom range/progress bar — shows the live reading within its range.
    c.bar = lv_bar_create(c.root);
    lv_obj_set_size(c.bar, lv_pct(100), 4);
    lv_obj_align(c.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(c.bar, theme::color_surface_alt(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(c.bar, theme::RADIUS_PILL, LV_PART_MAIN);
    lv_obj_set_style_border_width(c.bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c.bar, icon_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(c.bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(c.bar, theme::RADIUS_PILL, LV_PART_INDICATOR);
    lv_bar_set_range(c.bar, 0, 100);
    lv_bar_set_value(c.bar, 0, LV_ANIM_OFF);
    return c;
}

static void build_sensor_row(State* st, lv_obj_t* parent, int16_t y_top) {
    // 800 px wide, 16 px outer padding, 12 px gutter -> 3 cards = 248 px each.
    const int16_t pad = theme::PAD_MD;
    const int16_t gap = 12;
    const int16_t w   = (800 - 2 * pad - 2 * gap) / 3;

    auto water = make_sensor_card(parent,
        LV_SYMBOL_TINT, i18n::tr(i18n::LangKey::SENSE_WATER),
        theme::color_accent(),
        pad, y_top, w);
    st->lbl_water_val = water.lbl_value;
    st->lbl_water_age = water.lbl_sub;
    st->bar_water     = water.bar;

    auto amb = make_sensor_card(parent,
        LV_SYMBOL_HOME, i18n::tr(i18n::LangKey::SENSE_AMBIENT),
        theme::color_accent_2(),
        pad + w + gap, y_top, w);
    st->lbl_amb_val = amb.lbl_value;
    st->lbl_amb_age = amb.lbl_sub;
    st->bar_amb     = amb.bar;

    auto sun = make_sensor_card(parent,
        LV_SYMBOL_UP, i18n::tr(i18n::LangKey::TRG_SOLAR),
        theme::color_warning(),
        pad + 2 * (w + gap), y_top, w);
    st->lbl_sun_val = sun.lbl_value;
    st->bar_sun     = sun.bar;
    // Sun card uses smaller value font so both times fit.
    lv_obj_set_style_text_font(st->lbl_sun_val, theme::font_title(), 0);
    lv_obj_add_flag(sun.lbl_sub, LV_OBJ_FLAG_HIDDEN);  // unused sub-label
}

// A flat pill that doubles as a button.
static lv_obj_t* make_status_pill(lv_obj_t* parent,
                                  int16_t x, int16_t y, int16_t w,
                                  lv_event_cb_t on_click) {
    lv_obj_t* p = lv_btn_create(parent);
    lv_obj_set_size(p, w, 72);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_style_radius(p, theme::RADIUS_LG, 0);
    lv_obj_set_style_bg_color(p, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, theme::color_outline(), 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_opa(p, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(p, 16, 0);
    lv_obj_set_style_shadow_spread(p, 0, 0);
    lv_obj_set_style_shadow_ofs_y(p, 3, 0);
    lv_obj_set_style_shadow_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(p, LV_OPA_30, 0);
    lv_obj_set_style_bg_color(p, theme::color_surface_alt(),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_color(p, theme::color_accent(),
                                  LV_STATE_PRESSED);
    if (on_click) {
        lv_obj_add_event_cb(p, on_click, LV_EVENT_CLICKED, nullptr);
    }
    return p;
}

static void build_status_pills(State* st, lv_obj_t* parent, int16_t y_top) {
    const int16_t pad = theme::PAD_MD;
    const int16_t gap = 12;
    const int16_t w   = (800 - 2 * pad - gap) / 2;

    // Helper: small glowing circle as a health indicator.
    auto make_dot = [](lv_obj_t* pill) -> lv_obj_t* {
        lv_obj_t* dot = lv_obj_create(pill);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, theme::RADIUS_PILL, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, theme::color_text_disabled(), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(dot, 8, 0);
        lv_obj_set_style_shadow_spread(dot, 2, 0);
        lv_obj_set_style_shadow_ofs_y(dot, 0, 0);
        lv_obj_set_style_shadow_color(dot, theme::color_text_disabled(), 0);
        lv_obj_set_style_shadow_opa(dot, LV_OPA_50, 0);
        lv_obj_clear_flag(dot, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        return dot;
    };

    // Devices pill -> opens device list.
    lv_obj_t* p1 = make_status_pill(parent, pad, y_top, w,
        [](lv_event_t*) {
            screen_manager::push(device_list_screen::build());
        });
    lv_obj_t* icon1 = make_label(p1, LV_SYMBOL_LIST,
                                 theme::font_title(),
                                 theme::color_accent());
    lv_obj_align(icon1, LV_ALIGN_LEFT_MID, theme::PAD_MD, 0);
    st->lbl_dev_pill = make_label(p1, i18n::tr(i18n::LangKey::NAV_DEVICES),
                                  theme::font_body(),
                                  theme::color_text_primary());
    lv_obj_align(st->lbl_dev_pill, LV_ALIGN_LEFT_MID,
                 theme::PAD_MD + 36, 0);
    // Right side: fixed-width flex container avoids lazy-size issues with align_to.
    lv_obj_t* rhs1 = lv_obj_create(p1);
    lv_obj_set_size(rhs1, 110, 48);
    lv_obj_align(rhs1, LV_ALIGN_RIGHT_MID, -theme::PAD_SM, 0);
    lv_obj_set_style_bg_opa(rhs1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rhs1, 0, 0);
    lv_obj_set_style_pad_all(rhs1, 0, 0);
    lv_obj_set_style_pad_column(rhs1, theme::PAD_SM, 0);
    lv_obj_clear_flag(rhs1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(rhs1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rhs1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rhs1, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    st->lbl_dev_count = make_label(rhs1, "--", theme::font_title(),
                                   theme::color_text_primary());
    st->dot_dev = make_dot(rhs1);
    make_label(rhs1, LV_SYMBOL_RIGHT, theme::font_body(),
               theme::color_text_secondary());

    // Triggers pill -> navigates directly to triggers_screen.
    lv_obj_t* p2 = make_status_pill(parent, pad + w + gap, y_top, w,
        [](lv_event_t*) {
            screen_manager::push(triggers_screen::build());
        });
    lv_obj_t* icon2 = make_label(p2, LV_SYMBOL_LOOP,
                                 theme::font_title(),
                                 theme::color_accent_2());
    lv_obj_align(icon2, LV_ALIGN_LEFT_MID, theme::PAD_MD, 0);
    st->lbl_trg_pill = make_label(p2, i18n::tr(i18n::LangKey::NAV_TRIGGERS),
                                  theme::font_body(),
                                  theme::color_text_primary());
    lv_obj_align(st->lbl_trg_pill, LV_ALIGN_LEFT_MID,
                 theme::PAD_MD + 36, 0);
    // Right side: flex container.
    lv_obj_t* rhs2 = lv_obj_create(p2);
    lv_obj_set_size(rhs2, 110, 48);
    lv_obj_align(rhs2, LV_ALIGN_RIGHT_MID, -theme::PAD_SM, 0);
    lv_obj_set_style_bg_opa(rhs2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rhs2, 0, 0);
    lv_obj_set_style_pad_all(rhs2, 0, 0);
    lv_obj_set_style_pad_column(rhs2, theme::PAD_SM, 0);
    lv_obj_clear_flag(rhs2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(rhs2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rhs2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rhs2, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    st->lbl_trg_count = make_label(rhs2, "--", theme::font_title(),
                                   theme::color_text_primary());
    st->dot_trg = make_dot(rhs2);
    make_label(rhs2, LV_SYMBOL_RIGHT, theme::font_body(),
               theme::color_text_secondary());
}

// --- refresh ---------------------------------------------------------------

static void refresh_clock(State* st) {
    struct tm lt = aqua::time_mgr::TimeManager::now_local();
    char hh[4];   // hours part
    char mm[4];   // minutes part
    char wd[16];
    char dt[40];
    if (aqua::time_mgr::TimeManager::is_synced()) {
        snprintf(hh, sizeof(hh), "%02d", lt.tm_hour);
        snprintf(mm, sizeof(mm), "%02d", lt.tm_min);
        // Weekday and month via i18n for locale-aware names.
        const char* wday_full  = i18n::tr(static_cast<i18n::LangKey>(
            static_cast<uint16_t>(i18n::LangKey::DASH_WDAY_SUN) + (lt.tm_wday % 7)));
        const char* wday_short = i18n::tr(static_cast<i18n::LangKey>(
            static_cast<uint16_t>(i18n::LangKey::DASH_WDAY_SHORT_SUN) + (lt.tm_wday % 7)));
        const char* month_short = i18n::tr(static_cast<i18n::LangKey>(
            static_cast<uint16_t>(i18n::LangKey::DASH_MON_JAN) + (lt.tm_mon % 12)));
        snprintf(wd, sizeof(wd), "%s", wday_full);
        snprintf(dt, sizeof(dt), "%s %02d %s %04d",
                 wday_short, lt.tm_mday,
                 month_short, lt.tm_year + 1900);
    } else {
        snprintf(hh, sizeof(hh), "--");
        snprintf(mm, sizeof(mm), "--");
        snprintf(wd, sizeof(wd), "%s", i18n::tr(i18n::LangKey::DASH_TIME_NOT_SET));
        snprintf(dt, sizeof(dt), "%s", i18n::tr(i18n::LangKey::DASH_SYNCING));
    }
    label_set_if_changed(st->lbl_clock, hh);
    label_set_if_changed(st->lbl_mm, mm);
    // Colon blinks on every odd second — a subtle heartbeat.
    lv_obj_set_style_opa(st->lbl_clock_sep,
        (lt.tm_sec % 2 == 0) ? LV_OPA_COVER : LV_OPA_0, 0);
    label_set_if_changed(st->lbl_weekday, wd);
    label_set_if_changed(st->lbl_date, dt);
}

static void refresh_sensors(State* st) {
    auto& cfg = *ui_context().sys_cfg;
    const bool use_f = (cfg.temp_unit ==
                       aqua::storage::TempUnit::FAHRENHEIT);
    auto fmt_temp = [&](float c) -> float {
        return use_f ? (c * 9.0f / 5.0f + 32.0f) : c;
    };
    const char* unit = use_f ? "F" : "C";
    char buf[48];

    // Water
    auto wat = aqua::sensors::get(aqua::sensors::Role::WATER);
    if (wat.valid) {
        snprintf(buf, sizeof(buf), "%.1f\u00b0%s",
                 (double)fmt_temp(wat.temp_c), unit);
        label_set_if_changed(st->lbl_water_val, buf);
        // Color-code: blue <22, green 22-27, yellow 27-30, red >30 (Celsius)
        float tc = wat.temp_c;
        lv_color_t tc_col = (tc < 22.0f) ? theme::color_accent()
                          : (tc < 27.0f) ? theme::color_success()
                          : (tc < 30.0f) ? theme::color_warning()
                                         : theme::color_error();
        label_set_color_if_changed(st->lbl_water_val, tc_col);

        uint64_t age_s = aqua::sensors::age_ms(aqua::sensors::Role::WATER) / 1000;
        if (age_s < 120) {
            snprintf(buf, sizeof(buf), i18n::tr(i18n::LangKey::DASH_SEC_AGO), (unsigned)age_s);
        } else {
            snprintf(buf, sizeof(buf), i18n::tr(i18n::LangKey::DASH_MIN_AGO), (unsigned)(age_s / 60));
        }
        label_set_if_changed(st->lbl_water_age, buf);
        label_set_color_if_changed(st->lbl_water_age,
            age_s > 300 ? theme::color_warning()
                        : theme::color_text_secondary());
    } else {
        label_set_if_changed(st->lbl_water_val, "--");
        label_set_color_if_changed(st->lbl_water_val, theme::color_text_disabled());
        label_set_if_changed(st->lbl_water_age, i18n::tr(i18n::LangKey::DASH_NO_DATA));
    }
    if (st->bar_water) {
        int pct = wat.valid
            ? (int)((wat.temp_c - 15.0f) / 20.0f * 100.0f + 0.5f) : 0;
        pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        lv_bar_set_value(st->bar_water, pct, LV_ANIM_OFF);
    }

    // Ambient
    auto amb = aqua::sensors::get(aqua::sensors::Role::AMBIENT);
    if (amb.valid) {
        snprintf(buf, sizeof(buf), "%.1f\u00b0%s",
                 (double)fmt_temp(amb.temp_c), unit);
        label_set_if_changed(st->lbl_amb_val, buf);
        // Color ambient temp by comfort range: <18 blue, 18-26 green, >26 orange
        float ac = amb.temp_c;
        lv_color_t ac_col = (ac < 18.0f) ? theme::color_accent()
                          : (ac < 26.0f) ? theme::color_success()
                                         : theme::color_warning();
        label_set_color_if_changed(st->lbl_amb_val, ac_col);
        char sub[24];
        snprintf(sub, sizeof(sub), "%.0f%% RH",
                 (double)amb.humidity);
        label_set_if_changed(st->lbl_amb_age, sub);
    } else {
        label_set_if_changed(st->lbl_amb_val, "--");
        label_set_color_if_changed(st->lbl_amb_val, theme::color_text_disabled());
        label_set_if_changed(st->lbl_amb_age, i18n::tr(i18n::LangKey::DASH_NO_DATA));
    }
    if (st->bar_amb) {
        int pct = amb.valid
            ? (int)((amb.temp_c - 10.0f) / 20.0f * 100.0f + 0.5f) : 0;
        pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        lv_bar_set_value(st->bar_amb, pct, LV_ANIM_OFF);
    }

    // Sun
    struct tm now = aqua::time_mgr::TimeManager::now_local();
    if (now.tm_yday != st->solar_yday) {
        auto r = aqua::solar::compute(now.tm_year + 1900,
                                      now.tm_mon + 1,
                                      now.tm_mday,
                                      cfg.latitude, cfg.longitude,
                                      cfg.utc_offset_min);
        st->solar_yday        = now.tm_yday;
        st->solar_sunrise_min = r.sunrise_min;
        st->solar_sunset_min  = r.sunset_min;
        st->solar_valid       = r.valid;
    }
    if (st->solar_valid) {
        snprintf(buf, sizeof(buf),
                 LV_SYMBOL_UP " %02d:%02d\n" LV_SYMBOL_DOWN " %02d:%02d",
                 st->solar_sunrise_min / 60, st->solar_sunrise_min % 60,
                 st->solar_sunset_min  / 60, st->solar_sunset_min  % 60);
    } else if (cfg.latitude == 0.0f && cfg.longitude == 0.0f) {
        snprintf(buf, sizeof(buf), "%s", i18n::tr(i18n::LangKey::DASH_SET_LOCATION));
    } else {
        snprintf(buf, sizeof(buf), "%s", i18n::tr(i18n::LangKey::DASH_POLAR));
    }
    label_set_if_changed(st->lbl_sun_val, buf);
    if (st->bar_sun) {
        int pct = 0;
        if (st->solar_valid) {
            int now_min = now.tm_hour * 60 + now.tm_min;
            int dur_min = st->solar_sunset_min - st->solar_sunrise_min;
            if (dur_min > 0) {
                pct = (now_min - st->solar_sunrise_min) * 100 / dur_min;
                pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
            }
        }
        lv_bar_set_value(st->bar_sun, pct, LV_ANIM_OFF);
    }
}

static void refresh_status_pills(State* st) {
    const auto& ctx = ui_context();
    char cnt[24];

    // Devices.
    size_t total = 0, on = 0;
    if (ctx.devices) {
        const auto& list = ctx.devices->all();
        for (const auto& up : list) {
            if (!up) continue;
            ++total;
            if (up->current_active()) ++on;
        }
    }
    if (total == 0) {
        label_set_if_changed(st->lbl_dev_count, "--");
    } else {
        snprintf(cnt, sizeof(cnt), "%zu/%zu", on, total);
        label_set_if_changed(st->lbl_dev_count, cnt);
    }
    if (st->dot_dev) {
        lv_color_t col = (total == 0)  ? theme::color_text_disabled()
                       : (on == total) ? theme::color_success()
                       : (on > 0)      ? theme::color_warning()
                                       : theme::color_error();
        lv_obj_set_style_bg_color(st->dot_dev, col, 0);
        lv_obj_set_style_shadow_color(st->dot_dev, col, 0);
    }

    // Triggers.
    size_t trg_total = 0, trg_enabled = 0;
    if (ctx.triggers) {
        ctx.triggers->for_each([&](aqua::triggers::ITrigger& t) {
            ++trg_total;
            if (t.enabled) ++trg_enabled;
        });
    }
    if (trg_total == 0) {
        label_set_if_changed(st->lbl_trg_count, "--");
    } else {
        snprintf(cnt, sizeof(cnt), "%zu/%zu", trg_enabled, trg_total);
        label_set_if_changed(st->lbl_trg_count, cnt);
    }
    if (st->dot_trg) {
        lv_color_t col = (trg_total == 0)              ? theme::color_text_disabled()
                       : (trg_enabled == trg_total)    ? theme::color_success()
                       : (trg_enabled > 0)             ? theme::color_warning()
                                                       : theme::color_error();
        lv_obj_set_style_bg_color(st->dot_trg, col, 0);
        lv_obj_set_style_shadow_color(st->dot_trg, col, 0);
    }
}

static void refresh_status_icons(State* st) {
    // Slice 8: real WiFi / MQTT connection state.
    label_set_color_if_changed(st->lbl_net_wifi,
        aqua::wifi::is_connected() ? theme::color_success()
                                   : theme::color_text_disabled());
    label_set_color_if_changed(st->lbl_net_mqtt,
        aqua::mqtt::connected() ? theme::color_success()
                                : theme::color_text_disabled());

    // Fault indicator: hide unless faults are present.
    uint16_t n = aqua::faults::active_count();
    if (n > 0) {
        label_set_if_changed(st->lbl_fault_icon, LV_SYMBOL_WARNING);
        // Slice 7: populate and show warning banner.
        if (st->warn_banner) {
            auto faults = aqua::faults::active();
            // Build a compact summary string (first two fault labels).
            char buf[160];
            buf[0] = '\0';
            size_t shown = 0;
            for (auto& f : faults) {
                if (shown >= 2) { strncat(buf, " …", sizeof(buf) - strlen(buf) - 1); break; }
                if (shown > 0) strncat(buf, "  |  ", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, LV_SYMBOL_WARNING " ", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, f.label.c_str(), sizeof(buf) - strlen(buf) - 1);
                ++shown;
            }
            lv_label_set_text(st->warn_label, buf);
            if (!st->warn_visible) {
                lv_obj_set_height(st->warn_banner, 28);
                lv_obj_clear_flag(st->warn_banner, LV_OBJ_FLAG_HIDDEN);
                st->warn_visible = true;
            }
        }
    } else {
        label_set_if_changed(st->lbl_fault_icon, "");
        // Slice 7: collapse banner.
        if (st->warn_banner && st->warn_visible) {
            lv_obj_add_flag(st->warn_banner, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(st->warn_label, "");
            st->warn_visible = false;
        }
    }
}

static void refresh_cb(lv_timer_t* timer) {
    auto* st = static_cast<State*>(lv_timer_get_user_data(timer));
    if (!st || !st->root) return;
    refresh_clock(st);
    refresh_sensors(st);
    refresh_status_pills(st);
    refresh_status_icons(st);
}

static void on_screen_delete(lv_event_t* e) {
    lv_obj_t* root = static_cast<lv_obj_t*>(lv_event_get_target(e));
    State* st = state_of(root);
    if (st && st->refresh_timer) {
        lv_timer_del(st->refresh_timer);
        st->refresh_timer = nullptr;
    }
    if (g_dash == st) g_dash = nullptr;  // M3: stop cross-core refreshes
    delete st;
    lv_obj_set_user_data(root, nullptr);
}

}  // namespace

lv_obj_t* build() {
    if (!lvgl_port_lock(500)) {
        AC_LOGE(TAG, "build: LVGL lock timeout");
        return nullptr;
    }

    auto* st = new State();
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_user_data(root, st);
    lv_obj_add_event_cb(root, on_screen_delete, LV_EVENT_DELETE, nullptr);
    st->root = root;

    build_header(st, root);

    // Slice 7: warning banner — sits just below header, hidden when no faults.
    {
        lv_obj_t* ban = lv_obj_create(root);
        lv_obj_set_size(ban, 800, 28);
        lv_obj_set_pos(ban, 0, kHeaderH);
        lv_obj_set_style_bg_color(ban, theme::color_error(), 0);
        lv_obj_set_style_bg_opa(ban, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ban, 0, 0);
        lv_obj_set_style_pad_hor(ban, theme::PAD_SM, 0);
        lv_obj_set_style_pad_ver(ban, 4, 0);
        lv_obj_clear_flag(ban, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ban, LV_OBJ_FLAG_HIDDEN);    // hidden until faults active

        lv_obj_t* lbl = lv_label_create(ban);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, theme::font_caption(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(lbl, 780);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        st->warn_banner  = ban;
        st->warn_label   = lbl;
        st->warn_visible = false;
    }

    build_clock(st, root);

    // y-layout: header 0..56, clock 86..210, divider 218,
    // sensor row 228..358, status pills 374..446. Bottom 34 px breathing room.
    // Thin horizontal rule separating clock area from sensor cards.
    {
        lv_obj_t* sep = lv_obj_create(root);
        lv_obj_set_size(sep, 800 - 2 * theme::PAD_XL, 1);
        lv_obj_set_pos(sep, theme::PAD_XL, 218);
        lv_obj_set_style_bg_color(sep, theme::color_outline(), 0);
        lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sep, 0, 0);
        lv_obj_set_style_radius(sep, 0, 0);
        lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    }
    build_sensor_row(st, root, 228);
    build_status_pills(st, root, 374);

    // First paint
    refresh_clock(st);
    refresh_sensors(st);
    refresh_status_pills(st);
    refresh_status_icons(st);

    // M3: expose dashboard state for cross-core notify.
    g_dash = st;

    // 1 Hz refresh
    st->refresh_timer = lv_timer_create(refresh_cb, 1000, st);

    lvgl_port_unlock();
    AC_LOGI(TAG, "dashboard built (watch face)");
    return root;
}

void notify_device_changed() {
    // Called from Core 0 (scheduler task). Post a status-pill + icon refresh
    // to Core 1 (LVGL task) via lv_async_call, which is ISR/task safe.
    lv_async_call([](void*) {
        if (g_dash) {
            refresh_status_pills(g_dash);
            refresh_status_icons(g_dash);
        }
    }, nullptr);
}

}  // namespace aqua::ui::dashboard
