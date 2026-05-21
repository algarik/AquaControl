// AquaControl — Drum-roller numeric input widget (Phase 4, Slice 6)
//
// Wraps LVGL's lv_roller to provide ready-made pickers for the common
// input types used throughout the settings screens and the wizard:
//
//   DrumRoller::time_hhmm()     — side-by-side HH  :  MM pickers
//   DrumRoller::duration_min()  — single roller in minutes (1–360)
//   DrumRoller::percent()       — 0–100 % in 1 % steps
//   DrumRoller::signed_offset() — –360…+360 minutes offset for solar
//   DrumRoller::integer()       — arbitrary [min..max] step 1
//   DrumRoller::fractional()    — 0.0–5.0 in 0.1 steps (hysteresis etc.)
//
// Callers own the returned container lv_obj_t. Getters read the current
// roller positions synchronously (no callbacks needed for simple use).
//
// Thread safety: must be called from Core 1 with LVGL lock held.
#pragma once

#include <cstdint>

#include "lvgl.h"

namespace aqua::ui::drum_roller {

// ---- HH:MM time picker --------------------------------------------------
// Returns a container holding two side-by-side rollers.
// Use get_time_hhmm() to read the selected {hh, mm}.
lv_obj_t* time_hhmm(lv_obj_t* parent, int init_hh = 0, int init_mm = 0);
void      get_time_hhmm(lv_obj_t* container, int* out_hh, int* out_mm);

// ---- Duration in minutes ------------------------------------------------
// Roller values: 1, 2, 3 … 60 min, then 90, 120, 150 … 360 min.
// Use get_duration_min() to read the selected value in minutes.
lv_obj_t* duration_min(lv_obj_t* parent, int init_min = 5);
int       get_duration_min(lv_obj_t* roller);

// ---- Percent (0–100) ----------------------------------------------------
lv_obj_t* percent(lv_obj_t* parent, int init_pct = 100);
int       get_percent(lv_obj_t* roller);

// ---- Signed offset in minutes (–360 … +360, step 5) --------------------
lv_obj_t* signed_offset(lv_obj_t* parent, int init_min = 0);
int       get_signed_offset(lv_obj_t* roller);

// ---- Generic integer range (min..max, step 1) ---------------------------
// Label format: just the number as a string.
lv_obj_t* integer_range(lv_obj_t* parent, int lo, int hi, int init);
int       get_integer_range(lv_obj_t* roller, int lo);

// ---- Fractional 0.0–5.0 in 0.1 steps -----------------------------------
// Displayed as "0.0", "0.1" … "5.0".
lv_obj_t* fractional_tenth(lv_obj_t* parent, float init = 0.5f);
float     get_fractional_tenth(lv_obj_t* roller);

// ---- Temperature threshold: integer roller + tenths roller side-by-side -
// Returns container; use get_temp_threshold() to read the float value.
lv_obj_t* temp_threshold(lv_obj_t* parent, float init_c = 25.0f,
                          float lo = 0.0f, float hi = 40.0f);
float     get_temp_threshold(lv_obj_t* container);

}  // namespace aqua::ui::drum_roller
