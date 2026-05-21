// AquaControl — Sensors settings screen implementation.
//
// Controls:
//   * Water sensor address     lv_dropdown  Auto / 0x44 / 0x45
//   * Ambient sensor address   lv_dropdown  Auto / 0x44 / 0x45
//   * Water calibration        lv_roller    −5.0 … +5.0 °C in 0.1 steps
//   * Ambient calibration      lv_roller    −5.0 … +5.0 °C in 0.1 steps
//
// Address changes are stored in SystemConfig and take effect after reboot.
// Calibration offsets are applied immediately via sensor_sampler::apply_calibration()
// and also persisted to NVS.
#include "sensors_settings_screen.h"

#include <cstdio>
#include <cstring>

#include "ac_logger.h"
#include "chrome.h"
#include "config_storage.h"
#include "i18n.h"
#include "screen_manager.h"
#include "sensor_sampler.h"
#include "system_config.h"
#include "theme.h"
#include "ui_context.h"

namespace aqua::ui::sensors_settings_screen {

namespace {

using aqua::ui::i18n::LangKey;
using aqua::ui::i18n::tr;
using aqua::sensors::HistorySample;
using aqua::sensors::kHistorySlots;

constexpr const char* TAG = "SensorsCfg";

// Calibration roller: −5.0 … +5.0 in 0.1 steps (101 entries).
// Index 50 == 0.0.  offset_c = (index - 50) * 0.1f
static constexpr int   kCalCenter  = 50;
static constexpr float kCalStep    = 0.1f;
static constexpr int   kCalEntries = 101;

// Address dropdown option string.
static constexpr const char* kAddrOptions = "Auto\n0x44\n0x45";

// addr byte → dropdown index (0=Auto, 1=0x44, 2=0x45)
static uint16_t addr_to_index(uint8_t addr) {
    if (addr == 0x44) return 1;
    if (addr == 0x45) return 2;
    return 0;
}

// dropdown index → addr byte
static uint8_t index_to_addr(uint16_t idx) {
    if (idx == 1) return 0x44;
    if (idx == 2) return 0x45;
    return 0;
}

// Build a calibration options string (static buffer reused across calls).
static const char* cal_options_string() {
    // 101 entries × "−5.0\n" max 7 chars + null = ~710 bytes
    static char buf[750];
    if (buf[0] != '\0') return buf;  // already built
    char* p = buf;
    for (int i = 0; i < kCalEntries; ++i) {
        float val = (i - kCalCenter) * kCalStep;
        int written = snprintf(p, (size_t)(buf + sizeof(buf) - p),
                               "%+.1f", val);
        if (written <= 0) break;
        p += written;
        if (i < kCalEntries - 1) *p++ = '\n';
    }
    *p = '\0';
    return buf;
}

static int cal_offset_to_index(float offset_c) {
    int idx = (int)((offset_c / kCalStep) + 0.5f) + kCalCenter;
    if (idx < 0) idx = 0;
    if (idx >= kCalEntries) idx = kCalEntries - 1;
    return idx;
}

static float cal_index_to_offset(uint16_t idx) {
    return (static_cast<float>(idx) - kCalCenter) * kCalStep;
}

// ---------------------------------------------------------------------------
// Per-screen heap state
// ---------------------------------------------------------------------------

struct State {
    lv_obj_t* sw_water_enabled   = nullptr;
    lv_obj_t* sw_ambient_enabled = nullptr;
    lv_obj_t* dd_water_addr   = nullptr;
    lv_obj_t* dd_ambient_addr = nullptr;
    lv_obj_t* roller_water_cal   = nullptr;
    lv_obj_t* roller_ambient_cal = nullptr;
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

static lv_obj_t* make_row(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t* make_body_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
    return lbl;
}

static lv_obj_t* make_note_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_small(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
    return lbl;
}

static lv_obj_t* make_addr_dropdown(lv_obj_t* parent, uint8_t current_addr) {
    lv_obj_t* dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, kAddrOptions);
    lv_dropdown_set_selected(dd, addr_to_index(current_addr));
    lv_obj_set_width(dd, 120);
    lv_obj_set_style_text_font(dd, theme::font_body(), 0);
    lv_obj_set_style_text_color(dd, theme::color_text_primary(), 0);
    lv_obj_set_style_bg_color(dd, theme::color_surface_alt(), 0);
    lv_obj_set_style_border_color(dd, theme::color_accent(), 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(dd), theme::font_body(), 0);
    lv_obj_set_style_text_color(lv_dropdown_get_list(dd), theme::color_text_primary(), 0);
    lv_obj_set_style_bg_color(lv_dropdown_get_list(dd), theme::color_surface(), 0);
    return dd;
}

static lv_obj_t* make_cal_roller(lv_obj_t* parent, float init_offset) {
    lv_obj_t* roller = lv_roller_create(parent);
    lv_roller_set_options(roller, cal_options_string(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(roller, (uint16_t)cal_offset_to_index(init_offset),
                           LV_ANIM_OFF);
    lv_roller_set_visible_row_count(roller, 3);
    lv_obj_set_width(roller, 120);
    lv_obj_set_style_text_font(roller, theme::font_body(), 0);
    lv_obj_set_style_bg_color(roller, theme::color_surface_alt(), 0);
    lv_obj_set_style_bg_color(roller, theme::color_surface(), LV_PART_SELECTED);
    lv_obj_set_style_border_color(roller, theme::color_accent(),
                                  LV_PART_SELECTED);
    return roller;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

static void on_save(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st) return;

    auto* sys = aqua::ui::ui_context().sys_cfg;
    if (!sys) {
        AC_LOGE(TAG, "sys_cfg is null");
        return;
    }

    sys->water_sensor_enabled   = lv_obj_has_state(st->sw_water_enabled,   LV_STATE_CHECKED);
    sys->ambient_sensor_enabled = lv_obj_has_state(st->sw_ambient_enabled, LV_STATE_CHECKED);
    sys->water_sensor_addr   = index_to_addr(
        lv_dropdown_get_selected(st->dd_water_addr));
    sys->ambient_sensor_addr = index_to_addr(
        lv_dropdown_get_selected(st->dd_ambient_addr));
    sys->water_cal_offset_c  = cal_index_to_offset(
        lv_roller_get_selected(st->roller_water_cal));
    sys->ambient_cal_offset_c = cal_index_to_offset(
        lv_roller_get_selected(st->roller_ambient_cal));

    // Apply calibration offsets to the running sampler immediately.
    aqua::sensors::apply_calibration(
        sys->water_cal_offset_c, sys->ambient_cal_offset_c);
    // Apply enable flags immediately.
    aqua::sensors::set_enabled(aqua::sensors::Role::WATER,   sys->water_sensor_enabled);
    aqua::sensors::set_enabled(aqua::sensors::Role::AMBIENT, sys->ambient_sensor_enabled);

    aqua::storage::save_system_config(*sys);

    AC_LOGI(TAG, "saved: w_addr=0x%02X a_addr=0x%02X w_cal=%.1f a_cal=%.1f",
            sys->water_sensor_addr, sys->ambient_sensor_addr,
            sys->water_cal_offset_c, sys->ambient_cal_offset_c);

    screen_manager::pop(screen_manager::Transition::SLIDE_RIGHT);
}

static void on_screen_delete(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    delete st;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public build()
// ---------------------------------------------------------------------------

lv_obj_t* build() {
    auto* sys = aqua::ui::ui_context().sys_cfg;
    static const aqua::storage::SystemConfig s_defaults;
    const auto& cfg = sys ? *sys : s_defaults;

    auto* st = new State();

    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, on_screen_delete, LV_EVENT_DELETE, st);

    chrome::Header hdr =
        chrome::build(root, tr(LangKey::NAV_SENSORS), chrome::pop_on_back, tr(LangKey::BTN_SAVE), on_save);
    if (hdr.btn_action) {
        lv_obj_remove_event_cb(hdr.btn_action, on_save);
        lv_obj_add_event_cb(hdr.btn_action, on_save, LV_EVENT_CLICKED, st);
    }

    // Scrollable content area.
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

    // ── I2C Addresses ───────────────────────────────────────────────────

    make_section_label(scroll, tr(LangKey::SENS_I2C_SECTION));

    {
        lv_obj_t* row = make_row(scroll);
        make_body_label(row, tr(LangKey::SENS_WATER_SENSOR));
        st->sw_water_enabled = lv_switch_create(row);
        lv_obj_set_size(st->sw_water_enabled, 60, 30);
        lv_obj_set_ext_click_area(st->sw_water_enabled, 13);
        if (cfg.water_sensor_enabled) lv_obj_add_state(st->sw_water_enabled, LV_STATE_CHECKED);
        make_body_label(row, tr(LangKey::SENS_SENSOR_ENABLED));
        st->dd_water_addr = make_addr_dropdown(row, cfg.water_sensor_addr);
    }
    {
        lv_obj_t* row = make_row(scroll);
        make_body_label(row, tr(LangKey::SENS_AMBIENT_SENSOR));
        st->sw_ambient_enabled = lv_switch_create(row);
        lv_obj_set_size(st->sw_ambient_enabled, 60, 30);
        lv_obj_set_ext_click_area(st->sw_ambient_enabled, 13);
        if (cfg.ambient_sensor_enabled) lv_obj_add_state(st->sw_ambient_enabled, LV_STATE_CHECKED);
        make_body_label(row, tr(LangKey::SENS_SENSOR_ENABLED));
        st->dd_ambient_addr = make_addr_dropdown(row, cfg.ambient_sensor_addr);
    }

    make_note_label(scroll, tr(LangKey::SENS_ADDR_NOTE));

    // ── Calibration ─────────────────────────────────────────────────────

    make_section_label(scroll, tr(LangKey::SENS_CAL_SECTION));
    make_note_label(scroll, tr(LangKey::SENS_CAL_NOTE));

    {
        lv_obj_t* row = make_row(scroll);
        make_body_label(row, tr(LangKey::SENS_WATER_CAL));
        st->roller_water_cal = make_cal_roller(row, cfg.water_cal_offset_c);
    }
    {
        lv_obj_t* row = make_row(scroll);
        make_body_label(row, tr(LangKey::SENS_AMBIENT_CAL));
        st->roller_ambient_cal = make_cal_roller(row, cfg.ambient_cal_offset_c);
    }

    // ── Water Temp History (12h) ─────────────────────────────────────────

    make_section_label(scroll, tr(LangKey::SENS_HISTORY_TITLE));

    {
        HistorySample hist[kHistorySlots]{};
        int n = aqua::sensors::get_water_history(hist);

        // Card container
        lv_obj_t* card = lv_obj_create(scroll);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, theme::color_surface(), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, theme::RADIUS_MD, 0);
        lv_obj_set_style_pad_all(card, theme::PAD_SM, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        if (n < 2) {
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, tr(LangKey::SENS_HISTORY_NO_DATA));
            lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
        } else {
            // Find min/max for axis range.
            float tmin = hist[0].temp_c, tmax = hist[0].temp_c;
            for (int i = 1; i < n; ++i) {
                if (hist[i].temp_c < tmin) tmin = hist[i].temp_c;
                if (hist[i].temp_c > tmax) tmax = hist[i].temp_c;
            }
            // Expand range by 1°C each side, round outward.
            int ymin = (int)(tmin - 1.0f);
            int ymax = (int)(tmax + 2.0f);
            if (ymax == ymin) { ymin -= 1; ymax += 1; }

            lv_obj_t* chart = lv_chart_create(card);
            lv_obj_set_width(chart, LV_PCT(100));
            lv_obj_set_height(chart, 140);
            lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
            lv_chart_set_point_count(chart, (uint32_t)n);
            lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, ymin * 10, ymax * 10);
            lv_obj_set_style_bg_color(chart, theme::color_background(), 0);
            lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(chart, 1, 0);
            lv_obj_set_style_border_color(chart, theme::color_outline(), 0);
            lv_chart_set_div_line_count(chart, 4, 0);
            lv_obj_set_style_line_color(chart, theme::color_outline(), LV_PART_ITEMS);
            lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);  // hide dots

            lv_chart_series_t* ser = lv_chart_add_series(chart, theme::color_accent(), LV_CHART_AXIS_PRIMARY_Y);
            for (int i = 0; i < n; ++i) {
                // Store temperature × 10 to preserve one decimal using integer axis.
                lv_chart_set_next_value(chart, ser, (int32_t)(hist[i].temp_c * 10.0f));
            }
        }
    }

    return root;
}

}  // namespace aqua::ui::sensors_settings_screen
