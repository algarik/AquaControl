// AquaControl — Screensaver screen implementation.
//
// Displays a full-screen 7-segment style HH:MM clock with a blinking
// colon and a date/weekday line below.  Touch anywhere pops the screen.
//
// 7-segment geometry (H=252, W=126, T=18):
//
//    ╔══════════╗  ← segment a  (top horizontal)
//    ║          ║  ← segments f (left), b (right)
//    ╠══════════╣  ← segment g  (middle)
//    ║          ║  ← segments e (left), c (right)
//    ╚══════════╝  ← segment d  (bottom horizontal)
//
// Segment junction arithmetic (T/2 padding at each end of vertical segs):
//   VH = H/2 − T − T/2 = 99  → exact join at top/middle/bottom bars.
#include "screensaver_screen.h"

#include <cstdio>

#include "ac_logger.h"
#include "activity.h"
#include "i18n.h"
#include "screen_manager.h"
#include "theme.h"
#include "time_manager.h"
#include "esp_lvgl_port.h"

namespace aqua::ui::screensaver_screen {

namespace {

constexpr const char* TAG = "Screensaver";

// ─────────────────────────────────────────────────────────────────────
// 7-segment geometry constants
// ─────────────────────────────────────────────────────────────────────

constexpr int32_t DIGIT_W  = 126;  // digit bounding-box width
constexpr int32_t DIGIT_H  = 252;  // digit bounding-box height
constexpr int32_t SEG_T    = 18;   // segment thickness
constexpr int32_t DIG_GAP  = 14;   // gap between the two digits in each pair
constexpr int32_t COL_W    = 48;   // colon separator reserved width
constexpr int32_t DOT_SZ   = 22;   // colon dot diameter

// Vertical segment height: exactly fills from inner edge of top/mid/bot bar.
// H/2 = 126; minus SEG_T (18) for the bar, minus SEG_T/2 (9) for the gap.
constexpr int32_t SEG_VH = DIGIT_H / 2 - SEG_T - SEG_T / 2;  // = 99

// Clock widget size: HH:MM = D D COL D D
constexpr int32_t CLOCK_W = DIGIT_W + DIG_GAP + DIGIT_W + COL_W +
                             DIGIT_W + DIG_GAP + DIGIT_W;  // = 580

constexpr int32_t SCREEN_W = 800;
constexpr int32_t SCREEN_H = 480;
constexpr int32_t CLOCK_X  = (SCREEN_W - CLOCK_W) / 2;  // = 110

// Vertical layout: clock + gap + date text, centered on screen.
constexpr int32_t DATE_H   = 30;   // approximate height of font_title row
constexpr int32_t DATE_GAP = 20;   // gap between clock bottom and date text
constexpr int32_t BLOCK_H  = DIGIT_H + DATE_GAP + DATE_H;  // 302
constexpr int32_t CLOCK_Y  = (SCREEN_H - BLOCK_H) / 2;     // = 89

// ─────────────────────────────────────────────────────────────────────
// Segment bit definitions  (bit0=a … bit6=g)
// ─────────────────────────────────────────────────────────────────────

constexpr uint8_t SEG_A = (1 << 0);  // top horizontal
constexpr uint8_t SEG_B = (1 << 1);  // upper-right vertical
constexpr uint8_t SEG_C = (1 << 2);  // lower-right vertical
constexpr uint8_t SEG_D = (1 << 3);  // bottom horizontal
constexpr uint8_t SEG_E = (1 << 4);  // lower-left vertical
constexpr uint8_t SEG_F = (1 << 5);  // upper-left vertical
constexpr uint8_t SEG_G = (1 << 6);  // middle horizontal

// 0–9 segment patterns; index 10 = dash (only g lit).
constexpr uint8_t kSegMap[11] = {
    SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F,          // 0
    SEG_B|SEG_C,                                    // 1
    SEG_A|SEG_B|SEG_D|SEG_E|SEG_G,                 // 2
    SEG_A|SEG_B|SEG_C|SEG_D|SEG_G,                 // 3
    SEG_B|SEG_C|SEG_F|SEG_G,                        // 4
    SEG_A|SEG_C|SEG_D|SEG_F|SEG_G,                 // 5
    SEG_A|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G,           // 6
    SEG_A|SEG_B|SEG_C,                              // 7
    SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G,    // 8
    SEG_A|SEG_B|SEG_C|SEG_D|SEG_F|SEG_G,           // 9
    SEG_G,                                           // 10 = dash (−)
};

// ─────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────

struct State {
    lv_obj_t*   clock_obj  = nullptr;
    lv_obj_t*   date_label = nullptr;
    lv_timer_t* timer      = nullptr;
    int         last_h = -1, last_m = -1, last_s = -1;
    bool        colon_on = true;
};

// ─────────────────────────────────────────────────────────────────────
// Low-level drawing helpers
// ─────────────────────────────────────────────────────────────────────

// Draw a segment using the classic hex (diamond-cut) shape from real LCD displays:
// a center rectangle body + two triangular end-caps that form pointed tips.
// Horizontal when w >= h; vertical when h > w.
// The natural chamfer gaps (C = thin-dim / 2) separate adjacent segment ends.
static void draw_seg_hex(lv_layer_t* layer,
                         int32_t ox, int32_t oy,
                         int32_t w, int32_t h,
                         lv_color_t color, lv_opa_t opa) {
    const bool    horiz = (w >= h);
    const int32_t c     = horiz ? h / 2 : w / 2;

    // ── Body rectangle ───────────────────────────────────────────────
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color     = color;
    rdsc.bg_opa       = opa;
    rdsc.radius       = 0;
    rdsc.border_width = 0;
    rdsc.shadow_width = 0;
    lv_area_t body;
    if (horiz) {
        body = {ox + c, oy, ox + w - c - 1, oy + h - 1};
    } else {
        body = {ox, oy + c, ox + w - 1, oy + h - c - 1};
    }
    lv_draw_rect(layer, &rdsc, &body);

    // ── Triangular end-caps ──────────────────────────────────────────
    lv_draw_triangle_dsc_t tdsc;
    lv_draw_triangle_dsc_init(&tdsc);
    tdsc.color = color;
    tdsc.opa   = opa;
    if (horiz) {
        // Left cap: tip at left-mid, inner edge at x = ox+c
        tdsc.p[0].x = (lv_value_precise_t)ox;
        tdsc.p[0].y = (lv_value_precise_t)(oy + h / 2);
        tdsc.p[1].x = (lv_value_precise_t)(ox + c);
        tdsc.p[1].y = (lv_value_precise_t)oy;
        tdsc.p[2].x = (lv_value_precise_t)(ox + c);
        tdsc.p[2].y = (lv_value_precise_t)(oy + h);
        lv_draw_triangle(layer, &tdsc);
        // Right cap: tip at right-mid, inner edge at x = ox+w-c
        tdsc.p[0].x = (lv_value_precise_t)(ox + w);
        tdsc.p[0].y = (lv_value_precise_t)(oy + h / 2);
        tdsc.p[1].x = (lv_value_precise_t)(ox + w - c);
        tdsc.p[1].y = (lv_value_precise_t)oy;
        tdsc.p[2].x = (lv_value_precise_t)(ox + w - c);
        tdsc.p[2].y = (lv_value_precise_t)(oy + h);
        lv_draw_triangle(layer, &tdsc);
    } else {
        // Top cap: tip at top-mid, inner edge at y = oy+c
        tdsc.p[0].x = (lv_value_precise_t)(ox + w / 2);
        tdsc.p[0].y = (lv_value_precise_t)oy;
        tdsc.p[1].x = (lv_value_precise_t)ox;
        tdsc.p[1].y = (lv_value_precise_t)(oy + c);
        tdsc.p[2].x = (lv_value_precise_t)(ox + w);
        tdsc.p[2].y = (lv_value_precise_t)(oy + c);
        lv_draw_triangle(layer, &tdsc);
        // Bottom cap: tip at bottom-mid, inner edge at y = oy+h-c
        tdsc.p[0].x = (lv_value_precise_t)(ox + w / 2);
        tdsc.p[0].y = (lv_value_precise_t)(oy + h);
        tdsc.p[1].x = (lv_value_precise_t)ox;
        tdsc.p[1].y = (lv_value_precise_t)(oy + h - c);
        tdsc.p[2].x = (lv_value_precise_t)(ox + w);
        tdsc.p[2].y = (lv_value_precise_t)(oy + h - c);
        lv_draw_triangle(layer, &tdsc);
    }
}

// Draw one 7-segment digit.  ox/oy are absolute screen coordinates of the
// digit's top-left corner.  digit 0–9 draw the numeral; 10 draws a dash.
static void draw_digit(lv_layer_t* layer,
                       int32_t ox, int32_t oy,
                       uint8_t digit,
                       lv_color_t col_on, lv_color_t col_off) {
    const uint8_t segs = kSegMap[digit <= 10 ? digit : 10];

    // Segment opacity: active = opaque, inactive = barely visible ghost.
    constexpr lv_opa_t OPA_ON  = LV_OPA_COVER;
    constexpr lv_opa_t OPA_OFF = 22;  // ~8 % — retro ghost effect

    // a — top horizontal
    draw_seg_hex(layer,
        ox + SEG_T / 2,        oy,
        DIGIT_W - SEG_T,       SEG_T,
        (segs & SEG_A) ? col_on : col_off,
        (segs & SEG_A) ? OPA_ON : OPA_OFF);

    // b — upper-right vertical
    draw_seg_hex(layer,
        ox + DIGIT_W - SEG_T,  oy + SEG_T,
        SEG_T,                 SEG_VH,
        (segs & SEG_B) ? col_on : col_off,
        (segs & SEG_B) ? OPA_ON : OPA_OFF);

    // f — upper-left vertical
    draw_seg_hex(layer,
        ox,                    oy + SEG_T,
        SEG_T,                 SEG_VH,
        (segs & SEG_F) ? col_on : col_off,
        (segs & SEG_F) ? OPA_ON : OPA_OFF);

    // g — middle horizontal
    draw_seg_hex(layer,
        ox + SEG_T / 2,        oy + DIGIT_H / 2 - SEG_T / 2,
        DIGIT_W - SEG_T,       SEG_T,
        (segs & SEG_G) ? col_on : col_off,
        (segs & SEG_G) ? OPA_ON : OPA_OFF);

    // c — lower-right vertical
    draw_seg_hex(layer,
        ox + DIGIT_W - SEG_T,  oy + DIGIT_H / 2 + SEG_T / 2,
        SEG_T,                 SEG_VH,
        (segs & SEG_C) ? col_on : col_off,
        (segs & SEG_C) ? OPA_ON : OPA_OFF);

    // e — lower-left vertical
    draw_seg_hex(layer,
        ox,                    oy + DIGIT_H / 2 + SEG_T / 2,
        SEG_T,                 SEG_VH,
        (segs & SEG_E) ? col_on : col_off,
        (segs & SEG_E) ? OPA_ON : OPA_OFF);

    // d — bottom horizontal
    draw_seg_hex(layer,
        ox + SEG_T / 2,        oy + DIGIT_H - SEG_T,
        DIGIT_W - SEG_T,       SEG_T,
        (segs & SEG_D) ? col_on : col_off,
        (segs & SEG_D) ? OPA_ON : OPA_OFF);
}

// Draw the blinking colon separator.  Two circular dots vertically centered
// at H/3 and 2H/3 within the reserved COL_W band.
static void draw_colon(lv_layer_t* layer,
                       int32_t ox, int32_t oy,
                       bool visible,
                       lv_color_t col_on, lv_color_t col_off) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color     = visible ? col_on : col_off;
    dsc.bg_opa       = visible ? LV_OPA_COVER : (lv_opa_t)22;
    dsc.radius       = DOT_SZ / 2;   // fully circular
    dsc.border_width = 0;
    dsc.shadow_width = 0;

    int32_t cx = ox + COL_W / 2 - DOT_SZ / 2;  // centered horizontally
    int32_t y1 = oy + DIGIT_H / 3 - DOT_SZ / 2;
    int32_t y2 = oy + 2 * DIGIT_H / 3 - DOT_SZ / 2;

    lv_area_t a1 = {cx, y1, cx + DOT_SZ - 1, y1 + DOT_SZ - 1};
    lv_area_t a2 = {cx, y2, cx + DOT_SZ - 1, y2 + DOT_SZ - 1};
    lv_draw_rect(layer, &dsc, &a1);
    lv_draw_rect(layer, &dsc, &a2);
}

// ─────────────────────────────────────────────────────────────────────
// Clock widget draw callback (LV_EVENT_DRAW_MAIN)
// ─────────────────────────────────────────────────────────────────────

static void on_clock_draw(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;

    lv_layer_t* layer = lv_event_get_layer(e);
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st || !layer) return;

    // Absolute screen coordinates of the clock widget origin.
    lv_area_t coords;
    lv_obj_get_coords(st->clock_obj, &coords);
    int32_t ox = coords.x1;
    int32_t oy = coords.y1;

    lv_color_t col_on  = theme::color_accent();           // cyan
    lv_color_t col_off = lv_color_hex(0x0D2030);          // dim ghost

    struct tm lt = aqua::time_mgr::TimeManager::now_local();
    int h = aqua::time_mgr::TimeManager::is_synced() ? lt.tm_hour : 10;  // 10 = dash
    int m = aqua::time_mgr::TimeManager::is_synced() ? lt.tm_min  : 10;

    // Layout: D1 [DIG_GAP] D2 [COL_W] D3 [DIG_GAP] D4
    int32_t x = ox;

    draw_digit(layer, x, oy, (uint8_t)(h / 10), col_on, col_off);
    x += DIGIT_W + DIG_GAP;

    draw_digit(layer, x, oy, (uint8_t)(h % 10), col_on, col_off);
    x += DIGIT_W;

    draw_colon(layer, x, oy, st->colon_on, col_on, col_off);
    x += COL_W;

    draw_digit(layer, x, oy, (uint8_t)(m / 10), col_on, col_off);
    x += DIGIT_W + DIG_GAP;

    draw_digit(layer, x, oy, (uint8_t)(m % 10), col_on, col_off);
}

// ─────────────────────────────────────────────────────────────────────
// 500 ms timer — updates colon blink and time/date text
// ─────────────────────────────────────────────────────────────────────

static void on_tick(lv_timer_t* t) {
    auto* st = static_cast<State*>(lv_timer_get_user_data(t));
    if (!st) return;

    struct tm lt = aqua::time_mgr::TimeManager::now_local();
    const bool colon_now = (lt.tm_sec % 2 == 0);
    const bool time_ch   = (lt.tm_hour != st->last_h ||
                            lt.tm_min  != st->last_m ||
                            lt.tm_sec  != st->last_s);
    const bool colon_ch  = (colon_now != st->colon_on);

    if (time_ch || colon_ch) {
        st->colon_on = colon_now;
        st->last_h   = lt.tm_hour;
        st->last_m   = lt.tm_min;
        st->last_s   = lt.tm_sec;
        lv_obj_invalidate(st->clock_obj);
    }

    // Update date label on any minute boundary (or if not yet set).
    if (st->last_s == lt.tm_sec || time_ch) {
        if (aqua::time_mgr::TimeManager::is_synced()) {
            using aqua::ui::i18n::LangKey;
            using aqua::ui::i18n::tr;
            const char* wday = tr(static_cast<LangKey>(
                static_cast<uint16_t>(LangKey::DASH_WDAY_SUN) + (lt.tm_wday % 7)));
            const char* mon = tr(static_cast<LangKey>(
                static_cast<uint16_t>(LangKey::DASH_MON_JAN) + (lt.tm_mon % 12)));
            char buf[56];
            // U+00B7 middle dot in UTF-8 is \xc2\xb7
            snprintf(buf, sizeof(buf), "%s  \xc2\xb7  %02d %s %04d",
                     wday, lt.tm_mday, mon, lt.tm_year + 1900);
            lv_label_set_text(st->date_label, buf);
        } else {
            lv_label_set_text(st->date_label,
                              aqua::ui::i18n::tr(aqua::ui::i18n::LangKey::DASH_SYNCING));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// Touch dismiss
// ─────────────────────────────────────────────────────────────────────

static void on_dismiss(lv_event_t* /*e*/) {
    aqua::activity::notify();  // reset idle timer before popping
    aqua::ui::screen_manager::pop(aqua::ui::screen_manager::Transition::NONE);
}

// ─────────────────────────────────────────────────────────────────────
// Cleanup
// ─────────────────────────────────────────────────────────────────────

static void on_screen_delete(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    if (!st) return;
    if (st->timer) {
        lv_timer_delete(st->timer);
        st->timer = nullptr;
    }
    delete st;
}

// ─────────────────────────────────────────────────────────────────────
// lv_async_call callback — runs on Core 1 (LVGL task)
// ─────────────────────────────────────────────────────────────────────

static void do_push_cb(void* /*arg*/) {
    // Must be depth == 1 (dashboard) to avoid double-stacking.
    if (aqua::ui::screen_manager::depth() != 1) return;
    lv_obj_t* scrn = build();
    if (scrn) {
        aqua::ui::screen_manager::push(
            scrn, aqua::ui::screen_manager::Transition::NONE);
    }
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────

lv_obj_t* build() {
    auto* st = new State();

    // ── Root screen ───────────────────────────────────────────────────
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    // Entire screen is a touch target for dismissal.
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, on_dismiss,       LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(root, on_screen_delete, LV_EVENT_DELETE,  st);

    // ── Clock widget (custom-drawn 7-segment digits) ───────────────────
    lv_obj_t* clock_obj = lv_obj_create(root);
    lv_obj_set_size(clock_obj, CLOCK_W, DIGIT_H);
    lv_obj_set_pos(clock_obj, CLOCK_X, CLOCK_Y);
    lv_obj_set_style_bg_opa(clock_obj,    LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_obj, 0,          0);
    lv_obj_set_style_pad_all(clock_obj,   0,             0);
    lv_obj_set_style_shadow_width(clock_obj, 0,          0);
    lv_obj_clear_flag(clock_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(clock_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(clock_obj, on_clock_draw, LV_EVENT_DRAW_MAIN, st);
    st->clock_obj = clock_obj;

    // ── Date / weekday label ──────────────────────────────────────────
    // Positioned below the clock block, centered horizontally.
    lv_obj_t* date_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(date_lbl, theme::font_title(), 0);
    lv_obj_set_style_text_color(date_lbl, theme::color_text_secondary(), 0);
    lv_label_set_text(date_lbl, "");
    lv_obj_align(date_lbl, LV_ALIGN_TOP_MID, 0,
                 CLOCK_Y + DIGIT_H + DATE_GAP);
    st->date_label = date_lbl;

    // ── Timer — 500 ms for colon blink and time refresh ───────────────
    st->timer = lv_timer_create(on_tick, 500, st);
    on_tick(st->timer);  // populate immediately; avoids 500 ms blank flash

    AC_LOGI(TAG, "screensaver built");
    return root;
}

void schedule_push() {
    if (lv_async_call(do_push_cb, nullptr) != LV_RESULT_OK) {
        AC_LOGE(TAG, "schedule_push: lv_async_call failed");
    }
}

}  // namespace aqua::ui::screensaver_screen
