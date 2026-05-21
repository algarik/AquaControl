// AquaControl — Display & System settings screen implementation.
//
// Controls:
//   * Active brightness   lv_slider  10..100 %  → live backlight preview + sys_cfg
//   * Dim brightness      lv_slider   5..80  %  → sys_cfg
//   * Inactivity timeout  lv_dropdown  30s / 1min / 2min / 5min / 10min / Never
//   * Temperature unit    button-pair  °C  /  °F
//   * Run Setup Wizard    button → pushes wizard screen
//
// Save (chrome bar button) → storage::save_system_config() and applies
// the active brightness to the backlight immediately.
#include "display_system_screen.h"

#include "ac_logger.h"
#include "backlight.h"
#include "chrome.h"
#include "i18n.h"
#include "screen_manager.h"
#include "system_config.h"
#include "theme.h"
#include "ui_context.h"
#include "wizard.h"

namespace aqua::ui::display_system_screen {

namespace {

using aqua::ui::i18n::LangKey;
using aqua::ui::i18n::tr;

constexpr const char* TAG = "DisplaySys";

// Dropdown option list — order must match kTimeoutValues[].
static constexpr const char* kTimeoutOptions =
    "30 seconds\n1 minute\n2 minutes\n5 minutes\n10 minutes\nNever";

static constexpr uint16_t kTimeoutValues[] = { 30, 60, 120, 300, 600, 0 };
static constexpr uint8_t  kTimeoutCount    =
    sizeof(kTimeoutValues) / sizeof(kTimeoutValues[0]);

// ---------------------------------------------------------------------------
// Per-screen heap state
// ---------------------------------------------------------------------------

struct State {
    lv_obj_t* slider_active   = nullptr;  // 10..100 %
    lv_obj_t* lbl_active_val  = nullptr;  // "80 %"
    lv_obj_t* slider_dim      = nullptr;  // 5..80 %
    lv_obj_t* lbl_dim_val     = nullptr;  // "15 %"
    lv_obj_t* dd_timeout      = nullptr;  // dropdown
    lv_obj_t* btn_celsius     = nullptr;  // unit toggle
    lv_obj_t* btn_fahrenheit  = nullptr;
    bool      use_fahrenheit  = false;    // current selection
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

static lv_obj_t* make_value_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
    return lbl;
}

static lv_obj_t* make_slider(lv_obj_t* parent, int min_val, int max_val,
                              int init_val) {
    lv_obj_t* s = lv_slider_create(parent);
    lv_obj_set_width(s, LV_PCT(100));
    lv_slider_set_range(s, min_val, max_val);
    lv_slider_set_value(s, init_val, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(s, theme::color_surface_alt(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, theme::color_accent(),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, theme::color_accent(), LV_PART_KNOB);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_height(s, 8);
    lv_obj_set_style_pad_hor(s, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_ver(s, 6, LV_PART_KNOB);
    return s;
}

// Styled unit toggle button (°C or °F).
static lv_obj_t* make_unit_btn(lv_obj_t* parent, const char* label) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 90, 44);
    lv_obj_set_style_radius(btn, theme::RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_center(lbl);
    return btn;
}

static void apply_unit_btn_styles(State* st) {
    // Active button gets accent bg; inactive gets surface_alt.
    lv_obj_set_style_bg_color(st->btn_celsius,
        st->use_fahrenheit ? theme::color_surface_alt() : theme::color_accent(), 0);
    lv_obj_set_style_text_color(
        lv_obj_get_child(st->btn_celsius, 0),
        st->use_fahrenheit ? theme::color_text_secondary() : theme::color_text_primary(), 0);

    lv_obj_set_style_bg_color(st->btn_fahrenheit,
        st->use_fahrenheit ? theme::color_accent() : theme::color_surface_alt(), 0);
    lv_obj_set_style_text_color(
        lv_obj_get_child(st->btn_fahrenheit, 0),
        st->use_fahrenheit ? theme::color_text_primary() : theme::color_text_secondary(), 0);
}

// Map timeout value → dropdown index. Returns 5 (Never) on no match.
static uint8_t timeout_to_index(uint16_t timeout_s) {
    for (uint8_t i = 0; i < kTimeoutCount; ++i) {
        if (kTimeoutValues[i] == timeout_s) return i;
    }
    return 5;  // default to "Never" if value not in list
}

// ---------------------------------------------------------------------------
// Event callbacks
// ---------------------------------------------------------------------------

static void on_active_slider_changed(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st) return;
    int32_t val = lv_slider_get_value(st->slider_active);
    // Live backlight preview.
    aqua::display::backlight_set_percent((uint8_t)val);
    // Update value label.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d %%", (int)val);
    lv_label_set_text(st->lbl_active_val, buf);
}

static void on_dim_slider_changed(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st) return;
    int32_t val = lv_slider_get_value(st->slider_dim);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d %%", (int)val);
    lv_label_set_text(st->lbl_dim_val, buf);
}

static void on_celsius_clicked(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st || !st->use_fahrenheit) return;
    st->use_fahrenheit = false;
    apply_unit_btn_styles(st);
}

static void on_fahrenheit_clicked(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st || st->use_fahrenheit) return;
    st->use_fahrenheit = true;
    apply_unit_btn_styles(st);
}

static void on_wizard_clicked(lv_event_t* /*e*/) {
    if (lv_obj_t* wiz = aqua::ui::wizard::build()) {
        screen_manager::push(wiz, screen_manager::Transition::FADE);
    }
}

static void on_save(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st) return;

    auto* sys = aqua::ui::ui_context().sys_cfg;
    if (!sys) {
        AC_LOGE(TAG, "sys_cfg is null");
        return;
    }

    sys->brightness_active_pct =
        (uint8_t)lv_slider_get_value(st->slider_active);
    sys->brightness_dim_pct =
        (uint8_t)lv_slider_get_value(st->slider_dim);

    uint16_t sel = lv_dropdown_get_selected(st->dd_timeout);
    if (sel < kTimeoutCount) {
        sys->inactivity_timeout_s = kTimeoutValues[sel];
    }

    sys->temp_unit = st->use_fahrenheit
        ? aqua::storage::TempUnit::FAHRENHEIT
        : aqua::storage::TempUnit::CELSIUS;

    aqua::storage::save_system_config(*sys);
    // Apply active brightness immediately (dim_manager will pick up the rest
    // on its next 1-second tick via the shared sys_cfg pointer).
    aqua::display::backlight_set_percent(sys->brightness_active_pct);

    AC_LOGI(TAG, "saved: active=%u%% dim=%u%% timeout=%us unit=%s",
            (unsigned)sys->brightness_active_pct,
            (unsigned)sys->brightness_dim_pct,
            (unsigned)sys->inactivity_timeout_s,
            st->use_fahrenheit ? "F" : "C");

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
    // Fallback to defaults if context not yet set.
    static const aqua::storage::SystemConfig s_defaults;
    const auto& cfg = sys ? *sys : s_defaults;

    auto* st = new State();
    st->use_fahrenheit = (cfg.temp_unit == aqua::storage::TempUnit::FAHRENHEIT);

    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(root, on_screen_delete, LV_EVENT_DELETE, st);

    // Header with Save button. The chrome's action button uses nullptr
    // user_data by default; re-wire it so on_save receives our State*.
    chrome::build(root, tr(LangKey::NAV_DISPLAY), chrome::pop_on_back,
                  tr(LangKey::BTN_SAVE), on_save);
    {
        lv_obj_t* hdr = lv_obj_get_child(root, 0);
        uint32_t n = lv_obj_get_child_count(hdr);
        if (n > 0) {
            lv_obj_t* btn_save = lv_obj_get_child(hdr, (int32_t)(n - 1));
            lv_obj_remove_event_cb(btn_save, on_save);
            lv_obj_add_event_cb(btn_save, on_save, LV_EVENT_CLICKED, st);
        }
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

    // ── Display section ──────────────────────────────────────────────────

    make_section_label(scroll, tr(LangKey::DISP_SECTION_DISP));

    // Active brightness
    {
        lv_obj_t* row = make_row(scroll);
        make_body_label(row, tr(LangKey::DISP_ACTIVE_BRT));
        char buf[8];
        snprintf(buf, sizeof(buf), "%u %%", (unsigned)cfg.brightness_active_pct);
        st->lbl_active_val = make_value_label(row, buf);
    }
    st->slider_active = make_slider(scroll, 10, 100,
                                    cfg.brightness_active_pct);
    lv_obj_add_event_cb(st->slider_active, on_active_slider_changed,
                        LV_EVENT_VALUE_CHANGED, st);

    // Dim brightness
    {
        lv_obj_t* row = make_row(scroll);
        make_body_label(row, tr(LangKey::DISP_DIM_BRT));
        char buf[8];
        snprintf(buf, sizeof(buf), "%u %%", (unsigned)cfg.brightness_dim_pct);
        st->lbl_dim_val = make_value_label(row, buf);
    }
    st->slider_dim = make_slider(scroll, 5, 80, cfg.brightness_dim_pct);
    lv_obj_add_event_cb(st->slider_dim, on_dim_slider_changed,
                        LV_EVENT_VALUE_CHANGED, st);

    // Inactivity timeout
    {
        lv_obj_t* row = make_row(scroll);
        make_body_label(row, tr(LangKey::DISP_INACTIVITY));
        st->dd_timeout = lv_dropdown_create(row);
        char timeout_opts[128];
        snprintf(timeout_opts, sizeof(timeout_opts), "%s\n%s\n%s\n%s\n%s\n%s",
                 tr(LangKey::DISP_TIMEOUT_30S),
                 tr(LangKey::DISP_TIMEOUT_1M),
                 tr(LangKey::DISP_TIMEOUT_2M),
                 tr(LangKey::DISP_TIMEOUT_5M),
                 tr(LangKey::DISP_TIMEOUT_10M),
                 tr(LangKey::DISP_TIMEOUT_NEVER));
        lv_dropdown_set_options(st->dd_timeout, timeout_opts);
        lv_dropdown_set_selected(st->dd_timeout,
                                 timeout_to_index(cfg.inactivity_timeout_s));
        lv_obj_set_width(st->dd_timeout, 180);
        lv_obj_set_style_bg_color(st->dd_timeout, theme::color_surface_alt(), 0);
        lv_obj_set_style_bg_opa(st->dd_timeout, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(st->dd_timeout, theme::color_outline(), 0);
        lv_obj_set_style_text_font(st->dd_timeout, theme::font_body(), 0);
        lv_obj_set_style_text_color(st->dd_timeout, theme::color_text_primary(), 0);
        // Style the dropdown list.
        lv_obj_set_style_bg_color(
            lv_dropdown_get_list(st->dd_timeout),
            theme::color_surface(), 0);
        lv_obj_set_style_text_font(
            lv_dropdown_get_list(st->dd_timeout),
            theme::font_body(), 0);
        lv_obj_set_style_text_color(
            lv_dropdown_get_list(st->dd_timeout),
            theme::color_text_primary(), 0);
    }

    // ── Units section ────────────────────────────────────────────────────

    make_section_label(scroll, tr(LangKey::DISP_SECTION_UNITS));

    {
        lv_obj_t* row = make_row(scroll);
        make_body_label(row, tr(LangKey::DISP_TEMP_UNIT));
        lv_obj_t* btn_wrap = lv_obj_create(row);
        lv_obj_set_size(btn_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btn_wrap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_wrap, 0, 0);
        lv_obj_set_style_pad_all(btn_wrap, 0, 0);
        lv_obj_set_flex_flow(btn_wrap, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(btn_wrap, 8, 0);

        st->btn_celsius    = make_unit_btn(btn_wrap, tr(LangKey::UNIT_C));
        st->btn_fahrenheit = make_unit_btn(btn_wrap, tr(LangKey::UNIT_F));
        apply_unit_btn_styles(st);

        lv_obj_add_event_cb(st->btn_celsius,    on_celsius_clicked,
                            LV_EVENT_CLICKED, st);
        lv_obj_add_event_cb(st->btn_fahrenheit, on_fahrenheit_clicked,
                            LV_EVENT_CLICKED, st);
    }

    // ── Setup section ────────────────────────────────────────────────────

    make_section_label(scroll, tr(LangKey::DISP_SECTION_SETUP));

    {
        lv_obj_t* btn = lv_btn_create(scroll);
        lv_obj_set_size(btn, LV_PCT(100), 52);
        lv_obj_set_style_radius(btn, theme::RADIUS_SM, 0);
        lv_obj_set_style_bg_color(btn, theme::color_surface_alt(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, theme::color_outline(), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, theme::color_surface(),
                                  LV_STATE_PRESSED);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_SETTINGS "  Run Setup Wizard");
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, on_wizard_clicked, LV_EVENT_CLICKED, nullptr);
    }

    return root;
}

}  // namespace aqua::ui::display_system_screen
