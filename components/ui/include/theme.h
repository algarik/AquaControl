// AquaControl — UI theme constants
// All UI code must read colors/fonts/spacing from here. No hardcoded values.
#pragma once

#include "lvgl.h"

namespace aqua::ui::theme {

// --- Colors (dark aquatic palette, see implementation_plan.md §7) -----------
inline lv_color_t color_background()    { return lv_color_hex(0x0D1B2A); }
inline lv_color_t color_surface()       { return lv_color_hex(0x1B2A3B); }
inline lv_color_t color_accent()        { return lv_color_hex(0x00B4D8); }
inline lv_color_t color_accent_2()      { return lv_color_hex(0x48CAE4); }
inline lv_color_t color_success()       { return lv_color_hex(0x52B788); }
inline lv_color_t color_warning()       { return lv_color_hex(0xF4A261); }
inline lv_color_t color_error()         { return lv_color_hex(0xE63946); }
inline lv_color_t color_text_primary()  { return lv_color_hex(0xE0E0E0); }
inline lv_color_t color_text_secondary(){ return lv_color_hex(0x90A4AE); }

// --- Fonts ------------------------------------------------------------------
// Phase 1 placeholders — replaced by Roboto Latin+Cyrillic in Phase 4.
const lv_font_t* font_small();
const lv_font_t* font_body();
const lv_font_t* font_heading();
const lv_font_t* font_large();

// --- Spacing ----------------------------------------------------------------
inline constexpr int16_t PAD_SM = 8;
inline constexpr int16_t PAD_MD = 16;
inline constexpr int16_t PAD_LG = 24;

}  // namespace aqua::ui::theme
