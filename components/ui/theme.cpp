#include "theme.h"

// Generated faces (components/ui/fonts/roboto_*.c). LV_FONT_DECLARE is just
// `extern const lv_font_t name;` so we can use it for our own files too.
extern "C" {
    LV_FONT_DECLARE(roboto_14);
    LV_FONT_DECLARE(roboto_18);
    LV_FONT_DECLARE(roboto_24);
    LV_FONT_DECLARE(roboto_32);
    // C-2: roboto_48 replaces lv_font_montserrat_48 — covers Latin-1 (°) + Cyrillic.
    LV_FONT_DECLARE(roboto_48);
}

namespace aqua::ui::theme {

const lv_font_t* font_caption() { return &roboto_14; }
const lv_font_t* font_body()    { return &roboto_18; }
const lv_font_t* font_title()   { return &roboto_24; }
const lv_font_t* font_display() { return &roboto_32; }
// C-2: roboto_32 replaces lv_font_montserrat_28 — covers ° (U+00B0) and Cyrillic.
const lv_font_t* font_value_xl(){ return &roboto_32; }
// C-2: roboto_48 replaces lv_font_montserrat_48 — covers ° (U+00B0) and Cyrillic.
const lv_font_t* font_clock()   { return &roboto_48; }

}  // namespace aqua::ui::theme
