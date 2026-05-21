// AquaControl — Drum-roller numeric input widget implementation.
#include "drum_roller.h"

#include <cstdio>
#include <cstring>

#include "theme.h"

namespace aqua::ui::drum_roller {

namespace {

// Applies common styling to every roller (dark theme, body font, tall rows).
static void style_roller(lv_obj_t* roller) {
    lv_obj_set_style_bg_color(roller, theme::color_surface(),     LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller, theme::color_surface_alt(), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER,                 LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, theme::color_text_secondary(), LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, theme::color_text_primary(),   LV_PART_SELECTED);
    lv_obj_set_style_text_font(roller, theme::font_body(),             LV_PART_MAIN);
    lv_obj_set_style_text_font(roller, theme::font_body(),             LV_PART_SELECTED);
    lv_obj_set_style_border_width(roller, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(roller, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(roller, theme::RADIUS_SM, LV_PART_MAIN);
    // Tall enough for comfortable touch (min 3 visible rows).
    lv_roller_set_visible_row_count(roller, 3);
}

// Builds a string of newline-separated integers [lo..hi].
// Caller must free the returned buffer with delete[].
static char* build_int_options(int lo, int hi) {
    // Max 6 chars + '\n' per entry.
    int count = hi - lo + 1;
    char* buf = new char[static_cast<size_t>(count) * 7 + 1];
    char* p   = buf;
    for (int v = lo; v <= hi; ++v) {
        p += snprintf(p, 7, "%d", v);
        if (v < hi) { *p++ = '\n'; }
    }
    *p = '\0';
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
// HH:MM picker
// ---------------------------------------------------------------------------

// We tag rollers inside the container by index using user-data index.
// IMPORTANT: must be non-zero — LVGL zero-initialises user_data for newly
// created objects (e.g. the separator label) so a tag of 0 would match
// every untagged widget and corrupt the readback.
static constexpr uintptr_t kTagHH = 1;
static constexpr uintptr_t kTagMM = 2;

lv_obj_t* time_hhmm(lv_obj_t* parent, int init_hh, int init_mm) {
    // Container — horizontal flexbox.
    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cont, theme::PAD_SM, LV_PART_MAIN);

    // Hours roller (00–23).
    lv_obj_t* rh = lv_roller_create(cont);
    char* hh_opts = build_int_options(0, 23);
    // Zero-pad display.
    char hh_padded[24 * 4];
    {
        char* q = hh_padded;
        for (int i = 0; i <= 23; ++i) {
            q += snprintf(q, 4, "%02d", i);
            if (i < 23) *q++ = '\n';
        }
        *q = '\0';
    }
    delete[] hh_opts;
    lv_roller_set_options(rh, hh_padded, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(rh, (uint16_t)init_hh, LV_ANIM_OFF);
    style_roller(rh);
    lv_obj_set_width(rh, 72);
    lv_obj_set_user_data(rh, reinterpret_cast<void*>(kTagHH));

    // Colon separator.
    lv_obj_t* sep = lv_label_create(cont);
    lv_label_set_text(sep, ":");
    lv_obj_set_style_text_font(sep, theme::font_body(), 0);
    lv_obj_set_style_text_color(sep, theme::color_text_secondary(), 0);

    // Minutes roller (00–59).
    lv_obj_t* rm = lv_roller_create(cont);
    {
        char mm_padded[60 * 4];
        char* q = mm_padded;
        for (int i = 0; i <= 59; ++i) {
            q += snprintf(q, 4, "%02d", i);
            if (i < 59) *q++ = '\n';
        }
        *q = '\0';
        lv_roller_set_options(rm, mm_padded, LV_ROLLER_MODE_NORMAL);
    }
    lv_roller_set_selected(rm, (uint16_t)init_mm, LV_ANIM_OFF);
    style_roller(rm);
    lv_obj_set_width(rm, 72);
    lv_obj_set_user_data(rm, reinterpret_cast<void*>(kTagMM));

    return cont;
}

void get_time_hhmm(lv_obj_t* container, int* out_hh, int* out_mm) {
    if (!container || !out_hh || !out_mm) return;
    *out_hh = 0; *out_mm = 0;
    uint32_t n = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* child = lv_obj_get_child(container, (int32_t)i);
        if (lv_obj_get_user_data(child) == reinterpret_cast<void*>(kTagHH))
            *out_hh = (int)lv_roller_get_selected(child);
        else if (lv_obj_get_user_data(child) == reinterpret_cast<void*>(kTagMM))
            *out_mm = (int)lv_roller_get_selected(child);
    }
}

// ---------------------------------------------------------------------------
// Duration in minutes
// ---------------------------------------------------------------------------
// Values: 1–60 (step 1) then 75, 90, 105, 120, 150, 180, 240, 300, 360.
// The roller index does NOT directly equal the minute value — use
// get_duration_min() which reverses the mapping.

namespace {
// Canonical durations offered.
static const int kDurations[] = {
     1,  2,  3,  4,  5,  6,  7,  8,  9, 10,
    12, 15, 20, 25, 30, 45, 60, 75, 90, 120,
   150, 180, 240, 300, 360
};
static constexpr int kDurCount = (int)(sizeof(kDurations) / sizeof(kDurations[0]));

// Returns index of nearest value >= target in kDurations.
static int dur_index(int target_min) {
    for (int i = 0; i < kDurCount; ++i) {
        if (kDurations[i] >= target_min) return i;
    }
    return kDurCount - 1;
}

static char* build_duration_options() {
    char* buf = new char[kDurCount * 8 + 1];
    char* p   = buf;
    for (int i = 0; i < kDurCount; ++i) {
        int m = kDurations[i];
        if (m < 60)       p += snprintf(p, 8, "%d min", m);
        else if (m == 60) p += snprintf(p, 8, "1 h");
        else              p += snprintf(p, 8, "%d h %d", m / 60, m % 60);
        if (i < kDurCount - 1) *p++ = '\n';
    }
    *p = '\0';
    return buf;
}
}  // namespace

lv_obj_t* duration_min(lv_obj_t* parent, int init_min) {
    lv_obj_t* r = lv_roller_create(parent);
    char* opts  = build_duration_options();
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    delete[] opts;
    lv_roller_set_selected(r, (uint16_t)dur_index(init_min), LV_ANIM_OFF);
    style_roller(r);
    lv_obj_set_width(r, 120);
    return r;
}

int get_duration_min(lv_obj_t* roller) {
    if (!roller) return 0;
    int idx = (int)lv_roller_get_selected(roller);
    if (idx < 0 || idx >= kDurCount) return 0;
    return kDurations[idx];
}

// ---------------------------------------------------------------------------
// Percent (0–100)
// ---------------------------------------------------------------------------

lv_obj_t* percent(lv_obj_t* parent, int init_pct) {
    lv_obj_t* r  = lv_roller_create(parent);
    char* opts = new char[101 * 6 + 1];
    char* p    = opts;
    for (int i = 0; i <= 100; ++i) {
        p += snprintf(p, 6, "%d%%", i);
        if (i < 100) *p++ = '\n';
    }
    *p = '\0';
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    delete[] opts;
    int sel = init_pct < 0 ? 0 : init_pct > 100 ? 100 : init_pct;
    lv_roller_set_selected(r, (uint16_t)sel, LV_ANIM_OFF);
    style_roller(r);
    lv_obj_set_width(r, 96);
    return r;
}

int get_percent(lv_obj_t* roller) {
    if (!roller) return 0;
    return (int)lv_roller_get_selected(roller);
}

// ---------------------------------------------------------------------------
// Signed offset –360…+360 min (step 5)
// ---------------------------------------------------------------------------

namespace {
static const int kOffMin = -360;
static const int kOffMax =  360;
static const int kOffStep=   5;
static const int kOffCount = (kOffMax - kOffMin) / kOffStep + 1;  // 145

static int off_index(int min) {
    int idx = (min - kOffMin) / kOffStep;
    if (idx < 0) idx = 0;
    if (idx >= kOffCount) idx = kOffCount - 1;
    return idx;
}

static char* build_offset_options() {
    char* buf = new char[kOffCount * 10 + 1];
    char* p   = buf;
    for (int v = kOffMin; v <= kOffMax; v += kOffStep) {
        int abs_v = v < 0 ? -v : v;
        int h = abs_v / 60;
        int m = abs_v % 60;
        if (v >= 0) p += snprintf(p, 10, "+%d:%02d", h, m);
        else        p += snprintf(p, 10, "-%d:%02d", h, m);
        if (v < kOffMax) *p++ = '\n';
    }
    *p = '\0';
    return buf;
}
}  // namespace

lv_obj_t* signed_offset(lv_obj_t* parent, int init_min) {
    lv_obj_t* r = lv_roller_create(parent);
    char* opts  = build_offset_options();
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    delete[] opts;
    lv_roller_set_selected(r, (uint16_t)off_index(init_min), LV_ANIM_OFF);
    style_roller(r);
    lv_obj_set_width(r, 120);
    return r;
}

int get_signed_offset(lv_obj_t* roller) {
    if (!roller) return 0;
    return kOffMin + (int)lv_roller_get_selected(roller) * kOffStep;
}

// ---------------------------------------------------------------------------
// Generic integer range
// ---------------------------------------------------------------------------

lv_obj_t* integer_range(lv_obj_t* parent, int lo, int hi, int init) {
    lv_obj_t* r = lv_roller_create(parent);
    char* opts  = build_int_options(lo, hi);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    delete[] opts;
    int sel = init - lo;
    if (sel < 0) sel = 0;
    if (sel > hi - lo) sel = hi - lo;
    lv_roller_set_selected(r, (uint16_t)sel, LV_ANIM_OFF);
    style_roller(r);
    lv_obj_set_width(r, 96);
    return r;
}

int get_integer_range(lv_obj_t* roller, int lo) {
    if (!roller) return lo;
    return lo + (int)lv_roller_get_selected(roller);
}

// ---------------------------------------------------------------------------
// Fractional 0.0–5.0 in 0.1 steps  (50 entries, indices 0–50)
// ---------------------------------------------------------------------------

lv_obj_t* fractional_tenth(lv_obj_t* parent, float init) {
    lv_obj_t* r   = lv_roller_create(parent);
    char* buf = new char[51 * 6 + 1];
    char* p   = buf;
    for (int i = 0; i <= 50; ++i) {
        p += snprintf(p, 6, "%.1f", i * 0.1f);
        if (i < 50) *p++ = '\n';
    }
    *p = '\0';
    lv_roller_set_options(r, buf, LV_ROLLER_MODE_NORMAL);
    delete[] buf;
    int sel = (int)(init * 10.0f + 0.5f);
    if (sel < 0)  sel = 0;
    if (sel > 50) sel = 50;
    lv_roller_set_selected(r, (uint16_t)sel, LV_ANIM_OFF);
    style_roller(r);
    lv_obj_set_width(r, 80);
    return r;
}

float get_fractional_tenth(lv_obj_t* roller) {
    if (!roller) return 0.0f;
    return (float)lv_roller_get_selected(roller) * 0.1f;
}

// ---------------------------------------------------------------------------
// Temperature threshold: integer roller + tenths roller
// ---------------------------------------------------------------------------

static constexpr uintptr_t kTagTInt  = 10;
static constexpr uintptr_t kTagTFrac = 11;

lv_obj_t* temp_threshold(lv_obj_t* parent, float init_c,
                          float lo, float hi) {
    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cont, theme::PAD_SM, LV_PART_MAIN);

    int i_lo   = (int)lo;
    int i_hi   = (int)hi;
    int i_init = (int)init_c;
    if (i_init < i_lo) i_init = i_lo;
    if (i_init > i_hi) i_init = i_hi;

    // Integer part.
    lv_obj_t* ri = lv_roller_create(cont);
    char* int_opts = build_int_options(i_lo, i_hi);
    lv_roller_set_options(ri, int_opts, LV_ROLLER_MODE_NORMAL);
    delete[] int_opts;
    lv_roller_set_selected(ri, (uint16_t)(i_init - i_lo), LV_ANIM_OFF);
    style_roller(ri);
    lv_obj_set_width(ri, 64);
    lv_obj_set_user_data(ri, reinterpret_cast<void*>(kTagTInt));

    // Dot separator.
    lv_obj_t* dot = lv_label_create(cont);
    lv_label_set_text(dot, ".");
    lv_obj_set_style_text_font(dot, theme::font_body(), 0);
    lv_obj_set_style_text_color(dot, theme::color_text_secondary(), 0);

    // Tenths part (0–9).
    lv_obj_t* rf = lv_roller_create(cont);
    {
        const char* opts_10 = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";
        lv_roller_set_options(rf, opts_10, LV_ROLLER_MODE_NORMAL);
    }
    int frac_init = (int)((init_c - (int)init_c) * 10.0f + 0.5f);
    lv_roller_set_selected(rf, (uint16_t)frac_init, LV_ANIM_OFF);
    style_roller(rf);
    lv_obj_set_width(rf, 48);
    lv_obj_set_user_data(rf, reinterpret_cast<void*>(kTagTFrac));

    // Degree unit label.
    lv_obj_t* unit = lv_label_create(cont);
    lv_label_set_text(unit, " °C");
    lv_obj_set_style_text_font(unit, theme::font_body(), 0);
    lv_obj_set_style_text_color(unit, theme::color_text_secondary(), 0);

    return cont;
}

float get_temp_threshold(lv_obj_t* container) {
    if (!container) return 0.0f;
    int  int_val  = 0;
    int  frac_val = 0;
    uint32_t n = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* child = lv_obj_get_child(container, (int32_t)i);
        void* ud = lv_obj_get_user_data(child);
        if (ud == reinterpret_cast<void*>(kTagTInt))
            int_val = (int)lv_roller_get_selected(child);
        else if (ud == reinterpret_cast<void*>(kTagTFrac))
            frac_val = (int)lv_roller_get_selected(child);
    }
    return (float)int_val + (float)frac_val * 0.1f;
}

}  // namespace aqua::ui::drum_roller
