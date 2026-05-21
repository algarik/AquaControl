// AquaControl - Screen chrome (header bar) helper.
//
// All non-dashboard screens share a 56 px header containing a back-arrow
// button on the left, a centred title, and an optional right-hand action
// (e.g. "Save", "+ Add"). Wiring is done via plain LVGL event callbacks
// so each screen can decide how to react.
#pragma once

#include "lvgl.h"

namespace aqua::ui::chrome {

struct Header {
    lv_obj_t* root        = nullptr;  // header container (anchored top)
    lv_obj_t* btn_back    = nullptr;
    lv_obj_t* lbl_title   = nullptr;
    lv_obj_t* btn_action  = nullptr;  // nullptr if none
    lv_obj_t* lbl_action  = nullptr;
};

// Build a header inside `parent`, anchored to the top with 100 % width.
// `on_back` is invoked when the user taps the back arrow; pass nullptr to
// hide the back button (useful for the dashboard itself, which is root).
// If `action_text` is non-null an action button is shown on the right and
// `on_action` is invoked when tapped.
Header build(lv_obj_t* parent,
             const char* title,
             lv_event_cb_t on_back,
             const char* action_text = nullptr,
             lv_event_cb_t on_action = nullptr);

// Convenience back-button callback: pops the current screen. Use directly
// in `build(parent, title, chrome::pop_on_back)`.
void pop_on_back(lv_event_t* e);

constexpr int16_t kHeaderH = 56;

}  // namespace aqua::ui::chrome
