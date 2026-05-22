// AquaControl - Device list screen.
//
// Lists every configured IDevice with type, name, channel, and live state.
// Tapping a row pushes the device detail screen. "+ Add device" pill in
// the bottom-right launches the add-device flow.
#include "device_list_screen.h"

#include <cstdio>
#include <cstring>

#include "add_device_screen.h"
#include "chrome.h"
#include "device_detail_screen.h"
#include "device_manager.h"
#include "device_types.h"
#include "pwm_device.h"
#include "relay_device.h"
#include "rgb_device.h"
#include "screen_manager.h"
#include "theme.h"
#include "i18n.h"
#include "ui_context.h"

namespace aqua::ui::device_list_screen {

namespace {

using i18n::tr;
using i18n::LangKey;

constexpr int16_t kRowH    = 88;   // card height (px)
constexpr int16_t kRowGap  = 10;
constexpr int16_t kListPad = theme::PAD_LG;
constexpr int16_t kBadgeW  = 54;   // icon-badge square side
constexpr int16_t kTextX   = kBadgeW + theme::PAD_SM;  // 54+8 = 62px from content left

// Per-row context holding refreshable LVGL objects so refresh_states does
// not need fragile child-index arithmetic.
struct RowCtx {
    uint8_t   device_id;
    aqua::devices::DeviceType type;
    lv_obj_t* badge      = nullptr;   // icon badge bg (color / opacity)
    lv_obj_t* icon_lbl   = nullptr;   // symbol inside badge
    lv_obj_t* state_pill = nullptr;   // ON / OFF pill
    lv_obj_t* swatch     = nullptr;   // RGB: color preview rect (nullptr otherwise)
    lv_obj_t* level_bar  = nullptr;   // PWM: level bar widget (nullptr otherwise)
};

static const char* type_label(aqua::devices::DeviceType t) {
    using T = aqua::devices::DeviceType;
    switch (t) {
        case T::RELAY: return tr(LangKey::DEV_RELAY);
        case T::PWM:   return tr(LangKey::DEV_PWM);
        case T::RGB:   return tr(LangKey::DEV_RGB);
    }
    return "?";
}

// Icons are consistent with add_device_screen.cpp.
// RGB uses LV_SYMBOL_IMAGE (picture) — distinct from LV_SYMBOL_TINT (water).
static const char* type_icon(aqua::devices::DeviceType t) {
    using T = aqua::devices::DeviceType;
    switch (t) {
        case T::RELAY: return LV_SYMBOL_POWER;    // power toggle
        case T::PWM:   return LV_SYMBOL_CHARGE;   // lightning / electric dimmer
        case T::RGB:   return LV_SYMBOL_IMAGE;    // color picture / light scene
    }
    return LV_SYMBOL_LIST;
}

static void on_row_clicked(lv_event_t* e) {
    auto* ctx = static_cast<RowCtx*>(lv_event_get_user_data(e));
    if (!ctx) return;
    if (lv_obj_t* s = device_detail_screen::build(ctx->device_id)) {
        screen_manager::push(s, screen_manager::Transition::SLIDE_LEFT);
    }
}

static void on_add_clicked(lv_event_t*) {
    if (lv_obj_t* s = add_device_screen::build()) {
        screen_manager::push(s, screen_manager::Transition::SLIDE_LEFT);
    }
}

static void on_row_delete(lv_event_t* e) {
    // Free the per-row context we allocated with `new`.
    auto* ctx = static_cast<RowCtx*>(lv_event_get_user_data(e));
    delete ctx;
}

static lv_obj_t* make_row(lv_obj_t* parent, aqua::devices::IDevice& dev) {
    using T = aqua::devices::DeviceType;
    const T    type   = dev.get_type();
    const bool dev_on = dev.current_active();

    // ── Card shell ─────────────────────────────────────────────────────────
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), kRowH);
    lv_obj_set_style_bg_color(row, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, theme::color_outline(), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_70, 0);
    lv_obj_set_style_radius(row, theme::RADIUS_LG, 0);
    lv_obj_set_style_pad_all(row, theme::PAD_MD, 0);
    lv_obj_set_style_bg_color(row, theme::color_surface_alt(), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, theme::color_accent(), LV_STATE_PRESSED);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    // ── Icon badge (left) ──────────────────────────────────────────────────
    // Background color encodes device type + live state at a glance:
    //   RGB   → actual current color (so the badge IS the color preview)
    //   PWM   → accent, opacity proportional to level (dark=off, bright=full)
    //   RELAY → success-green when ON; muted when OFF
    lv_color_t badge_color;
    lv_opa_t   badge_opa = LV_OPA_COVER;
    if (type == T::RGB) {
        auto& r = static_cast<aqua::devices::RgbDevice&>(dev);
        aqua::devices::Rgb8 c = aqua::devices::hsv_to_rgb(r.color_hsv);
        badge_color = lv_color_make(c.r, c.g, c.b);
    } else if (type == T::PWM) {
        auto& p = static_cast<aqua::devices::PwmDevice&>(dev);
        badge_color = theme::color_accent();
        badge_opa   = dev_on
            ? (lv_opa_t)(LV_OPA_30 + (uint16_t)LV_OPA_70 * p.level_pct / 100u)
            : LV_OPA_20;
    } else {  // RELAY
        badge_color = dev_on ? theme::color_success() : theme::color_surface_alt();
    }

    lv_obj_t* badge = lv_obj_create(row);
    lv_obj_set_size(badge, kBadgeW, kBadgeW);
    lv_obj_set_style_bg_color(badge, badge_color, 0);
    lv_obj_set_style_bg_opa(badge, badge_opa, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_radius(badge, theme::RADIUS_MD, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_align(badge, LV_ALIGN_LEFT_MID, 0, 0);

    // Icon symbol centered in badge.  On bright RGB colors use dark ink.
    lv_color_t icon_color = lv_color_white();
    if (type == T::RGB) {
        auto& r = static_cast<aqua::devices::RgbDevice&>(dev);
        if (r.color_hsv.v > 0.55f && r.color_hsv.s > 0.15f)
            icon_color = lv_color_make(20, 30, 40);
    } else if (!dev_on) {
        icon_color = theme::color_text_disabled();
    }
    lv_obj_t* icon = lv_label_create(badge);
    lv_label_set_text(icon, type_icon(type));
    lv_obj_set_style_text_font(icon, theme::font_title(), 0);
    lv_obj_set_style_text_color(icon, icon_color, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);

    // ── Device name ────────────────────────────────────────────────────────
    lv_obj_t* name = lv_label_create(row);
    lv_label_set_text(name, dev.name.c_str());
    lv_obj_set_style_text_font(name, theme::font_body(), 0);
    lv_obj_set_style_text_color(name, theme::color_text_primary(), 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 490);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, kTextX, -12);

    // ── Sub-label: type · channel · id · device-specific info ─────────────
    uint8_t ch = 0;
    if      (type == T::RELAY) ch = static_cast<aqua::devices::RelayDevice&>(dev).channel();
    else if (type == T::PWM)   ch = static_cast<aqua::devices::PwmDevice&>(dev).channel();
    else if (type == T::RGB)   ch = static_cast<aqua::devices::RgbDevice&>(dev).base_channel();

    // Middle-dot separator: UTF-8 · = 0xC2 0xB7
    static constexpr char kSep[] = "  \xc2\xb7  ";
    char sub[160];
    snprintf(sub, sizeof(sub), "%s%sch %u%sid %u",
             type_label(type), kSep, (unsigned)ch, kSep, (unsigned)dev.id);

    if (type == T::RELAY) {
        auto& rel = static_cast<aqua::devices::RelayDevice&>(dev);
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "%s%s", kSep,
                 rel.active_high ? tr(LangKey::DEV_ACTIVE_HIGH)
                                 : tr(LangKey::DEV_ACTIVE_LOW));
        strncat(sub, tmp, sizeof(sub) - strlen(sub) - 1);
    } else if (type == T::PWM) {
        auto& p = static_cast<aqua::devices::PwmDevice&>(dev);
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s%u%%", kSep, (unsigned)p.level_pct);
        strncat(sub, tmp, sizeof(sub) - strlen(sub) - 1);
        auto fs = p.fade_status();
        if (fs == aqua::devices::PwmDevice::FadeStatus::FADING_IN) {
            snprintf(tmp, sizeof(tmp), "%s%s", kSep, tr(LangKey::DEV_FADING_IN));
            strncat(sub, tmp, sizeof(sub) - strlen(sub) - 1);
        } else if (fs == aqua::devices::PwmDevice::FadeStatus::FADING_OUT) {
            snprintf(tmp, sizeof(tmp), "%s%s", kSep, tr(LangKey::DEV_FADING_OUT));
            strncat(sub, tmp, sizeof(sub) - strlen(sub) - 1);
        }
    } else if (type == T::RGB) {
        auto& r = static_cast<aqua::devices::RgbDevice&>(dev);
        aqua::devices::Rgb8 c = aqua::devices::hsv_to_rgb(r.color_hsv);
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%s#%02X%02X%02X",
                 kSep, (unsigned)c.r, (unsigned)c.g, (unsigned)c.b);
        strncat(sub, tmp, sizeof(sub) - strlen(sub) - 1);
    }

    lv_obj_t* sublbl = lv_label_create(row);
    lv_label_set_text(sublbl, sub);
    lv_obj_set_style_text_font(sublbl, theme::font_caption(), 0);
    lv_obj_set_style_text_color(sublbl, theme::color_text_secondary(), 0);
    lv_label_set_long_mode(sublbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sublbl, 490);
    lv_obj_align(sublbl, LV_ALIGN_LEFT_MID, kTextX, +12);

    // ── Right column: state pill + chevron (+ swatch for RGB) ─────────────
    // Layout (from RIGHT_MID):
    //   chevron   x = -4          y = 0  (always)
    //   pill      x = -52         y = -13 when swatch present, else 0
    //   swatch    x = -52         y = +14 (RGB only)

    const bool has_swatch = (type == T::RGB);
    const int16_t pill_y  = has_swatch ? -13 : 0;

    lv_obj_t* state = lv_label_create(row);
    lv_label_set_text(state, dev_on ? tr(LangKey::DEV_ON) : tr(LangKey::DEV_OFF));
    lv_obj_set_style_text_font(state, theme::font_caption(), 0);
    lv_obj_set_style_text_color(state,
        dev_on ? theme::color_background() : theme::color_text_secondary(), 0);
    lv_obj_set_style_bg_color(state,
        dev_on ? theme::color_success() : theme::color_surface_alt(), 0);
    lv_obj_set_style_bg_opa(state, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(state,
        dev_on ? theme::color_success() : theme::color_outline(), 0);
    lv_obj_set_style_border_width(state, 1, 0);
    lv_obj_set_style_radius(state, theme::RADIUS_PILL, 0);
    lv_obj_set_style_pad_hor(state, theme::PAD_SM, 0);
    lv_obj_set_style_pad_ver(state, 3, 0);
    lv_obj_align(state, LV_ALIGN_RIGHT_MID, -52, pill_y);

    lv_obj_t* chev = lv_label_create(row);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(chev, theme::font_body(), 0);
    lv_obj_set_style_text_color(chev, theme::color_text_disabled(), 0);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -theme::PAD_XS, 0);

    // ── RGB color swatch ───────────────────────────────────────────────────
    // A solid rectangle colored with the current Hi (ON) color.
    // Makes it possible to identify the light color at a glance without
    // squinting at the hex string in the sub-label.
    lv_obj_t* swatch = nullptr;
    if (type == T::RGB) {
        auto& r = static_cast<aqua::devices::RgbDevice&>(dev);
        aqua::devices::Rgb8 c = aqua::devices::hsv_to_rgb(r.color_hsv);
        swatch = lv_obj_create(row);
        lv_obj_set_size(swatch, 52, 16);
        lv_obj_set_style_bg_color(swatch, lv_color_make(c.r, c.g, c.b), 0);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(swatch, theme::color_outline(), 0);
        lv_obj_set_style_border_width(swatch, 1, 0);
        lv_obj_set_style_radius(swatch, theme::RADIUS_SM, 0);
        lv_obj_set_style_pad_all(swatch, 0, 0);
        lv_obj_clear_flag(swatch, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_align(swatch, LV_ALIGN_RIGHT_MID, -52, +14);
    }

    // ── PWM level bar ──────────────────────────────────────────────────────
    // Thin progress bar across the bottom of the card, showing current level.
    // Gives immediate visual feedback of brightness/intensity.
    lv_obj_t* level_bar = nullptr;
    if (type == T::PWM) {
        auto& p = static_cast<aqua::devices::PwmDevice&>(dev);
        level_bar = lv_bar_create(row);
        lv_obj_set_size(level_bar, LV_PCT(100), 5);
        lv_bar_set_range(level_bar, 0, 100);
        lv_bar_set_value(level_bar, (int16_t)p.level_pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(level_bar, theme::color_surface_alt(), LV_PART_MAIN);
        lv_obj_set_style_border_width(level_bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(level_bar, 3, LV_PART_MAIN);
        lv_obj_set_style_pad_all(level_bar, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(level_bar, theme::color_accent(), LV_PART_INDICATOR);
        lv_obj_set_style_radius(level_bar, 3, LV_PART_INDICATOR);
        lv_obj_clear_flag(level_bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(level_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    // ── Click / lifecycle ──────────────────────────────────────────────────
    auto* ctx = new RowCtx{dev.id, type, badge, icon, state, swatch, level_bar};
    lv_obj_set_user_data(row, ctx);
    lv_obj_add_event_cb(row, on_row_clicked, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(row, on_row_delete,  LV_EVENT_DELETE,  ctx);

    return row;
}

struct ListState {
    lv_obj_t* list_container = nullptr;  // scrollable rows container
    lv_obj_t* empty_label    = nullptr;  // empty-state placeholder
    lv_timer_t* refresh_timer = nullptr; // 1Hz state refresh
};

static ListState* list_state_of(lv_obj_t* root) {
    return root ? static_cast<ListState*>(lv_obj_get_user_data(root))
                : nullptr;
}

// Wipe rows then rebuild from current DeviceManager contents.
static void rebuild_rows(lv_obj_t* root) {
    auto* ls = list_state_of(root);
    if (!ls || !ls->list_container) return;
    lv_obj_clean(ls->list_container);

    const auto& ctx = ui_context();
    const bool empty = !ctx.devices || ctx.devices->size() == 0;
    if (ls->empty_label) {
        if (empty) lv_obj_clear_flag(ls->empty_label, LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_add_flag (ls->empty_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (empty) return;
    for (const auto& up : ctx.devices->all()) {
        if (up) make_row(ls->list_container, *up);
    }
}

// Lightweight 1Hz pass: update only the dynamic elements stored in RowCtx.
// Gated on actual value changes to avoid spurious LVGL invalidations
// (per the update-cost rule in the UI design system).
static void refresh_states(lv_obj_t* root) {
    auto* ls = list_state_of(root);
    if (!ls || !ls->list_container) return;
    const auto& ctx = ui_context();
    if (!ctx.devices) return;

    using T = aqua::devices::DeviceType;

    uint32_t n = lv_obj_get_child_cnt(ls->list_container);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* row = lv_obj_get_child(ls->list_container, i);
        if (!row) continue;
        auto* rc = static_cast<RowCtx*>(lv_obj_get_user_data(row));
        if (!rc) continue;
        auto* dev = ctx.devices->find(rc->device_id);
        if (!dev) continue;

        const bool dev_active = dev->current_active();

        // ── State pill ─────────────────────────────────────────────────────
        if (rc->state_pill) {
            const char* want = dev_active ? tr(LangKey::DEV_ON) : tr(LangKey::DEV_OFF);
            if (strcmp(lv_label_get_text(rc->state_pill), want) != 0)
                lv_label_set_text(rc->state_pill, want);
            lv_obj_set_style_text_color(rc->state_pill,
                dev_active ? theme::color_background() : theme::color_text_secondary(), 0);
            lv_obj_set_style_bg_color(rc->state_pill,
                dev_active ? theme::color_success() : theme::color_surface_alt(), 0);
            lv_obj_set_style_border_color(rc->state_pill,
                dev_active ? theme::color_success() : theme::color_outline(), 0);
        }

        // ── Badge color / opacity (reflects live state) ────────────────────
        if (rc->badge) {
            if (rc->type == T::RGB) {
                auto& r = static_cast<aqua::devices::RgbDevice&>(*dev);
                aqua::devices::Rgb8 c = aqua::devices::hsv_to_rgb(r.color_hsv);
                lv_obj_set_style_bg_color(rc->badge, lv_color_make(c.r, c.g, c.b), 0);
                // Adaptive icon ink
                if (rc->icon_lbl) {
                    lv_color_t ink = (r.color_hsv.v > 0.55f && r.color_hsv.s > 0.15f)
                        ? lv_color_make(20, 30, 40) : lv_color_white();
                    lv_obj_set_style_text_color(rc->icon_lbl, ink, 0);
                }
            } else if (rc->type == T::PWM) {
                auto& p = static_cast<aqua::devices::PwmDevice&>(*dev);
                lv_opa_t opa = dev_active
                    ? (lv_opa_t)(LV_OPA_30 + (uint16_t)LV_OPA_70 * p.level_pct / 100u)
                    : LV_OPA_20;
                lv_obj_set_style_bg_opa(rc->badge, opa, 0);
                if (rc->icon_lbl) {
                    lv_obj_set_style_text_color(rc->icon_lbl,
                        dev_active ? lv_color_white() : theme::color_text_disabled(), 0);
                }
            } else {  // RELAY
                lv_obj_set_style_bg_color(rc->badge,
                    dev_active ? theme::color_success() : theme::color_surface_alt(), 0);
                if (rc->icon_lbl) {
                    lv_obj_set_style_text_color(rc->icon_lbl,
                        dev_active ? lv_color_white() : theme::color_text_disabled(), 0);
                }
            }
        }

        // ── RGB color swatch ───────────────────────────────────────────────
        if (rc->swatch && rc->type == T::RGB) {
            auto& r = static_cast<aqua::devices::RgbDevice&>(*dev);
            aqua::devices::Rgb8 c = aqua::devices::hsv_to_rgb(r.color_hsv);
            lv_obj_set_style_bg_color(rc->swatch, lv_color_make(c.r, c.g, c.b), 0);
        }

        // ── PWM level bar ──────────────────────────────────────────────────
        if (rc->level_bar && rc->type == T::PWM) {
            auto& p = static_cast<aqua::devices::PwmDevice&>(*dev);
            if (lv_bar_get_value(rc->level_bar) != (int16_t)p.level_pct)
                lv_bar_set_value(rc->level_bar, (int16_t)p.level_pct, LV_ANIM_OFF);
        }
    }
}

static void on_screen_loaded(lv_event_t* e) {
    auto* root = static_cast<lv_obj_t*>(lv_event_get_target(e));
    rebuild_rows(root);
}

static void on_refresh_timer(lv_timer_t* t) {
    auto* root = static_cast<lv_obj_t*>(lv_timer_get_user_data(t));
    refresh_states(root);
}

static void on_screen_delete(lv_event_t* e) {
    auto* root = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* ls = list_state_of(root);
    if (!ls) return;
    if (ls->refresh_timer) {
        lv_timer_del(ls->refresh_timer);
        ls->refresh_timer = nullptr;
    }
    delete ls;
    lv_obj_set_user_data(root, nullptr);
}

}  // namespace

lv_obj_t* build() {
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    chrome::build(root, tr(LangKey::NAV_DEVICES), chrome::pop_on_back,
                  LV_SYMBOL_PLUS "  Add", on_add_clicked);

    auto* ls = new ListState();
    lv_obj_set_user_data(root, ls);
    lv_obj_add_event_cb(root, on_screen_delete, LV_EVENT_DELETE, nullptr);
    // Rebuild the list every time this screen becomes active so that
    // adding/removing devices on child screens is reflected on return.
    lv_obj_add_event_cb(root, on_screen_loaded, LV_EVENT_SCREEN_LOADED,
                        nullptr);

    // Empty-state placeholder (visibility toggled by rebuild_rows).
    ls->empty_label = lv_label_create(root);
    lv_label_set_text(ls->empty_label,
        "No devices yet.\n\nTap \"" LV_SYMBOL_PLUS
        "  Add\" to create your first device.");
    lv_obj_set_style_text_font(ls->empty_label, theme::font_body(), 0);
    lv_obj_set_style_text_color(ls->empty_label,
                                theme::color_text_secondary(), 0);
    lv_obj_set_style_text_align(ls->empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ls->empty_label, 600);
    lv_label_set_long_mode(ls->empty_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(ls->empty_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(ls->empty_label, LV_OBJ_FLAG_HIDDEN);

    // Scroll list container.
    ls->list_container = lv_obj_create(root);
    lv_obj_remove_style_all(ls->list_container);
    lv_obj_set_size(ls->list_container, 800, 480 - chrome::kHeaderH);
    lv_obj_set_pos(ls->list_container, 0, chrome::kHeaderH);
    lv_obj_set_style_pad_all(ls->list_container, kListPad, 0);
    lv_obj_set_style_pad_row(ls->list_container, kRowGap, 0);
    lv_obj_set_flex_flow(ls->list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ls->list_container, LV_DIR_VER);

    // Initial population (the SCREEN_LOADED event also fires later, so this
    // is just for the very first paint before LVGL dispatches it).
    rebuild_rows(root);

    // 1Hz lightweight refresh of ON/OFF pills (no full rebuild).
    ls->refresh_timer = lv_timer_create(on_refresh_timer, 1000, root);
    return root;
}

}  // namespace aqua::ui::device_list_screen
