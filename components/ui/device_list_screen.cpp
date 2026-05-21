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

constexpr int16_t kRowH    = 84;
constexpr int16_t kRowGap  = 10;
constexpr int16_t kListPad = theme::PAD_LG;

struct RowCtx { uint8_t device_id; };

static const char* type_label(aqua::devices::DeviceType t) {
    using T = aqua::devices::DeviceType;
    switch (t) {
        case T::RELAY: return tr(LangKey::DEV_RELAY);
        case T::PWM:   return tr(LangKey::DEV_PWM);
        case T::RGB:   return tr(LangKey::DEV_RGB);
    }
    return "?";
}

static const char* type_icon(aqua::devices::DeviceType t) {
    using T = aqua::devices::DeviceType;
    switch (t) {
        case T::RELAY: return LV_SYMBOL_POWER;
        case T::PWM:   return LV_SYMBOL_CHARGE;
        case T::RGB:   return LV_SYMBOL_TINT;
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
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), kRowH);
    lv_obj_set_style_bg_color(row, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, theme::color_outline(), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_70, 0);
    lv_obj_set_style_radius(row, theme::RADIUS_LG, 0);
    lv_obj_set_style_pad_all(row, theme::PAD_MD, 0);
    lv_obj_set_style_bg_color(row, theme::color_surface_alt(),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, theme::color_accent(),
                                  LV_STATE_PRESSED);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    // Icon (left)
    lv_obj_t* icon = lv_label_create(row);
    lv_label_set_text(icon, type_icon(dev.get_type()));
    lv_obj_set_style_text_font(icon, theme::font_title(), 0);
    lv_obj_set_style_text_color(icon, theme::color_accent(), 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

    // Name
    lv_obj_t* name = lv_label_create(row);
    lv_label_set_text(name, dev.name.c_str());
    lv_obj_set_style_text_font(name, theme::font_body(), 0);
    lv_obj_set_style_text_color(name, theme::color_text_primary(), 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 420);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 56, -10);

    // Type + channel sub-label
    char sub[128];
    uint8_t ch = 0;
    if (dev.get_type() == aqua::devices::DeviceType::RELAY) {
        ch = static_cast<aqua::devices::RelayDevice&>(dev).channel();
    } else if (dev.get_type() == aqua::devices::DeviceType::PWM) {
        ch = static_cast<aqua::devices::PwmDevice&>(dev).channel();
    } else if (dev.get_type() == aqua::devices::DeviceType::RGB) {
        ch = static_cast<aqua::devices::RgbDevice&>(dev).base_channel();
    }
    snprintf(sub, sizeof(sub), "%s  ch.%u  id.%u",
             type_label(dev.get_type()), (unsigned)ch, (unsigned)dev.id);

    // Type-specific detail appended to the sub-label.
    if (dev.get_type() == aqua::devices::DeviceType::PWM) {
        auto& p = static_cast<aqua::devices::PwmDevice&>(dev);
        char tmp[40];
        snprintf(tmp, sizeof(tmp), "  |  level %u%%  fade %u/%u min",
                 (unsigned)p.level_pct, (unsigned)p.fade_in_min, (unsigned)p.fade_out_min);
        strncat(sub, tmp, sizeof(sub) - strlen(sub) - 1);
        auto fs = p.fade_status();
        if (fs == aqua::devices::PwmDevice::FadeStatus::FADING_IN) {
            strncat(sub, "  [", sizeof(sub) - strlen(sub) - 1);
            strncat(sub, tr(LangKey::DEV_FADING_IN), sizeof(sub) - strlen(sub) - 1);
            strncat(sub, "]", sizeof(sub) - strlen(sub) - 1);
        } else if (fs == aqua::devices::PwmDevice::FadeStatus::FADING_OUT) {
            strncat(sub, "  [", sizeof(sub) - strlen(sub) - 1);
            strncat(sub, tr(LangKey::DEV_FADING_OUT), sizeof(sub) - strlen(sub) - 1);
            strncat(sub, "]", sizeof(sub) - strlen(sub) - 1);
        }
    } else if (dev.get_type() == aqua::devices::DeviceType::RGB) {
        auto& r = static_cast<aqua::devices::RgbDevice&>(dev);
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "  |  #%02X%02X%02X  bright %u%%",
                 r.color.r, r.color.g, r.color.b, (unsigned)r.brightness_pct);
        strncat(sub, tmp, sizeof(sub) - strlen(sub) - 1);
    } else if (dev.get_type() == aqua::devices::DeviceType::RELAY) {
        auto& rel = static_cast<aqua::devices::RelayDevice&>(dev);
        strncat(sub, rel.active_high ? "  |  " : "  |  ", sizeof(sub) - strlen(sub) - 1);
        strncat(sub, rel.active_high ? tr(LangKey::DEV_ACTIVE_HIGH)
                                     : tr(LangKey::DEV_ACTIVE_LOW),
                sizeof(sub) - strlen(sub) - 1);
    }
    lv_obj_t* sublbl = lv_label_create(row);
    lv_label_set_text(sublbl, sub);
    lv_obj_set_style_text_font(sublbl, theme::font_caption(), 0);
    lv_obj_set_style_text_color(sublbl, theme::color_text_secondary(), 0);
    lv_obj_align(sublbl, LV_ALIGN_LEFT_MID, 56, 14);

    // State badge / pill (right) — styled label with background
    const bool dev_on = dev.current_active();
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
    lv_obj_set_style_pad_ver(state, 4, 0);
    lv_obj_align(state, LV_ALIGN_RIGHT_MID, -(theme::PAD_LG + theme::PAD_SM), 0);

    // Chevron
    lv_obj_t* chev = lv_label_create(row);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(chev, theme::font_body(), 0);
    lv_obj_set_style_text_color(chev, theme::color_text_secondary(), 0);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -theme::PAD_XS, 0);

    auto* ctx = new RowCtx{dev.id};
    lv_obj_set_user_data(row, ctx);  // so refresh can find the device id
    lv_obj_add_event_cb(row, on_row_clicked, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(row, on_row_delete, LV_EVENT_DELETE, ctx);

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

// Lightweight 1Hz pass: walk existing rows and refresh the ON/OFF pill
// (keyed by device id stored in the per-row user_data).
static void refresh_states(lv_obj_t* root) {
    auto* ls = list_state_of(root);
    if (!ls || !ls->list_container) return;
    const auto& ctx = ui_context();
    if (!ctx.devices) return;
    uint32_t n = lv_obj_get_child_cnt(ls->list_container);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* row = lv_obj_get_child(ls->list_container, i);
        if (!row) continue;
        auto* rc = static_cast<RowCtx*>(lv_obj_get_user_data(row));
        if (!rc) continue;
        auto* dev = ctx.devices->find(rc->device_id);
        if (!dev) continue;
        // Row layout: icon, name, sub, state, chevron.
        // State label is the second-to-last child.
        uint32_t cn = lv_obj_get_child_cnt(row);
        if (cn < 2) continue;
        lv_obj_t* state = lv_obj_get_child(row, cn - 2);
        if (!state) continue;
        const bool dev_active = dev->current_active();
        const char* want = dev_active ? tr(LangKey::DEV_ON) : tr(LangKey::DEV_OFF);
        const char* cur = lv_label_get_text(state);
        if (!cur || strcmp(cur, want) != 0) {
            lv_label_set_text(state, want);
        }
        lv_obj_set_style_text_color(state,
            dev_active ? theme::color_background() : theme::color_text_secondary(), 0);
        lv_obj_set_style_bg_color(state,
            dev_active ? theme::color_success() : theme::color_surface_alt(), 0);
        lv_obj_set_style_border_color(state,
            dev_active ? theme::color_success() : theme::color_outline(), 0);
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
