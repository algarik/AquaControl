// AquaControl - "Coming soon" placeholder.
#include "placeholder_screen.h"

#include "chrome.h"
#include "theme.h"

namespace aqua::ui::placeholder_screen {

lv_obj_t* build(const char* title, const char* message) {
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    chrome::build(root, title, chrome::pop_on_back);

    lv_obj_t* lbl_icon = lv_label_create(root);
    lv_label_set_text(lbl_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(lbl_icon, theme::font_display(), 0);
    lv_obj_set_style_text_color(lbl_icon, theme::color_text_secondary(), 0);
    lv_obj_align(lbl_icon, LV_ALIGN_CENTER, 0, -32);

    lv_obj_t* lbl_msg = lv_label_create(root);
    lv_label_set_text(lbl_msg, message ? message : "Coming soon.");
    lv_obj_set_style_text_font(lbl_msg, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl_msg, theme::color_text_secondary(), 0);
    lv_obj_set_style_text_align(lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_msg, 640);
    lv_label_set_long_mode(lbl_msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(lbl_msg, LV_ALIGN_CENTER, 0, 24);

    return root;
}

}  // namespace aqua::ui::placeholder_screen
