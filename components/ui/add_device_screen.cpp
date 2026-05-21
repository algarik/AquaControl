// AquaControl - Add device screen.
//
// Three-step inline flow on a single screen:
//   1. Pick a type (Relay / PWM / RGB) via large pills.
//   2. Pick a hardware channel. Range depends on type (Relay 0..15,
//      PWM 0..15, RGB 0..13 [needs 3 consecutive]).
//   3. Tap Save in the chrome bar. We auto-assign id, generate a default
//      name like "Relay 3", persist to NVS, then pop back to the list.
//
// Cancel = chrome back arrow.
#include "add_device_screen.h"

#include <cstdio>
#include <memory>

#include "ac_logger.h"
#include "chrome.h"
#include "config_storage.h"
#include "device_manager.h"
#include "device_types.h"
#include "pwm_device.h"
#include "relay_device.h"
#include "rgb_device.h"
#include "faults.h"
#include "scheduler.h"
#include "screen_manager.h"
#include "theme.h"
#include "i18n.h"
#include "ui_context.h"

namespace aqua::ui::add_device_screen {

namespace {

constexpr const char* TAG = "AddDevice";

using aqua::devices::DeviceType;
using aqua::ui::i18n::LangKey;
using aqua::ui::i18n::tr;

struct State {
    DeviceType    type        = DeviceType::RELAY;
    int           channel     = 0;
    lv_obj_t*     btn_relay   = nullptr;
    lv_obj_t*     btn_pwm     = nullptr;
    lv_obj_t*     btn_rgb     = nullptr;
    lv_obj_t*     lbl_channel = nullptr;
    lv_obj_t*     lbl_hint    = nullptr;
    lv_obj_t*     lbl_summary = nullptr;
};

static State* s_state = nullptr;

static int channel_max() {
    switch (s_state->type) {
        case DeviceType::RELAY: return 15;  // PCF8575: 0..15
        case DeviceType::PWM:   return 15;  // PCA9685: 0..15
        case DeviceType::RGB:   return 13;  // needs 3 consecutive
    }
    return 15;
}

static const char* type_word(DeviceType t) {
    switch (t) {
        case DeviceType::RELAY: return "Relay";
        case DeviceType::PWM:   return "PWM";
        case DeviceType::RGB:   return "RGB";
    }
    return "?";
}

static void refresh_summary() {
    if (!s_state) return;
    char buf[96];
    if (s_state->type == DeviceType::RGB) {
        snprintf(buf, sizeof(buf),
                 "%s on channels %d-%d",
                 type_word(s_state->type),
                 s_state->channel, s_state->channel + 2);
    } else {
        snprintf(buf, sizeof(buf), "%s on channel %d",
                 type_word(s_state->type), s_state->channel);
    }
    lv_label_set_text(s_state->lbl_summary, buf);

    // Calculate available channels for current type.
    const auto& uic = ui_context();
    int slots_used = 0;
    if (uic.devices) {
        for (const auto& d : uic.devices->all()) {
            if (!d) continue;
            bool relay_type = (d->get_type() == DeviceType::RELAY);
            bool pwm_type   = (d->get_type() == DeviceType::PWM ||
                                d->get_type() == DeviceType::RGB);
            if (s_state->type == DeviceType::RELAY && relay_type) ++slots_used;
            if ((s_state->type == DeviceType::PWM || s_state->type == DeviceType::RGB) && pwm_type) {
                slots_used += (d->get_type() == DeviceType::RGB) ? 3 : 1;
            }
        }
    }
    int slots_free = 16 - slots_used;

    char hint[192];
    if (s_state->type == DeviceType::RGB) {
        snprintf(hint, sizeof(hint),
                 "RGB devices use three consecutive PCA9685 channels (R, G, B). Pick the first one.\n"
                 "PCA9685 slots used: %d/16 (%d free)",
                 slots_used, slots_free);
    } else if (s_state->type == DeviceType::PWM) {
        snprintf(hint, sizeof(hint),
                 "PWM devices map to one PCA9685 channel (0..15).\n"
                 "PCA9685 slots used: %d/16 (%d free)",
                 slots_used, slots_free);
    } else {
        snprintf(hint, sizeof(hint),
                 "Relays map to one PCF8575 output (0..15).\n"
                 "PCF8575 slots used: %d/16 (%d free)",
                 slots_used, slots_free);
    }
    lv_label_set_text(s_state->lbl_hint, hint);
    lv_obj_set_style_text_color(s_state->lbl_hint,
        slots_free <= 2 ? theme::color_warning() : theme::color_text_secondary(), 0);

    char ch_buf[16];
    snprintf(ch_buf, sizeof(ch_buf), "%d", s_state->channel);
    lv_label_set_text(s_state->lbl_channel, ch_buf);

    // Clamp channel into the new range and update visual state.
    if (s_state->channel > channel_max()) {
        s_state->channel = channel_max();
        snprintf(ch_buf, sizeof(ch_buf), "%d", s_state->channel);
        lv_label_set_text(s_state->lbl_channel, ch_buf);
    }
}

static void style_type_pill(lv_obj_t* btn, bool selected) {
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

static void refresh_type_pills() {
    style_type_pill(s_state->btn_relay,
                    s_state->type == DeviceType::RELAY);
    style_type_pill(s_state->btn_pwm,
                    s_state->type == DeviceType::PWM);
    style_type_pill(s_state->btn_rgb,
                    s_state->type == DeviceType::RGB);
}

static void on_type_relay(lv_event_t*) {
    s_state->type = DeviceType::RELAY;
    refresh_type_pills();
    refresh_summary();
}
static void on_type_pwm(lv_event_t*) {
    s_state->type = DeviceType::PWM;
    refresh_type_pills();
    refresh_summary();
}
static void on_type_rgb(lv_event_t*) {
    s_state->type = DeviceType::RGB;
    refresh_type_pills();
    refresh_summary();
}

static void on_channel_minus(lv_event_t*) {
    if (s_state->channel > 0) {
        --s_state->channel;
        refresh_summary();
    }
}
static void on_channel_plus(lv_event_t*) {
    if (s_state->channel < channel_max()) {
        ++s_state->channel;
        refresh_summary();
    }
}

static void on_save(lv_event_t*) {
    if (!s_state) return;
    const auto& uic = ui_context();
    if (!uic.devices) {
        AC_LOGE(TAG, "no device manager");
        return;
    }

    // Hardware-channel uniqueness check: prevent the user creating two
    // devices that drive the same PCF8575 output or the same PCA9685
    // channel (and for RGB, any of its three contiguous channels).
    if (!uic.devices->is_channel_free(s_state->type,
                                       (uint8_t)s_state->channel)) {
        char msg[128];
        if (s_state->type == DeviceType::RGB) {
            snprintf(msg, sizeof(msg),
                     tr(LangKey::ADD_DEV_CONFLICT_RANGE),
                     s_state->channel, s_state->channel + 2);
        } else {
            snprintf(msg, sizeof(msg),
                     tr(LangKey::ADD_DEV_CONFLICT_1),
                     s_state->channel, type_word(s_state->type));
        }
        lv_obj_t* mb = lv_msgbox_create(lv_layer_top());
        lv_msgbox_add_title(mb, tr(LangKey::ADD_DEV_CONFLICT_TITLE));
        lv_msgbox_add_text(mb, msg);
        lv_obj_t* ok = lv_msgbox_add_footer_button(mb, tr(LangKey::BTN_OK));
        lv_obj_add_event_cb(ok, [](lv_event_t* ev) {
            lv_msgbox_close(static_cast<lv_obj_t*>(lv_event_get_user_data(ev)));
        }, LV_EVENT_CLICKED, mb);
        AC_LOGW(TAG, "channel %d already in use for %s",
                s_state->channel, type_word(s_state->type));
        return;
    }

    uint8_t id = uic.devices->next_free_id();
    if (id == 0) {
        AC_LOGE(TAG, "no free device id");
        return;
    }

    char name[24];
    snprintf(name, sizeof(name), "%s %u", type_word(s_state->type),
             (unsigned)id);

    std::unique_ptr<aqua::devices::IDevice> dev;
    switch (s_state->type) {
        case DeviceType::RELAY:
            if (!uic.drv_pcf) {
                AC_LOGE(TAG, "PCF8575 not initialised");
                return;
            }
            dev = std::make_unique<aqua::devices::RelayDevice>(
                id, name, uic.drv_pcf, (uint8_t)s_state->channel);
            break;
        case DeviceType::PWM:
            if (!uic.drv_pca) {
                AC_LOGE(TAG, "PCA9685 not initialised");
                return;
            }
            dev = std::make_unique<aqua::devices::PwmDevice>(
                id, name, uic.drv_pca, (uint8_t)s_state->channel);
            break;
        case DeviceType::RGB:
            if (!uic.drv_pca) {
                AC_LOGE(TAG, "PCA9685 not initialised");
                return;
            }
            dev = std::make_unique<aqua::devices::RgbDevice>(
                id, name, uic.drv_pca, (uint8_t)s_state->channel);
            break;
    }

    uic.devices->add(std::move(dev));
    aqua::storage::save_devices(*uic.devices);
    AC_LOGI(TAG, "added device id=%u name=\"%s\" channel=%d",
            (unsigned)id, name, s_state->channel);

    // Check hardware slot utilization and raise a warning fault if >= 14/16.
    {
        int relay_slots = 0, pwm_slots = 0;
        for (const auto& d : uic.devices->all()) {
            if (!d) continue;
            if (d->get_type() == DeviceType::RELAY) ++relay_slots;
            if (d->get_type() == DeviceType::PWM)   ++pwm_slots;
            if (d->get_type() == DeviceType::RGB)   pwm_slots += 3;
        }
        if (relay_slots >= 14)
            aqua::faults::raise(0x0020, aqua::faults::Source::OTHER,
                                "Nearly out of relay channels");
        else
            aqua::faults::clear(0x0020, aqua::faults::Source::OTHER);
        if (pwm_slots >= 14)
            aqua::faults::raise(0x0021, aqua::faults::Source::OTHER,
                                "Nearly out of PWM channels");
        else
            aqua::faults::clear(0x0021, aqua::faults::Source::OTHER);
    }

    aqua::scheduler::wake_now();

    screen_manager::pop(screen_manager::Transition::SLIDE_RIGHT);
}

// --- builders --------------------------------------------------------------

static lv_obj_t* make_type_pill(lv_obj_t* parent, const char* text,
                                lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 200, theme::TOUCH_MIN + 8);
    lv_obj_set_style_radius(btn, theme::RADIUS_PILL, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

static lv_obj_t* make_stepper_btn(lv_obj_t* parent, const char* sym,
                                  lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, theme::TOUCH_MIN + 8, theme::TOUCH_MIN + 8);
    lv_obj_set_style_radius(btn, theme::RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(btn, theme::color_surface_alt(), 0);
    lv_obj_set_style_border_color(btn, theme::color_outline(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, theme::color_accent(), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, theme::color_text_primary(), 0);
    lv_obj_set_style_text_color(btn, theme::color_background(),
                                LV_STATE_PRESSED);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, sym);
    lv_obj_set_style_text_font(lbl, theme::font_title(), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

}  // namespace

lv_obj_t* build() {
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_state = new State();
    lv_obj_set_user_data(root, s_state);
    // Auto-free state when the screen is deleted.
    lv_obj_add_event_cb(root, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_obj_get_user_data(
            (lv_obj_t*)lv_event_get_current_target(e)));
        if (st == s_state) s_state = nullptr;
        delete st;
    }, LV_EVENT_DELETE, nullptr);

    chrome::build(root, tr(LangKey::ADD_DEV_TITLE), chrome::pop_on_back,
                  LV_SYMBOL_OK "  Save", on_save);

    const int16_t y0 = chrome::kHeaderH + theme::PAD_LG;

    // --- Type picker -------------------------------------------------------
    lv_obj_t* lbl_type = lv_label_create(root);
    lv_label_set_text(lbl_type, tr(LangKey::ADD_DEV_TYPE));
    lv_obj_set_style_text_font(lbl_type, theme::font_title(), 0);
    lv_obj_set_style_text_color(lbl_type, theme::color_text_secondary(), 0);
    lv_obj_set_pos(lbl_type, theme::PAD_LG, y0);

    s_state->btn_relay = make_type_pill(root,
        LV_SYMBOL_POWER "  Relay", on_type_relay);
    lv_obj_set_pos(s_state->btn_relay, theme::PAD_LG, y0 + 24);

    s_state->btn_pwm = make_type_pill(root,
        LV_SYMBOL_CHARGE "  PWM", on_type_pwm);
    lv_obj_set_pos(s_state->btn_pwm, theme::PAD_LG + 216, y0 + 24);

    s_state->btn_rgb = make_type_pill(root,
        LV_SYMBOL_TINT "  RGB", on_type_rgb);
    lv_obj_set_pos(s_state->btn_rgb, theme::PAD_LG + 216 * 2, y0 + 24);

    // --- Channel stepper ---------------------------------------------------
    const int16_t y_ch = y0 + 24 + theme::TOUCH_MIN + 8 + theme::PAD_LG;
    lv_obj_t* lbl_ch_cap = lv_label_create(root);
    lv_label_set_text(lbl_ch_cap, tr(LangKey::ADD_DEV_CHANNEL));
    lv_obj_set_style_text_font(lbl_ch_cap, theme::font_title(), 0);
    lv_obj_set_style_text_color(lbl_ch_cap,
                                theme::color_text_secondary(), 0);
    lv_obj_set_pos(lbl_ch_cap, theme::PAD_LG, y_ch);

    lv_obj_t* btn_minus = make_stepper_btn(root, LV_SYMBOL_MINUS,
                                           on_channel_minus);
    lv_obj_set_pos(btn_minus, theme::PAD_LG, y_ch + 24);

    s_state->lbl_channel = lv_label_create(root);
    lv_label_set_text(s_state->lbl_channel, "0");
    lv_obj_set_style_text_font(s_state->lbl_channel, theme::font_display(), 0);
    lv_obj_set_style_text_color(s_state->lbl_channel,
                                theme::color_text_primary(), 0);
    lv_obj_set_style_text_align(s_state->lbl_channel,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_state->lbl_channel, 100);
    lv_obj_set_pos(s_state->lbl_channel, theme::PAD_LG + 80,
                   y_ch + 24 + (theme::TOUCH_MIN + 8 - 32) / 2);

    lv_obj_t* btn_plus = make_stepper_btn(root, LV_SYMBOL_PLUS,
                                          on_channel_plus);
    lv_obj_set_pos(btn_plus, theme::PAD_LG + 188, y_ch + 24);

    // Hint to the right of the stepper.
    s_state->lbl_hint = lv_label_create(root);
    lv_label_set_text(s_state->lbl_hint, "");
    lv_obj_set_style_text_font(s_state->lbl_hint, theme::font_caption(), 0);
    lv_obj_set_style_text_color(s_state->lbl_hint,
                                theme::color_text_secondary(), 0);
    lv_label_set_long_mode(s_state->lbl_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_state->lbl_hint, 460);
    lv_obj_set_pos(s_state->lbl_hint, theme::PAD_LG + 280, y_ch + 28);

    // --- Summary card (bottom) --------------------------------------------
    lv_obj_t* summary = lv_obj_create(root);
    lv_obj_set_size(summary, 800 - 2 * theme::PAD_LG, 76);
    lv_obj_set_pos(summary, theme::PAD_LG, 480 - 76 - theme::PAD_LG);
    lv_obj_set_style_bg_color(summary, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(summary, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(summary, theme::color_outline(), 0);
    lv_obj_set_style_border_width(summary, 1, 0);
    lv_obj_set_style_border_opa(summary, LV_OPA_70, 0);
    lv_obj_set_style_radius(summary, theme::RADIUS_LG, 0);
    lv_obj_set_style_pad_all(summary, theme::PAD_MD, 0);
    lv_obj_clear_flag(summary, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* sum_cap = lv_label_create(summary);
    lv_label_set_text(sum_cap, "Will be created as");
    lv_obj_set_style_text_font(sum_cap, theme::font_caption(), 0);
    lv_obj_set_style_text_color(sum_cap,
                                theme::color_text_secondary(), 0);
    lv_obj_align(sum_cap, LV_ALIGN_TOP_LEFT, 0, 0);

    s_state->lbl_summary = lv_label_create(summary);
    lv_label_set_text(s_state->lbl_summary, "");
    lv_obj_set_style_text_font(s_state->lbl_summary, theme::font_title(), 0);
    lv_obj_set_style_text_color(s_state->lbl_summary,
                                theme::color_text_primary(), 0);
    lv_obj_align(s_state->lbl_summary, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    refresh_type_pills();
    refresh_summary();
    return root;
}

}  // namespace aqua::ui::add_device_screen
