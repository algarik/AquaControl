// AquaControl — UI theme constants
// All UI code must read colors/fonts/spacing from here. No hardcoded values.
// See /memories/repo/aquacontrol-ui-design-system.md for the rationale.
#pragma once

#include "lvgl.h"

namespace aqua::ui::theme {

// --- Colors (deep-water aquatech palette) -----------------------------------
inline lv_color_t color_background()    { return lv_color_hex(0x0A1620); }
inline lv_color_t color_surface()       { return lv_color_hex(0x152433); }
inline lv_color_t color_surface_alt()   { return lv_color_hex(0x1E3247); }
inline lv_color_t color_outline()       { return lv_color_hex(0x2B4055); }
inline lv_color_t color_accent()        { return lv_color_hex(0x22D3EE); }
inline lv_color_t color_accent_2()      { return lv_color_hex(0x67E8F9); }
inline lv_color_t color_success()       { return lv_color_hex(0x34D399); }
inline lv_color_t color_warning()       { return lv_color_hex(0xFBBF24); }
inline lv_color_t color_error()         { return lv_color_hex(0xF87171); }
inline lv_color_t color_text_primary()  { return lv_color_hex(0xF1F5F9); }
inline lv_color_t color_text_secondary(){ return lv_color_hex(0x94A3B8); }
inline lv_color_t color_text_disabled() { return lv_color_hex(0x475569); }

// --- Fonts ------------------------------------------------------------------
// Roboto Regular with Latin + Latin-1 supplement + full Cyrillic + LVGL
// FontAwesome icon subset (LV_SYMBOL_* macros). Generated via lv_font_conv
// from tools/fonts/Roboto-Regular.ttf + fa-solid-900.ttf.
const lv_font_t* font_caption();   // 14 px
const lv_font_t* font_body();      // 18 px
const lv_font_t* font_title();     // 24 px
const lv_font_t* font_display();   // 32 px

// Built-in Montserrat at larger sizes for the dashboard "watch" face.
// These are ASCII-only (no Cyrillic) and only used for numerics + short
// English labels where Cyrillic is never displayed.
const lv_font_t* font_value_xl();  // 28 px — big sensor numbers
const lv_font_t* font_clock();     // 48 px — giant clock HH:MM

// Legacy names kept for any caller still using them.
inline const lv_font_t* font_small()   { return font_caption(); }
inline const lv_font_t* font_heading() { return font_title(); }
inline const lv_font_t* font_large()   { return font_display(); }

// --- Spacing & radii --------------------------------------------------------
inline constexpr int16_t PAD_XS = 4;
inline constexpr int16_t PAD_SM = 8;
inline constexpr int16_t PAD_MD = 16;
inline constexpr int16_t PAD_LG = 24;
inline constexpr int16_t PAD_XL = 32;

inline constexpr int16_t RADIUS_SM   = 8;
inline constexpr int16_t RADIUS_MD   = 14;
inline constexpr int16_t RADIUS_LG   = 20;
inline constexpr int16_t RADIUS_PILL = 999;

inline constexpr int16_t TOUCH_MIN = 56;

}  // namespace aqua::ui::theme
