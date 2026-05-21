// AquaControl - chrome helper implementation.
#include "chrome.h"

#include "screen_manager.h"
#include "theme.h"

namespace aqua::ui::chrome {

namespace {

constexpr int16_t kBtnW = 48;

static lv_obj_t* make_chrome_btn(lv_obj_t* parent, const char* symbol_text,
                                 lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, kBtnW, theme::TOUCH_MIN);
    lv_obj_set_style_radius(btn, theme::RADIUS_MD, 0);
    lv_obj_set_style_bg_color(btn, theme::color_surface_alt(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, theme::color_accent(), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, theme::color_text_primary(), 0);
    lv_obj_set_style_text_color(btn, theme::color_background(),
                                LV_STATE_PRESSED);

    lv_obj_t* l = lv_label_create(btn);
    lv_label_set_text(l, symbol_text);
    lv_obj_set_style_text_font(l, theme::font_title(), 0);
    lv_obj_center(l);

    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

}  // namespace

Header build(lv_obj_t* parent, const char* title, lv_event_cb_t on_back,
             const char* action_text, lv_event_cb_t on_action) {
    Header h{};

    h.root = lv_obj_create(parent);
    lv_obj_set_size(h.root, LV_PCT(100), kHeaderH);
    lv_obj_set_pos(h.root, 0, 0);
    lv_obj_set_style_bg_color(h.root, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(h.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(h.root, 1, 0);
    lv_obj_set_style_border_side(h.root, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(h.root, theme::color_outline(), 0);
    lv_obj_set_style_border_opa(h.root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(h.root, 0, 0);
    lv_obj_set_style_pad_hor(h.root, theme::PAD_MD, 0);
    lv_obj_set_style_pad_ver(h.root, 0, 0);
    lv_obj_clear_flag(h.root, LV_OBJ_FLAG_SCROLLABLE);

    if (on_back) {
        h.btn_back = make_chrome_btn(h.root, LV_SYMBOL_LEFT, on_back);
        lv_obj_align(h.btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    }

    h.lbl_title = lv_label_create(h.root);
    lv_label_set_text(h.lbl_title, title ? title : "");
    lv_obj_set_style_text_font(h.lbl_title, theme::font_title(), 0);
    lv_obj_set_style_text_color(h.lbl_title, theme::color_text_primary(), 0);
    lv_obj_align(h.lbl_title, LV_ALIGN_CENTER, 0, 0);

    if (action_text && *action_text) {
        h.btn_action = lv_btn_create(h.root);
        lv_obj_set_height(h.btn_action, theme::TOUCH_MIN);
        lv_obj_set_style_radius(h.btn_action, theme::RADIUS_PILL, 0);
        lv_obj_set_style_bg_color(h.btn_action, theme::color_accent(), 0);
        lv_obj_set_style_bg_opa(h.btn_action, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(h.btn_action, 0, 0);
        lv_obj_set_style_shadow_width(h.btn_action, 0, 0);
        lv_obj_set_style_pad_hor(h.btn_action, theme::PAD_MD, 0);
        lv_obj_set_style_bg_color(h.btn_action, theme::color_accent_2(),
                                  LV_STATE_PRESSED);
        h.lbl_action = lv_label_create(h.btn_action);
        lv_label_set_text(h.lbl_action, action_text);
        lv_obj_set_style_text_font(h.lbl_action, theme::font_body(), 0);
        lv_obj_set_style_text_color(h.lbl_action,
                                    theme::color_background(), 0);
        lv_obj_center(h.lbl_action);
        lv_obj_align(h.btn_action, LV_ALIGN_RIGHT_MID, 0, 0);
        if (on_action) {
            lv_obj_add_event_cb(h.btn_action, on_action,
                                LV_EVENT_CLICKED, nullptr);
        }
    }

    return h;
}

void pop_on_back(lv_event_t*) {
    screen_manager::pop();
}

}  // namespace aqua::ui::chrome
