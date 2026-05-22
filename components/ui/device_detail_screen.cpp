// AquaControl - Device detail / settings hub.
//
// LVGL patterns used here (all crash-safe):
//
//  * Per-button context allocation: every interactive widget gets its own
//    heap-allocated ActionCtx { State*, Action, int } stored in the event
//    user_data, plus an LV_EVENT_DELETE handler that frees it. Callbacks
//    NEVER fish the State out of lv_screen_active() (which is fragile
//    during screen swaps).
//
//  * Delete-confirmation modal: the destructive "Delete device" button
//    opens an lv_msgbox. Only the Confirm button actually removes the
//    device. The msgbox is closed via lv_msgbox_close_async so it tears
//    itself down between event dispatches rather than mid-click.
//
//  * Debounced NVS save: rapid stepper taps reset a 500 ms one-shot timer
//    instead of calling save_devices() on every press. The screen flushes
//    any pending save on destroy.
//
//  * No synchronous pop() chains: screen_manager::pop() is itself
//    deferred via lv_async_call, so click handlers may freely call it.
//
#include "device_detail_screen.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "ac_logger.h"
#include "chrome.h"
#include "config_storage.h"
#include "device_manager.h"
#include "device_types.h"
#include "i18n.h"
#include "pwm_device.h"
#include "relay_device.h"
#include "rgb_device.h"
#include "scheduler.h"
#include "screen_manager.h"
#include "theme.h"
#include "trigger_manager.h"
#include "trigger_types.h"
#include "ui_context.h"

namespace aqua::ui::device_detail_screen {

namespace {

using aqua::ui::i18n::LangKey;
using aqua::ui::i18n::tr;

constexpr const char* TAG = "DeviceDetail";

// One enum value per button. The on_action() dispatch reads this from the
// ActionCtx attached to the button.
enum class Action {
    TargetOn,
    TargetOff,
    CancelOverride,
    ModeUntilNext,
    Mode1h,
    Mode2h,
    Mode4h,
    Mode8h,
    ModeHold,
    PwmLevel,        // param = step delta (signed)
    PwmLevelLo,      // param = step delta (lo-end analog output)
    PwmFadeIn,       // param = step delta
    PwmFadeOut,
    RgbFadeIn,
    RgbFadeOut,
    RgbPreset,       // param = preset index
    RelayTogglePolarity,  // toggle active_high
    DeleteRequest,   // opens the confirm dialog
};

struct RgbPreset {
    LangKey                  name_key;
    aqua::devices::Hsv       hsv;
    lv_color_t               color;
};

static const RgbPreset s_rgb_presets[] = {
    {LangKey::RGB_WARM,    { 33.0f, 0.647f, 1.0f}, lv_color_hex(0xFFB45A)},
    {LangKey::RGB_COOL,    {214.0f, 0.137f, 1.0f}, lv_color_hex(0xDCEBFF)},
    {LangKey::RGB_RED,     {  0.0f, 1.000f, 1.0f}, lv_color_hex(0xFF0000)},
    {LangKey::RGB_GREEN,   {120.0f, 1.000f, 1.0f}, lv_color_hex(0x00FF00)},
    {LangKey::RGB_BLUE,    {240.0f, 1.000f, 1.0f}, lv_color_hex(0x0000FF)},
    {LangKey::RGB_MAGENTA, {300.0f, 1.000f, 1.0f}, lv_color_hex(0xFF00FF)},
    {LangKey::RGB_CYAN,    {180.0f, 1.000f, 1.0f}, lv_color_hex(0x00FFFF)},
};

struct State {
    uint8_t     device_id      = 0;
    bool        target_active  = true;
    bool        trigger_linked = false;

    // State card labels
    lv_obj_t*   lbl_state_big   = nullptr;
    lv_obj_t*   lbl_state_sub   = nullptr;
    lv_obj_t*   lbl_override    = nullptr;

    // Control card pills
    lv_obj_t*   btn_on          = nullptr;
    lv_obj_t*   btn_off         = nullptr;
    lv_obj_t*   btn_cancel      = nullptr;

    // PWM labels
    lv_obj_t*   lbl_pwm_level    = nullptr;
    lv_obj_t*   lbl_pwm_level_lo = nullptr;
    lv_obj_t*   lbl_pwm_fin      = nullptr;
    lv_obj_t*   lbl_pwm_fout     = nullptr;

    // RGB labels
    lv_obj_t*   lbl_rgb_fin       = nullptr;
    lv_obj_t*   lbl_rgb_fout      = nullptr;
    lv_obj_t*   lbl_rgb_swatch    = nullptr;   // Hi color preview
    lv_obj_t*   lbl_rgb_swatch_lo = nullptr;   // Lo color preview
    lv_obj_t*   sld_rgb_h         = nullptr;   // Hi: hue 0-360
    lv_obj_t*   sld_rgb_s         = nullptr;   // Hi: saturation 0-100
    lv_obj_t*   sld_rgb_v         = nullptr;   // Hi: value 0-100
    lv_obj_t*   sld_rgb_h_lo      = nullptr;   // Lo: hue 0-360
    lv_obj_t*   sld_rgb_s_lo      = nullptr;   // Lo: saturation 0-100
    lv_obj_t*   sld_rgb_v_lo      = nullptr;   // Lo: value 0-100

    // Relay labels
    lv_obj_t*   lbl_relay_polarity = nullptr;

    // Name editing
    lv_obj_t*   ta_name         = nullptr;
    lv_obj_t*   kbd_name        = nullptr;  // keyboard on lv_layer_top(), or nullptr
    bool        kbd_ru          = false;    // current keyboard lang (false=EN, true=RU)

    lv_timer_t* refresh_timer   = nullptr;

    // Debounced NVS save.
    lv_timer_t* save_timer      = nullptr;
    bool        save_pending    = false;
};

// One per interactive widget. Lifetime is tied to the widget; the widget's
// LV_EVENT_DELETE handler frees it.
struct ActionCtx {
    State*  st     = nullptr;
    Action  act    = Action::TargetOn;
    int     param  = 0;
};

// --- lookups ----------------------------------------------------------------

static aqua::devices::IDevice* lookup(uint8_t id) {
    const auto& ctx = ui_context();
    if (!ctx.devices) return nullptr;
    return ctx.devices->find(id);
}

static bool device_has_triggers(uint8_t device_id) {
    const auto& ctx = ui_context();
    if (!ctx.triggers) return false;
    bool linked = false;
    ctx.triggers->for_each([&](aqua::triggers::ITrigger& t) {
        for (uint8_t id : t.linked_device_ids) {
            if (id == device_id) { linked = true; break; }
        }
    });
    return linked;
}

// --- debounced save --------------------------------------------------------

static void flush_save(State* st) {
    if (!st || !st->save_pending) return;
    const auto& ctx = ui_context();
    if (ctx.devices) {
        aqua::storage::save_devices(*ctx.devices);
    }
    st->save_pending = false;
    aqua::scheduler::wake_now();

    // UI-4: Show a brief "Saved" toast so the user knows the change persisted.
    lv_obj_t* layer = lv_layer_top();
    lv_obj_t* toast = lv_label_create(layer);
    lv_label_set_text(toast, i18n::tr(i18n::LangKey::MSG_SAVED));
    lv_obj_set_style_text_font(toast, theme::font_body(), 0);
    lv_obj_set_style_text_color(toast, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(toast, theme::PAD_MD, 0);
    lv_obj_set_style_pad_ver(toast, theme::PAD_SM, 0);
    lv_obj_set_style_radius(toast, theme::RADIUS_PILL, 0);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -theme::PAD_LG);
    // Auto-delete after 1500 ms via a one-shot timer.
    lv_timer_t* t = lv_timer_create([](lv_timer_t* tmr) {
        lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_timer_get_user_data(tmr));
        if (lbl && lv_obj_is_valid(lbl)) lv_obj_del(lbl);
        lv_timer_del(tmr);
    }, 1500, toast);
    (void)t;
}

static void save_timer_cb(lv_timer_t* t) {
    auto* st = static_cast<State*>(lv_timer_get_user_data(t));
    flush_save(st);
    lv_timer_pause(t);  // single-shot
}

static void schedule_save(State* st) {
    if (!st) return;
    st->save_pending = true;
    if (!st->save_timer) {
        st->save_timer = lv_timer_create(save_timer_cb, 500, st);
    } else {
        lv_timer_reset(st->save_timer);
        lv_timer_resume(st->save_timer);
    }
}

// --- styling helpers --------------------------------------------------------

static void style_pill(lv_obj_t* btn) {
    lv_obj_set_style_radius(btn, theme::RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(btn, theme::color_surface_alt(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, theme::color_outline(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_text_color(btn, theme::color_text_primary(), 0);
    lv_obj_set_style_bg_color(btn, theme::color_accent(), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, theme::color_background(),
                                LV_STATE_PRESSED);
}

static void style_pill_selected(lv_obj_t* btn, bool selected) {
    if (selected) {
        lv_obj_set_style_bg_color(btn, theme::color_accent(), 0);
        lv_obj_set_style_border_color(btn, theme::color_accent_2(), 0);
        lv_obj_set_style_text_color(btn, theme::color_background(), 0);
    } else {
        lv_obj_set_style_bg_color(btn, theme::color_surface_alt(), 0);
        lv_obj_set_style_border_color(btn, theme::color_outline(), 0);
        lv_obj_set_style_text_color(btn, theme::color_text_primary(), 0);
    }
}

// --- forward decls ---------------------------------------------------------

static void on_action(lv_event_t* e);
static void on_ctx_delete(lv_event_t* e);
static void apply_override(State* st, aqua::devices::OverrideMode mode,
                           uint32_t duration_s, bool target);
static void open_delete_dialog(State* st);
static void refresh(State* st);

// Allocate an ActionCtx, attach click + delete handlers.
static void bind_action(lv_obj_t* widget, State* st, Action act, int param) {
    // H-6: use nothrow — OOM must not crash the device during a UI interaction.
    auto* c = new(std::nothrow) ActionCtx{st, act, param};
    if (!c) {
        AC_LOGE(TAG, "ActionCtx alloc failed — button inoperative");
        return;
    }
    lv_obj_add_event_cb(widget, on_action,      LV_EVENT_CLICKED, c);
    lv_obj_add_event_cb(widget, on_ctx_delete,  LV_EVENT_DELETE,  c);
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text,
                           int16_t w, int16_t h,
                           State* st, Action act, int param = 0) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    style_pill(btn);
    lv_obj_t* l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, theme::font_body(), 0);
    lv_obj_center(l);
    if (st) bind_action(btn, st, act, param);
    return btn;
}

// Card container (uses default lv_obj base style; we set our look on top).
static lv_obj_t* make_card(lv_obj_t* parent, const char* title) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, theme::color_outline(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_70, 0);
    lv_obj_set_style_radius(card, theme::RADIUS_LG, 0);
    lv_obj_set_style_pad_all(card, theme::PAD_MD, 0);
    lv_obj_set_style_pad_row(card, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (title && *title) {
        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl,
                                    theme::color_text_secondary(), 0);
    }
    return card;
}

// Horizontal row inside a card.
static lv_obj_t* make_row(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

// caption | [-] value [+]
//
// Returns the value label so the caller can update it on each step.
static lv_obj_t* make_stepper_row(lv_obj_t* parent, const char* caption,
                                  const char* initial_value,
                                  State* st, Action minus_act,
                                  int minus_param,
                                  Action plus_act, int plus_param) {
    lv_obj_t* row = make_row(parent);

    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, caption);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
    lv_obj_set_flex_grow(lbl, 1);

    make_pill(row, "-", 60, theme::TOUCH_MIN, st, minus_act, minus_param);

    lv_obj_t* val = lv_label_create(row);
    lv_label_set_text(val, initial_value);
    lv_obj_set_style_text_font(val, theme::font_title(), 0);
    lv_obj_set_style_text_color(val, theme::color_accent(), 0);
    lv_obj_set_width(val, 110);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);

    make_pill(row, "+", 60, theme::TOUCH_MIN, st, plus_act, plus_param);
    return val;
}

// --- override application ---------------------------------------------------

static void apply_override(State* st, aqua::devices::OverrideMode mode,
                           uint32_t duration_s, bool target) {
    if (!st) return;
    auto* dev = lookup(st->device_id);
    if (!dev) return;
    time_t until = 0;
    if (mode == aqua::devices::OverrideMode::TIMED) {
        until = time(nullptr) + (time_t)duration_s;
    }
    dev->set_override(mode, target, until);
    AC_LOGI(TAG, "device %u override: mode=%d target=%d until=%lld",
            (unsigned)dev->id, (int)mode, (int)target,
            (long long)until);
    aqua::scheduler::wake_now();
}

// --- stepper helpers (PWM / RGB) -------------------------------------------

static void pwm_level_step(State* st, int delta) {
    auto* dev = lookup(st->device_id);
    if (!dev || dev->get_type() != aqua::devices::DeviceType::PWM) return;
    auto* p = static_cast<aqua::devices::PwmDevice*>(dev);
    int v = (int)p->level_pct + delta;
    p->level_pct = (uint8_t)std::clamp(v, 0, 100);
    if (st->lbl_pwm_level) {
        char b[16]; snprintf(b, sizeof(b), "%u%%",
                             (unsigned)p->level_pct);
        lv_label_set_text(st->lbl_pwm_level, b);
    }
    // Live apply: if device is currently active, reflect the new level now.
    if (p->current_active()) {
        p->apply(true, /*force=*/true);
    }
    schedule_save(st);
}

static void pwm_level_lo_step(State* st, int delta) {
    auto* dev = lookup(st->device_id);
    if (!dev || dev->get_type() != aqua::devices::DeviceType::PWM) return;
    auto* p = static_cast<aqua::devices::PwmDevice*>(dev);
    int v = (int)p->level_lo_pct + delta;
    p->level_lo_pct = (uint8_t)std::clamp(v, 0, 100);
    if (st->lbl_pwm_level_lo) {
        char b[16]; snprintf(b, sizeof(b), "%u%%", (unsigned)p->level_lo_pct);
        lv_label_set_text(st->lbl_pwm_level_lo, b);
    }
    schedule_save(st);
}

static void pwm_fade_step(State* st, int delta, bool is_fade_in) {
    auto* dev = lookup(st->device_id);
    if (!dev || dev->get_type() != aqua::devices::DeviceType::PWM) return;
    auto* p = static_cast<aqua::devices::PwmDevice*>(dev);
    int v = (int)(is_fade_in ? p->fade_in_min : p->fade_out_min) + delta;
    v = std::clamp(v, 0, 240);
    if (is_fade_in) p->fade_in_min  = (uint16_t)v;
    else            p->fade_out_min = (uint16_t)v;
    lv_obj_t* lbl = is_fade_in ? st->lbl_pwm_fin : st->lbl_pwm_fout;
    if (lbl) {
        char b[16]; snprintf(b, sizeof(b), "%d min", v);
        lv_label_set_text(lbl, b);
    }
    schedule_save(st);
}

static void rgb_fade_step(State* st, int delta, bool is_fade_in) {
    auto* dev = lookup(st->device_id);
    if (!dev || dev->get_type() != aqua::devices::DeviceType::RGB) return;
    auto* g = static_cast<aqua::devices::RgbDevice*>(dev);
    int v = (int)(is_fade_in ? g->fade_in_min : g->fade_out_min) + delta;
    v = std::clamp(v, 0, 240);
    if (is_fade_in) g->fade_in_min  = (uint16_t)v;
    else            g->fade_out_min = (uint16_t)v;
    lv_obj_t* lbl = is_fade_in ? st->lbl_rgb_fin : st->lbl_rgb_fout;
    if (lbl) {
        char b[16]; snprintf(b, sizeof(b), "%d min", v);
        lv_label_set_text(lbl, b);
    }
    schedule_save(st);
}

// Sync slider positions to the device's current HSV color (no save, no apply).
static void refresh_rgb_sliders(State* st, aqua::devices::RgbDevice* g);  // fwd

// Derive an lv_color_t from an Hsv value.
static inline lv_color_t hsv_to_lvc(aqua::devices::Hsv hsv) {
    aqua::devices::Rgb8 rgb = aqua::devices::hsv_to_rgb(hsv);
    return lv_color_make(rgb.r, rgb.g, rgb.b);
}
static inline void set_sld_color(lv_obj_t* sld, lv_color_t c) {
    if (!sld) return;
    lv_obj_set_style_bg_color(sld, c, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sld, c, LV_PART_KNOB);
}
// Update each slider's indicator so it reflects HSV semantics:
//   H slider → pure hue at full S/V
//   S slider → current hue at current S, full V
//   V slider → full current color (same as swatch)
static void update_slider_colors(State* st, aqua::devices::RgbDevice* g) {
    if (!g) return;
    auto& hi = g->color_hsv;
    set_sld_color(st->sld_rgb_h,    hsv_to_lvc({hi.h, 1.0f, 1.0f}));
    set_sld_color(st->sld_rgb_s,    hsv_to_lvc({hi.h, hi.s, 1.0f}));
    set_sld_color(st->sld_rgb_v,    hsv_to_lvc(hi));
    auto& lo = g->color_lo_hsv;
    set_sld_color(st->sld_rgb_h_lo, hsv_to_lvc({lo.h, 1.0f, 1.0f}));
    set_sld_color(st->sld_rgb_s_lo, hsv_to_lvc({lo.h, lo.s, 1.0f}));
    set_sld_color(st->sld_rgb_v_lo, hsv_to_lvc(lo));
}

static void refresh_rgb_sliders(State* st, aqua::devices::RgbDevice* g) {
    if (st->sld_rgb_h)    lv_slider_set_value(st->sld_rgb_h,    (int)g->color_hsv.h, LV_ANIM_OFF);
    if (st->sld_rgb_s)    lv_slider_set_value(st->sld_rgb_s,    (int)(g->color_hsv.s * 100.0f + 0.5f), LV_ANIM_OFF);
    if (st->sld_rgb_v)    lv_slider_set_value(st->sld_rgb_v,    (int)(g->color_hsv.v * 100.0f + 0.5f), LV_ANIM_OFF);
    if (st->sld_rgb_h_lo) lv_slider_set_value(st->sld_rgb_h_lo, (int)g->color_lo_hsv.h, LV_ANIM_OFF);
    if (st->sld_rgb_s_lo) lv_slider_set_value(st->sld_rgb_s_lo, (int)(g->color_lo_hsv.s * 100.0f + 0.5f), LV_ANIM_OFF);
    if (st->sld_rgb_v_lo) lv_slider_set_value(st->sld_rgb_v_lo, (int)(g->color_lo_hsv.v * 100.0f + 0.5f), LV_ANIM_OFF);
    update_slider_colors(st, g);
}

// Per-slider context allocated once and stored via lv_obj_set_user_data.
struct RgbSliderCtx { State* st; int channel; bool is_lo; };

// Called from LVGL VALUE_CHANGED event on one of the 3 HSV sliders.
static void on_rgb_slider(lv_event_t* e) {
    auto* ctx = static_cast<RgbSliderCtx*>(lv_event_get_user_data(e));
    if (!ctx) return;
    State* st = ctx->st;
    auto* dev = lookup(st->device_id);
    if (!dev || dev->get_type() != aqua::devices::DeviceType::RGB) return;
    auto* g = static_cast<aqua::devices::RgbDevice*>(dev);
    lv_obj_t* sld = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int val = (int)lv_slider_get_value(sld);
    // Route to hi or lo color field.
    auto& color = ctx->is_lo ? g->color_lo_hsv : g->color_hsv;
    // channel 0 = H (0-360), 1 = S (0-100), 2 = V (0-100)
    if (ctx->channel == 0) {
        color.h = (float)std::clamp(val, 0, 360);
    } else if (ctx->channel == 1) {
        color.s = (float)std::clamp(val, 0, 100) / 100.0f;
    } else {
        color.v = (float)std::clamp(val, 0, 100) / 100.0f;
    }
    // Update the swatch for the group that changed.
    aqua::devices::Rgb8 rgb = aqua::devices::hsv_to_rgb(color);
    lv_color_t lvc = lv_color_make(rgb.r, rgb.g, rgb.b);
    lv_obj_t* swatch = ctx->is_lo ? st->lbl_rgb_swatch_lo : st->lbl_rgb_swatch;
    if (swatch) lv_obj_set_style_bg_color(swatch, lvc, 0);
    // Update all slider indicator colors.
    update_slider_colors(st, g);
    // Live apply only for hi color changes while device is active.
    if (!ctx->is_lo && g->current_active()) g->apply(true, /*force=*/true);
    schedule_save(st);
}

static void rgb_apply_preset(State* st, int idx) {
    auto* dev = lookup(st->device_id);
    if (!dev || dev->get_type() != aqua::devices::DeviceType::RGB) return;
    auto* g = static_cast<aqua::devices::RgbDevice*>(dev);
    const auto& p = s_rgb_presets[idx];
    g->color_hsv = p.hsv;
    aqua::devices::Rgb8 rgb = aqua::devices::hsv_to_rgb(p.hsv);
    if (st->lbl_rgb_swatch) {
        lv_obj_set_style_bg_color(st->lbl_rgb_swatch,
            lv_color_make(rgb.r, rgb.g, rgb.b), 0);
    }
    refresh_rgb_sliders(st, g);  // also calls update_slider_colors
    // Live apply: if device is currently active, reflect the new color now.
    if (g->current_active()) {
        g->apply(true, /*force=*/true);
    }
    schedule_save(st);
}

// --- target toggle (manual mode) -------------------------------------------

static void set_target(State* st, bool active) {
    st->target_active = active;
    style_pill_selected(st->btn_on,  active);
    style_pill_selected(st->btn_off, !active);
    // Trigger-less devices: ON/OFF IS the manual hold itself.
    if (!st->trigger_linked) {
        apply_override(st, aqua::devices::OverrideMode::INDEFINITE,
                       0, active);
    }
}

// --- delete confirmation modal ---------------------------------------------

// We keep this tiny struct attached to the confirm button so it knows which
// device to delete.
struct ConfirmCtx {
    State*    st;
    lv_obj_t* mbox;
};

static void on_confirm_delete_widget_delete(lv_event_t* e) {
    delete static_cast<ConfirmCtx*>(lv_event_get_user_data(e));
}

static void on_confirm_delete(lv_event_t* e) {
    auto* cc = static_cast<ConfirmCtx*>(lv_event_get_user_data(e));
    if (!cc || !cc->st) return;
    State* st = cc->st;
    lv_obj_t* mbox = cc->mbox;
    uint8_t did = st->device_id;
    AC_LOGI(TAG, "delete device %u confirmed", (unsigned)did);

    const auto& ctx = ui_context();
    if (ctx.devices) {
        ctx.devices->remove(did);
        aqua::storage::save_devices(*ctx.devices);
        st->save_pending = false;
    }
    aqua::scheduler::wake_now();

    // Close the modal and pop the screen — both deferred via lv_async so
    // this click handler returns cleanly before either teardown begins.
    if (mbox) lv_msgbox_close_async(mbox);
    screen_manager::pop();
}

static void on_cancel_delete(lv_event_t* e) {
    auto* cc = static_cast<ConfirmCtx*>(lv_event_get_user_data(e));
    if (cc && cc->mbox) lv_msgbox_close_async(cc->mbox);
}

static void open_delete_dialog(State* st) {
    if (!st) return;
    auto* dev = lookup(st->device_id);
    if (!dev) return;

    lv_obj_t* mbox = lv_msgbox_create(nullptr);  // modal on top layer

    lv_msgbox_add_title(mbox, tr(LangKey::DEV_DELETE_TITLE));

    char body[160];
    snprintf(body, sizeof(body), tr(LangKey::DEV_DELETE_CONFIRM),
             dev->name.c_str());
    lv_msgbox_add_text(mbox, body);

    lv_obj_t* btn_cancel = lv_msgbox_add_footer_button(mbox, tr(LangKey::BTN_CANCEL));
    char del_btn_text[64];
    snprintf(del_btn_text, sizeof(del_btn_text), "%s  %s", LV_SYMBOL_TRASH, tr(LangKey::BTN_DELETE));
    lv_obj_t* btn_confirm = lv_msgbox_add_footer_button(mbox, del_btn_text);
    lv_obj_set_style_bg_color(btn_confirm, theme::color_error(), 0);
    lv_obj_set_style_text_color(btn_confirm,
                                theme::color_text_primary(), 0);

    // The two footer buttons each get their own ConfirmCtx, freed on
    // LV_EVENT_DELETE so closing the msgbox cleans up properly.
    auto* cc_confirm = new ConfirmCtx{st, mbox};
    lv_obj_add_event_cb(btn_confirm, on_confirm_delete,
                        LV_EVENT_CLICKED, cc_confirm);
    lv_obj_add_event_cb(btn_confirm, on_confirm_delete_widget_delete,
                        LV_EVENT_DELETE, cc_confirm);

    auto* cc_cancel = new ConfirmCtx{st, mbox};
    lv_obj_add_event_cb(btn_cancel, on_cancel_delete,
                        LV_EVENT_CLICKED, cc_cancel);
    lv_obj_add_event_cb(btn_cancel, on_confirm_delete_widget_delete,
                        LV_EVENT_DELETE, cc_cancel);
}

// --- unified action dispatch -----------------------------------------------

static void on_ctx_delete(lv_event_t* e) {
    delete static_cast<ActionCtx*>(lv_event_get_user_data(e));
}

static void on_action(lv_event_t* e) {
    auto* c = static_cast<ActionCtx*>(lv_event_get_user_data(e));
    if (!c || !c->st) return;
    State* st = c->st;

    switch (c->act) {
        case Action::TargetOn:        set_target(st, true);  break;
        case Action::TargetOff:       set_target(st, false); break;

        case Action::CancelOverride: {
            auto* dev = lookup(st->device_id);
            if (!dev) break;
            if (dev->has_override()) {
                dev->clear_override();
                AC_LOGI(TAG, "device %u override cleared",
                        (unsigned)dev->id);
                aqua::scheduler::wake_now();
            }
        } break;

        case Action::ModeUntilNext:
            apply_override(st,
                aqua::devices::OverrideMode::UNTIL_NEXT, 0,
                st->target_active);
            break;
        case Action::Mode1h:
            apply_override(st,
                aqua::devices::OverrideMode::TIMED, 1 * 3600,
                st->target_active);
            break;
        case Action::Mode2h:
            apply_override(st,
                aqua::devices::OverrideMode::TIMED, 2 * 3600,
                st->target_active);
            break;
        case Action::Mode4h:
            apply_override(st,
                aqua::devices::OverrideMode::TIMED, 4 * 3600,
                st->target_active);
            break;
        case Action::Mode8h:
            apply_override(st,
                aqua::devices::OverrideMode::TIMED, 8 * 3600,
                st->target_active);
            break;
        case Action::ModeHold:
            apply_override(st,
                aqua::devices::OverrideMode::INDEFINITE, 0,
                st->target_active);
            break;

        case Action::PwmLevel:    pwm_level_step   (st, c->param);            break;
        case Action::PwmLevelLo:  pwm_level_lo_step(st, c->param);            break;
        case Action::PwmFadeIn:   pwm_fade_step (st, c->param, true);      break;
        case Action::PwmFadeOut:  pwm_fade_step (st, c->param, false);     break;
        case Action::RgbFadeIn:   rgb_fade_step(st, c->param, true);       break;
        case Action::RgbFadeOut:  rgb_fade_step(st, c->param, false);      break;
        case Action::RgbPreset:   rgb_apply_preset(st, c->param);          break;
        case Action::RelayTogglePolarity: {
            auto* dev2 = lookup(st->device_id);
            if (dev2 && dev2->get_type() == aqua::devices::DeviceType::RELAY) {
                auto* r = static_cast<aqua::devices::RelayDevice*>(dev2);
                r->active_high = !r->active_high;
                const auto& ctx2 = ui_context();
                if (ctx2.devices) aqua::storage::save_devices(*ctx2.devices);
                refresh(st);
            }
            break;
        }
        case Action::DeleteRequest: open_delete_dialog(st); break;
    }
}

// --- live refresh ----------------------------------------------------------

static void refresh(State* st) {
    if (!st) return;
    auto* dev = lookup(st->device_id);
    if (!dev) return;

    if (st->lbl_state_big) {
        if (dev->get_type() == aqua::devices::DeviceType::PWM) {
            auto* pwm = static_cast<aqua::devices::PwmDevice*>(dev);
            switch (pwm->fade_status()) {
                case aqua::devices::PwmDevice::FadeStatus::FADING_IN:
                    lv_label_set_text(st->lbl_state_big, tr(LangKey::DEV_FADING_IN));
                    lv_obj_set_style_text_color(st->lbl_state_big,
                        theme::color_accent(), 0);
                    break;
                case aqua::devices::PwmDevice::FadeStatus::FADING_OUT:
                    lv_label_set_text(st->lbl_state_big, tr(LangKey::DEV_FADING_OUT));
                    lv_obj_set_style_text_color(st->lbl_state_big,
                        theme::color_text_secondary(), 0);
                    break;
                default: {
                    const bool on = dev->current_active();
                    lv_label_set_text(st->lbl_state_big, on ? tr(LangKey::DEV_ON) : tr(LangKey::DEV_OFF));
                    lv_obj_set_style_text_color(st->lbl_state_big,
                        on ? theme::color_success()
                           : theme::color_text_secondary(), 0);
                    break;
                }
            }
        } else {
            const bool on = dev->current_active();
            lv_label_set_text(st->lbl_state_big, on ? tr(LangKey::DEV_ON) : tr(LangKey::DEV_OFF));
            lv_obj_set_style_text_color(st->lbl_state_big,
                on ? theme::color_success()
                   : theme::color_text_secondary(), 0);
        }
    }
    if (st->lbl_state_sub) {
        char sub[96];
        uint8_t ch = 0;
        const char* type_word = tr(LangKey::DEV_DEVICE);
        if (dev->get_type() == aqua::devices::DeviceType::RELAY) {
            ch = static_cast<aqua::devices::RelayDevice*>(dev)->channel();
            type_word = tr(LangKey::DEV_RELAY);
        } else if (dev->get_type() == aqua::devices::DeviceType::PWM) {
            ch = static_cast<aqua::devices::PwmDevice*>(dev)->channel();
            type_word = tr(LangKey::DEV_PWM);
        } else if (dev->get_type() == aqua::devices::DeviceType::RGB) {
            ch = static_cast<aqua::devices::RgbDevice*>(dev)->base_channel();
            type_word = tr(LangKey::DEV_RGB);
        }
        snprintf(sub, sizeof(sub),
                 "%s  -  ch %u  -  %s",
                 type_word, (unsigned)ch,
                 st->trigger_linked ? tr(LangKey::DEV_TRIGGER_DRIVEN)
                                    : tr(LangKey::DEV_MANUAL_ONLY));
        lv_label_set_text(st->lbl_state_sub, sub);
    }
    if (st->lbl_override) {
        if (dev->has_override()) {
            char ob[96];
            switch (dev->override_mode()) {
                case aqua::devices::OverrideMode::INDEFINITE:
                    snprintf(ob, sizeof(ob),
                             "%s  %s %s",
                             LV_SYMBOL_PAUSE,
                             tr(LangKey::DEV_HELD),
                             dev->override_target() ? tr(LangKey::DEV_ON) : tr(LangKey::DEV_OFF));
                    break;
                case aqua::devices::OverrideMode::UNTIL_NEXT:
                    snprintf(ob, sizeof(ob),
                             "%s  %s %s",
                             LV_SYMBOL_BELL,
                             dev->override_target() ? tr(LangKey::DEV_ON) : tr(LangKey::DEV_OFF),
                             tr(LangKey::DEV_UNTIL_NEXT_TRG));
                    break;
                case aqua::devices::OverrideMode::TIMED: {
                    long rem = (long)(dev->override_until_epoch_utc() -
                                      time(nullptr));
                    if (rem < 0) rem = 0;
                    char dur[24];
                    snprintf(dur, sizeof(dur), tr(LangKey::DEV_FOR_MINUTES), (int)(rem / 60));
                    snprintf(ob, sizeof(ob),
                             "%s  %s %s",
                             LV_SYMBOL_BELL,
                             dev->override_target() ? tr(LangKey::DEV_ON) : tr(LangKey::DEV_OFF),
                             dur);
                    break;
                }
                default: ob[0] = 0; break;
            }
            lv_label_set_text(st->lbl_override, ob);
            lv_obj_set_style_text_color(st->lbl_override,
                                        theme::color_warning(), 0);
        } else {
            lv_label_set_text(st->lbl_override,
                              st->trigger_linked ? tr(LangKey::DEV_FOLLOWS_TRG)
                                                  : tr(LangKey::DEV_NO_HOLD));
            lv_obj_set_style_text_color(st->lbl_override,
                                        theme::color_text_secondary(), 0);
        }
    }
    if (st->btn_cancel) {
        if (dev->has_override()) {
            lv_obj_clear_flag(st->btn_cancel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(st->btn_cancel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (st->lbl_relay_polarity) {
        auto* r = static_cast<aqua::devices::RelayDevice*>(dev);
        lv_label_set_text(st->lbl_relay_polarity,
            r->active_high ? tr(LangKey::DEV_ACTIVE_HIGH)
                           : tr(LangKey::DEV_ACTIVE_LOW));
    }
}

static void refresh_cb(lv_timer_t* t) {
    auto* st = static_cast<State*>(lv_timer_get_user_data(t));
    refresh(st);
}

// LV_EVENT_DELETE on the screen root: tear down timers + flush pending
// save before freeing the State.
static void name_kbd_hide(State* st);  // forward decl

static void on_root_delete(lv_event_t* e) {
    // Only handle the root's own delete event (don't react to child deletes
    // that may bubble up — only happens if EVENT_BUBBLE is set, which it
    // isn't here, but the check is cheap).
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;

    auto* root = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* st = static_cast<State*>(lv_obj_get_user_data(root));
    if (!st) return;

    // If the user edited the device name but exited without pressing ✓,
    // persist the current textarea text now.
    if (st->ta_name) {
        const char* pending_name = lv_textarea_get_text(st->ta_name);
        auto* dev_ptr = lookup(st->device_id);
        if (dev_ptr && pending_name && strlen(pending_name) > 0) {
            dev_ptr->name = pending_name;
            st->save_pending = true;
        }
    }

    if (st->save_timer) {
        lv_timer_del(st->save_timer);
        st->save_timer = nullptr;
    }
    if (st->save_pending) {
        const auto& ctx = ui_context();
        if (ctx.devices) {
            aqua::storage::save_devices(*ctx.devices);
        }
        aqua::scheduler::wake_now();
        st->save_pending = false;
    }
    if (st->refresh_timer) {
        lv_timer_del(st->refresh_timer);
        st->refresh_timer = nullptr;
    }
    // Clean up floating keyboard if screen is popped while keyboard is open
    name_kbd_hide(st);
    delete st;
    lv_obj_set_user_data(root, nullptr);
}

// --- section builders ------------------------------------------------------

static void build_state_card(State* st, lv_obj_t* parent) {
    lv_obj_t* card = make_card(parent, tr(LangKey::DEV_CURRENT_STATE));
    lv_obj_t* row  = make_row(card);

    st->lbl_state_big = lv_label_create(row);
    lv_label_set_text(st->lbl_state_big, "--");
    lv_obj_set_style_text_font(st->lbl_state_big,
                               theme::font_display(), 0);
    lv_obj_set_style_text_color(st->lbl_state_big,
                                theme::color_text_secondary(), 0);

    lv_obj_t* col = lv_obj_create(row);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_left(col, theme::PAD_MD, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    st->lbl_state_sub = lv_label_create(col);
    lv_label_set_text(st->lbl_state_sub, "");
    lv_obj_set_style_text_font(st->lbl_state_sub, theme::font_caption(), 0);
    lv_obj_set_style_text_color(st->lbl_state_sub,
                                theme::color_text_secondary(), 0);

    st->lbl_override = lv_label_create(col);
    lv_label_set_text(st->lbl_override, "");
    lv_obj_set_style_text_font(st->lbl_override, theme::font_body(), 0);
}

static void build_control_card(State* st, lv_obj_t* parent) {
    const char* title = st->trigger_linked ? tr(LangKey::DEV_OVERRIDE_CTRL)
                                            : tr(LangKey::DEV_MANUAL_CTRL);
    lv_obj_t* card = make_card(parent, title);

    lv_obj_t* row1 = make_row(card);
    char btn_on_text[48], btn_off_text[48];
    snprintf(btn_on_text, sizeof(btn_on_text), "%s  %s", LV_SYMBOL_POWER, tr(LangKey::DEV_TURN_ON));
    snprintf(btn_off_text, sizeof(btn_off_text), "%s  %s", LV_SYMBOL_POWER, tr(LangKey::DEV_TURN_OFF));
    st->btn_on  = make_pill(row1, btn_on_text,
                            220, theme::TOUCH_MIN,
                            st, Action::TargetOn);
    st->btn_off = make_pill(row1, btn_off_text,
                            220, theme::TOUCH_MIN,
                            st, Action::TargetOff);
    style_pill_selected(st->btn_on,  st->target_active);
    style_pill_selected(st->btn_off, !st->target_active);

    if (st->trigger_linked) {
        lv_obj_t* hint = lv_label_create(card);
        lv_label_set_text(hint, tr(LangKey::DEV_OVERRIDE_HINT));
        lv_obj_set_style_text_font(hint, theme::font_caption(), 0);
        lv_obj_set_style_text_color(hint,
                                    theme::color_text_secondary(), 0);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, LV_PCT(100));

        lv_obj_t* row2 = make_row(card);
        make_pill(row2, tr(LangKey::DEV_UNTIL_NEXT),  150, theme::TOUCH_MIN,
                  st, Action::ModeUntilNext);
        make_pill(row2, "1h",   80, theme::TOUCH_MIN, st, Action::Mode1h);
        make_pill(row2, "2h",   80, theme::TOUCH_MIN, st, Action::Mode2h);
        make_pill(row2, "4h",   80, theme::TOUCH_MIN, st, Action::Mode4h);
        make_pill(row2, "8h",   80, theme::TOUCH_MIN, st, Action::Mode8h);
        char hold_text[48];
        snprintf(hold_text, sizeof(hold_text), "%s  %s", LV_SYMBOL_PAUSE, tr(LangKey::DEV_HOLD));
        make_pill(row2, hold_text, 130, theme::TOUCH_MIN, st, Action::ModeHold);
    } else {
        lv_obj_t* hint = lv_label_create(card);
        lv_label_set_text(hint, tr(LangKey::DEV_NOT_LINKED));
        lv_obj_set_style_text_font(hint, theme::font_caption(), 0);
        lv_obj_set_style_text_color(hint,
                                    theme::color_text_secondary(), 0);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, LV_PCT(100));
    }

    char cancel_text[64];
    snprintf(cancel_text, sizeof(cancel_text), "%s  %s", LV_SYMBOL_CLOSE, tr(LangKey::DEV_CANCEL_HOLD));
    st->btn_cancel = make_pill(card,
        cancel_text, 280, theme::TOUCH_MIN,
        st, Action::CancelOverride);
    lv_obj_set_style_bg_color(st->btn_cancel, theme::color_warning(), 0);
    lv_obj_set_style_border_color(st->btn_cancel,
                                  theme::color_warning(), 0);
    lv_obj_set_style_text_color(st->btn_cancel,
                                theme::color_background(), 0);
}

static void build_pwm_card(State* st, lv_obj_t* parent,
                           aqua::devices::PwmDevice& p) {
    lv_obj_t* card = make_card(parent, tr(LangKey::DEV_PWM_SETTINGS));
    char b[16];

    snprintf(b, sizeof(b), "%u%%", (unsigned)p.level_pct);
    st->lbl_pwm_level = make_stepper_row(card, tr(LangKey::DEV_LEVEL), b,
        st, Action::PwmLevel, -5, Action::PwmLevel, +5);

    snprintf(b, sizeof(b), "%u%%", (unsigned)p.level_lo_pct);
    st->lbl_pwm_level_lo = make_stepper_row(card, tr(LangKey::DEV_LEVEL_LO), b,
        st, Action::PwmLevelLo, -5, Action::PwmLevelLo, +5);
    {
        lv_obj_t* hint = lv_label_create(card);
        lv_label_set_text(hint, tr(LangKey::DEV_LEVEL_LO_HINT));
        lv_obj_set_style_text_font(hint, theme::font_caption(), 0);
        lv_obj_set_style_text_color(hint, theme::color_text_disabled(), 0);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, LV_PCT(100));
    }

    snprintf(b, sizeof(b), "%u min", (unsigned)p.fade_in_min);
    st->lbl_pwm_fin = make_stepper_row(card, tr(LangKey::DEV_FADE_IN), b,
        st, Action::PwmFadeIn,  -1, Action::PwmFadeIn,  +1);

    snprintf(b, sizeof(b), "%u min", (unsigned)p.fade_out_min);
    st->lbl_pwm_fout = make_stepper_row(card, tr(LangKey::DEV_FADE_OUT), b,
        st, Action::PwmFadeOut, -1, Action::PwmFadeOut, +1);
}

static void build_rgb_card(State* st, lv_obj_t* parent,
                           aqua::devices::RgbDevice& g) {
    lv_obj_t* card = make_card(parent, tr(LangKey::DEV_RGB_SETTINGS));
    char b[16];

    // ---- Color Hi (main ON color) ------------------------------------------
    {
        lv_obj_t* sec = lv_label_create(card);
        lv_label_set_text(sec, tr(LangKey::DEV_COLOR_HI));
        lv_obj_set_style_text_font(sec, theme::font_body(), 0);
        lv_obj_set_style_text_color(sec, theme::color_accent(), 0);
    }

    // Colour swatch + preset buttons row.
    lv_obj_t* row_color = make_row(card);
    st->lbl_rgb_swatch = lv_obj_create(row_color);
    lv_obj_set_size(st->lbl_rgb_swatch, 56, 56);
    lv_obj_set_style_radius(st->lbl_rgb_swatch, theme::RADIUS_MD, 0);
    lv_obj_set_style_bg_opa(st->lbl_rgb_swatch, LV_OPA_COVER, 0);
    {
        aqua::devices::Rgb8 init_rgb = aqua::devices::hsv_to_rgb(g.color_hsv);
        lv_obj_set_style_bg_color(st->lbl_rgb_swatch,
            lv_color_make(init_rgb.r, init_rgb.g, init_rgb.b), 0);
    }
    lv_obj_set_style_border_color(st->lbl_rgb_swatch,
                                  theme::color_outline(), 0);
    lv_obj_set_style_border_width(st->lbl_rgb_swatch, 1, 0);
    lv_obj_clear_flag(st->lbl_rgb_swatch, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < (int)(sizeof(s_rgb_presets) /
                              sizeof(s_rgb_presets[0])); ++i) {
        const auto& pr = s_rgb_presets[i];
        lv_obj_t* btn = lv_btn_create(row_color);
        lv_obj_set_size(btn, 44, 44);
        lv_obj_set_style_radius(btn, theme::RADIUS_PILL, 0);
        lv_obj_set_style_bg_color(btn, pr.color, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, theme::color_outline(), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        bind_action(btn, st, Action::RgbPreset, i);
    }

    // ---- Custom colour: H / S / V sliders ----------------------------------
    // Lambda builds one labelled slider row.  Indicator color is set to a
    // placeholder; update_slider_colors() corrects all indicators at the end.
    auto make_hsv_slider = [&](const char* lbl_text, int channel, bool is_lo,
                                int max_val, int init_val_i) -> lv_obj_t* {
        lv_obj_t* row = make_row(card);
        lv_obj_set_style_pad_row(row, theme::PAD_SM, 0);

        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_label_set_text(lbl, lbl_text);
        lv_obj_set_style_min_width(lbl, 36, 0);

        lv_obj_t* sld = lv_slider_create(row);
        lv_slider_set_range(sld, 0, max_val);
        lv_slider_set_value(sld, init_val_i, LV_ANIM_OFF);
        lv_obj_set_height(sld, 20);
        lv_obj_set_flex_grow(sld, 1);
        lv_obj_set_style_bg_color(sld, theme::color_text_disabled(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sld, theme::color_text_disabled(), LV_PART_KNOB);
        auto* ctx = new RgbSliderCtx{st, channel, is_lo};
        lv_obj_add_event_cb(sld, on_rgb_slider, LV_EVENT_VALUE_CHANGED, ctx);
        lv_obj_add_event_cb(sld, [](lv_event_t* e){
            delete static_cast<RgbSliderCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, ctx);
        return sld;
    };

    // ---- Color Hi sliders --------------------------------------------------
    st->sld_rgb_h = make_hsv_slider(tr(LangKey::DEV_HUE), 0, false, 360,
                                    (int)g.color_hsv.h);
    st->sld_rgb_s = make_hsv_slider(tr(LangKey::DEV_SAT), 1, false, 100,
                                    (int)(g.color_hsv.s * 100.0f + 0.5f));
    st->sld_rgb_v = make_hsv_slider(tr(LangKey::DEV_VAL), 2, false, 100,
                                    (int)(g.color_hsv.v * 100.0f + 0.5f));

    // ---- Color Lo (analog low-end for TEMP_MAP) ----------------------------
    {
        lv_obj_t* sec = lv_label_create(card);
        lv_label_set_text(sec, tr(LangKey::DEV_COLOR_LO));
        lv_obj_set_style_text_font(sec, theme::font_body(), 0);
        lv_obj_set_style_text_color(sec, theme::color_text_secondary(), 0);
    }
    {
        lv_obj_t* row_lo = make_row(card);
        st->lbl_rgb_swatch_lo = lv_obj_create(row_lo);
        lv_obj_set_size(st->lbl_rgb_swatch_lo, 40, 40);
        lv_obj_set_style_radius(st->lbl_rgb_swatch_lo, theme::RADIUS_MD, 0);
        lv_obj_set_style_bg_opa(st->lbl_rgb_swatch_lo, LV_OPA_COVER, 0);
        {
            aqua::devices::Rgb8 lo_rgb = aqua::devices::hsv_to_rgb(g.color_lo_hsv);
            lv_obj_set_style_bg_color(st->lbl_rgb_swatch_lo,
                lv_color_make(lo_rgb.r, lo_rgb.g, lo_rgb.b), 0);
        }
        lv_obj_set_style_border_color(st->lbl_rgb_swatch_lo,
                                      theme::color_outline(), 0);
        lv_obj_set_style_border_width(st->lbl_rgb_swatch_lo, 1, 0);
        lv_obj_clear_flag(st->lbl_rgb_swatch_lo, LV_OBJ_FLAG_SCROLLABLE);
    }

    st->sld_rgb_h_lo = make_hsv_slider(tr(LangKey::DEV_HUE), 0, true, 360,
                                       (int)g.color_lo_hsv.h);
    st->sld_rgb_s_lo = make_hsv_slider(tr(LangKey::DEV_SAT), 1, true, 100,
                                       (int)(g.color_lo_hsv.s * 100.0f + 0.5f));
    st->sld_rgb_v_lo = make_hsv_slider(tr(LangKey::DEV_VAL), 2, true, 100,
                                       (int)(g.color_lo_hsv.v * 100.0f + 0.5f));

    // Set initial indicator colors for all sliders.
    update_slider_colors(st, &g);

    snprintf(b, sizeof(b), "%u min", (unsigned)g.fade_in_min);
    st->lbl_rgb_fin = make_stepper_row(card, tr(LangKey::DEV_FADE_IN), b,
        st, Action::RgbFadeIn,  -1, Action::RgbFadeIn,  +1);

    snprintf(b, sizeof(b), "%u min", (unsigned)g.fade_out_min);
    st->lbl_rgb_fout = make_stepper_row(card, tr(LangKey::DEV_FADE_OUT), b,
        st, Action::RgbFadeOut, -1, Action::RgbFadeOut, +1);
}

static void build_relay_card(State* st, lv_obj_t* parent,
                             aqua::devices::RelayDevice& r) {
    lv_obj_t* card = make_card(parent, tr(LangKey::DEV_RELAY_SETTINGS));

    lv_obj_t* row = make_row(card);

    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
    lv_label_set_text(lbl, r.active_high
        ? tr(LangKey::DEV_ACTIVE_HIGH) : tr(LangKey::DEV_ACTIVE_LOW));
    st->lbl_relay_polarity = lbl;

    lv_obj_t* btn = make_pill(row, tr(LangKey::DEV_TOGGLE), 150, theme::TOUCH_MIN,
                              st, Action::RelayTogglePolarity);
    (void)btn;
}

// ---------------------------------------------------------------------------
// Device name editing card — with inline keyboard and EN/RU layout switch.
// ---------------------------------------------------------------------------

// Cyrillic keyboard map for lv_keyboard USER_1 mode.
static const char* s_kb_ru_lc[] = {
    "й","ц","у","к","е","н","г","ш","щ","з","х","\n",
    "ф","ы","в","а","п","р","о","л","д","ж","э","\n",
    "я","ч","с","м","и","т","ь","б","ю",".","\n",
    LV_SYMBOL_BACKSPACE," ",LV_SYMBOL_OK,""
};
static const lv_buttonmatrix_ctrl_t s_kb_ru_ctrl[] = {
    (lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,
    (lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,
    (lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,(lv_buttonmatrix_ctrl_t)1,
    LV_BUTTONMATRIX_CTRL_NONE,(lv_buttonmatrix_ctrl_t)4,LV_BUTTONMATRIX_CTRL_NONE
};

static void name_kbd_hide(State* st) {
    if (!st || !st->kbd_name) return;
    // Restore scroll bottom padding that was added when keyboard appeared.
    if (st->ta_name) {
        lv_obj_t* scroll_p = lv_obj_get_parent(lv_obj_get_parent(st->ta_name));
        if (scroll_p) lv_obj_set_style_pad_bottom(scroll_p, theme::PAD_LG, 0);
    }
    lv_obj_del(st->kbd_name);
    st->kbd_name = nullptr;
}

static void on_name_kbd_event(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (code == LV_EVENT_READY && st->ta_name) {
            const char* txt = lv_textarea_get_text(st->ta_name);
            auto* dev = lookup(st->device_id);
            if (dev && txt && strlen(txt) > 0) {
                dev->name = txt;
                schedule_save(st);
            }
        }
        name_kbd_hide(st);
    }
}

static void on_name_lang_btn(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st || !st->kbd_name) return;
    st->kbd_ru = !st->kbd_ru;
    // Find the keyboard widget (first lv_keyboard child of container)
    lv_obj_t* cont = st->kbd_name;
    lv_obj_t* kbd = nullptr;
    uint32_t cnt = lv_obj_get_child_cnt(cont);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* ch = lv_obj_get_child(cont, (int32_t)i);
        if (lv_obj_check_type(ch, &lv_keyboard_class)) { kbd = ch; break; }
    }
    if (!kbd) return;
    if (st->kbd_ru) {
        lv_keyboard_set_mode(kbd, LV_KEYBOARD_MODE_USER_1);
    } else {
        lv_keyboard_set_mode(kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
    }
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    if (lbl) lv_label_set_text(lbl, st->kbd_ru ? "EN" : "RU");
}

static void on_ta_name_focused(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st || st->kbd_name) return;

    // Full-screen container on lv_layer_top():
    //   — transparent background lets content show through
    //   — covers entire screen so tapping above the keyboard dismisses it
    lv_obj_t* cont = lv_obj_create(lv_layer_top());
    lv_obj_set_size(cont, 800, 480);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    // Tap anywhere outside the keyboard/lang-btn children → dismiss
    lv_obj_add_event_cb(cont, [](lv_event_t* ev) {
        auto* s = static_cast<State*>(lv_event_get_user_data(ev));
        if (s) name_kbd_hide(s);
    }, LV_EVENT_CLICKED, st);

    // EN/RU toggle button — sits in the 260px gap above the 220px keyboard
    lv_obj_t* lang_btn = lv_btn_create(cont);
    lv_obj_set_size(lang_btn, 64, 32);
    lv_obj_align(lang_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -224);  // just above keyboard
    style_pill(lang_btn);
    lv_obj_t* lang_lbl = lv_label_create(lang_btn);
    lv_label_set_text(lang_lbl, "RU");
    lv_obj_set_style_text_font(lang_lbl, theme::font_caption(), 0);
    lv_obj_center(lang_lbl);
    lv_obj_add_event_cb(lang_btn, on_name_lang_btn, LV_EVENT_CLICKED, st);

    lv_obj_t* kbd = lv_keyboard_create(cont);
    lv_obj_set_size(kbd, 800, 220);
    lv_obj_align(kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(kbd, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(kbd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kbd, 1, 0);
    lv_obj_set_style_border_color(kbd, theme::color_outline(), 0);
    lv_obj_set_style_bg_color(kbd, theme::color_surface_alt(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kbd, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kbd, theme::color_text_primary(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kbd, theme::font_body(), LV_PART_ITEMS);
    lv_obj_set_style_border_color(kbd, theme::color_outline(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kbd, 1, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kbd, theme::color_accent(),
                               LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kbd, theme::color_background(),
                                LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_keyboard_set_map(kbd, LV_KEYBOARD_MODE_USER_1, s_kb_ru_lc, s_kb_ru_ctrl);
    lv_keyboard_set_textarea(kbd, st->ta_name);
    lv_keyboard_set_mode(kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(kbd, on_name_kbd_event, LV_EVENT_READY,  st);
    lv_obj_add_event_cb(kbd, on_name_kbd_event, LV_EVENT_CANCEL, st);
    st->kbd_name = cont;

    // Scroll ta_name above the 220px keyboard: add bottom padding to the
    // scroll container (ta_name → card → scroll) then bring ta into view.
    if (st->ta_name) {
        lv_obj_t* scroll_p = lv_obj_get_parent(lv_obj_get_parent(st->ta_name));
        if (scroll_p) {
            lv_obj_set_style_pad_bottom(scroll_p, 220 + theme::PAD_LG, 0);
            lv_obj_scroll_to_view(st->ta_name, LV_ANIM_ON);
        }
    }
}

static void build_name_card(State* st, lv_obj_t* parent,
                            aqua::devices::IDevice& dev) {
    lv_obj_t* card = make_card(parent, tr(LangKey::DEV_NAME_SETTINGS));
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ta = lv_textarea_create(card);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 32);
    lv_textarea_set_text(ta, dev.name.c_str());
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, theme::TOUCH_MIN);
    lv_obj_set_style_text_font(ta, theme::font_body(), 0);
    lv_obj_set_style_bg_color(ta, theme::color_surface_alt(), 0);
    lv_obj_set_style_border_color(ta, theme::color_outline(), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_text_color(ta, theme::color_text_primary(), 0);
    lv_obj_add_event_cb(ta, on_ta_name_focused, LV_EVENT_FOCUSED, st);
    st->ta_name = ta;

    lv_obj_t* hint = lv_label_create(card);
    lv_label_set_text(hint, tr(LangKey::DEV_NAME_HINT));
    lv_obj_set_style_text_font(hint, theme::font_caption(), 0);
    lv_obj_set_style_text_color(hint, theme::color_text_secondary(), 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
}


// LVGL dropdown options string for DeviceRole enum (newline-separated, 0-indexed).
static constexpr const char* kRoleOptions =
    "Generic\n"
    "Heater\n"
    "Pump\n"
    "Light\n"
    "Fan\n"
    "CO2\n"
    "Doser\n"
    "UV";

static void on_role_changed(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st) return;
    auto* dev = lookup(st->device_id);
    if (!dev) return;
    lv_obj_t* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uint16_t sel = lv_dropdown_get_selected(dd);
    dev->role = static_cast<aqua::devices::DeviceRole>(sel);
    schedule_save(st);
}

static void build_role_card(State* st, lv_obj_t* parent,
                            aqua::devices::IDevice& dev) {
    lv_obj_t* card = make_card(parent, tr(LangKey::DEV_ROLE_SETTINGS));
    lv_obj_t* row  = make_row(card);

    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
    lv_label_set_text(lbl, tr(LangKey::DEV_ROLE_LABEL));

    lv_obj_t* dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, kRoleOptions);
    lv_dropdown_set_selected(dd, (uint16_t)dev.role);
    lv_obj_set_width(dd, 180);
    lv_obj_set_height(dd, theme::TOUCH_MIN);
    lv_obj_set_style_text_font(dd, theme::font_body(), 0);
    lv_obj_set_style_text_color(dd, theme::color_text_primary(), 0);
    lv_obj_set_style_bg_color(dd, theme::color_surface_alt(), 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(dd), theme::font_body(), 0);
    lv_obj_set_style_text_color(lv_dropdown_get_list(dd), theme::color_text_primary(), 0);
    lv_obj_set_style_bg_color(lv_dropdown_get_list(dd), theme::color_surface(), 0);
    lv_obj_add_event_cb(dd, on_role_changed, LV_EVENT_VALUE_CHANGED, st);
}

static void build_danger_card(State* st, lv_obj_t* parent) {
    lv_obj_t* card = make_card(parent, nullptr);
    lv_obj_t* btn = make_pill(card,
        LV_SYMBOL_TRASH "  Delete device", 260, theme::TOUCH_MIN,
        st, Action::DeleteRequest);
    lv_obj_set_style_bg_color(btn, theme::color_error(), 0);
    lv_obj_set_style_border_color(btn, theme::color_error(), 0);
    lv_obj_set_style_text_color(btn, theme::color_text_primary(), 0);
}

}  // namespace

lv_obj_t* build(uint8_t device_id) {
    auto* dev = lookup(device_id);
    if (!dev) {
        AC_LOGW(TAG, "build: device %u not found", (unsigned)device_id);
        return nullptr;
    }

    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    auto* st = new State();
    st->device_id      = device_id;
    st->trigger_linked = device_has_triggers(device_id);
    st->target_active  = st->trigger_linked ? !dev->current_active()
                                             :  dev->current_active();
    lv_obj_set_user_data(root, st);
    lv_obj_add_event_cb(root, on_root_delete, LV_EVENT_DELETE, nullptr);

    chrome::build(root, dev->name.c_str(), chrome::pop_on_back);

    // Scroll container.
    lv_obj_t* scroll = lv_obj_create(root);
    lv_obj_set_size(scroll, 800, 480 - chrome::kHeaderH);
    lv_obj_set_pos(scroll, 0, chrome::kHeaderH);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, theme::PAD_LG, 0);
    lv_obj_set_style_pad_row(scroll, theme::PAD_MD, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);

    build_state_card(st, scroll);
    build_control_card(st, scroll);

    if (dev->get_type() == aqua::devices::DeviceType::PWM) {
        build_pwm_card(st, scroll,
                       *static_cast<aqua::devices::PwmDevice*>(dev));
    } else if (dev->get_type() == aqua::devices::DeviceType::RGB) {
        build_rgb_card(st, scroll,
                       *static_cast<aqua::devices::RgbDevice*>(dev));
    } else if (dev->get_type() == aqua::devices::DeviceType::RELAY) {
        build_relay_card(st, scroll,
                         *static_cast<aqua::devices::RelayDevice*>(dev));
    }

    build_role_card(st, scroll, *dev);
    build_name_card(st, scroll, *dev);
    build_danger_card(st, scroll);

    refresh(st);
    st->refresh_timer = lv_timer_create(refresh_cb, 1000, st);
    return root;
}

}  // namespace aqua::ui::device_detail_screen
