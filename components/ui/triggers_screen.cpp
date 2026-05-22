// AquaControl — Triggers settings screen implementation.
//
// ┌ Trigger List Screen ──────────────────────────────────────────────────┐
// │ Header: "Triggers"  |  "+ Add"                                        │
// │ Scrollable list: one row per trigger                                   │
// │   [●/○] Name  (type badge)   [Edit] [Delete]                          │
// └────────────────────────────────────────────────────────────────────────┘
//
// ┌ Trigger Edit/Add Screen ───────────────────────────────────────────────┐
// │ Header: "Edit Trigger"/"Add Trigger"  |  "Save"                        │
// │ Name  [textarea]                                                       │
// │ Type  (shown as static badge for existing; dropdown for new)           │
// │ —— Schedule fields ——                                                  │
// │   Start  [drum_roller::time_hhmm]                                      │
// │   Stop   [drum_roller::time_hhmm]                                      │
// │   Days   [Mon][Tue][Wed][Thu][Fri][Sat][Sun] toggle buttons            │
// │ —— Solar fields ——                                                     │
// │   Event  [Sunrise|Sunset dropdown]                                     │
// │   Offset [signed_offset roller]                                        │
// │   Duration [duration_min roller]                                       │
// │ —— Temp fields ——                                                      │
// │   Sensor  [Water|Ambient dropdown]                                     │
// │   Condition [Above|Below dropdown]                                     │
// │   Threshold [temp_threshold roller]                                    │
// │   Hysteresis [fractional_tenth roller]                                 │
// │ Linked devices  [checkboxes per device]                               │
// └────────────────────────────────────────────────────────────────────────┘

#include "triggers_screen.h"

#include <cstdio>
#include <memory>

#include "ac_logger.h"
#include "chrome.h"
#include "config_storage.h"
#include "device_manager.h"
#include "drum_roller.h"
#include "faults.h"
#include "i18n.h"
#include "schedule_trigger.h"
#include "screen_manager.h"
#include "sensor_sampler.h"
#include "solar_trigger.h"
#include "temp_map_trigger.h"
#include "temp_trigger.h"
#include "theme.h"
#include "time_manager.h"
#include "trigger_manager.h"
#include "trigger_types.h"
#include "trigger_validator.h"
#include "ui_context.h"

namespace aqua::ui::triggers_screen {

namespace {

using aqua::ui::i18n::LangKey;
using aqua::ui::i18n::tr;

constexpr const char* TAG = "TriggersScreen";

// ---------------------------------------------------------------------------
// Shared UI helpers
// ---------------------------------------------------------------------------

static lv_obj_t* make_section_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_title(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_accent(), 0);
    return lbl;
}

static lv_obj_t* make_field_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
    lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
    return lbl;
}

static lv_obj_t* make_textarea(lv_obj_t* parent, const char* placeholder,
                                const char* initial) {
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, 44);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (initial && initial[0]) lv_textarea_set_text(ta, initial);
    lv_obj_set_style_bg_color(ta, theme::color_surface_alt(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, theme::color_outline(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, theme::font_body(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, theme::color_text_primary(), LV_PART_MAIN);
    lv_obj_set_style_radius(ta, theme::RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, theme::PAD_SM, LV_PART_MAIN);
    return ta;
}

// A horizontal flex container for placing two fields side-by-side.
// Pair this with `make_pair_col()` for each child.
static lv_obj_t* make_pair_row(lv_obj_t* parent) {
    lv_obj_t* r = lv_obj_create(parent);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_column(r, theme::PAD_MD, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

// A 50%-width column used inside `make_pair_row`. Stacks its children
// vertically (label above field).
static lv_obj_t* make_pair_col(lv_obj_t* row_parent) {
    lv_obj_t* col = lv_obj_create(row_parent);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, theme::PAD_XS, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    return col;
}

static lv_color_t type_badge_color(aqua::triggers::TriggerType t) {
    switch (t) {
        case aqua::triggers::TriggerType::SCHEDULE: return theme::color_accent();
        case aqua::triggers::TriggerType::SOLAR:    return theme::color_warning();
        case aqua::triggers::TriggerType::TEMP:     return theme::color_error();
        case aqua::triggers::TriggerType::TEMP_MAP: return theme::color_accent();
        default:                                    return theme::color_text_disabled();
    }
}

static const char* type_badge_text(aqua::triggers::TriggerType t) {
    switch (t) {
        case aqua::triggers::TriggerType::SCHEDULE: return "SCHED";
        case aqua::triggers::TriggerType::SOLAR:    return "SOLAR";
        case aqua::triggers::TriggerType::TEMP:     return "TEMP";
        case aqua::triggers::TriggerType::TEMP_MAP: return "TMAP";
        default:                                    return "?";
    }
}

// ============================================================================
// Trigger Edit / Add screen
// ============================================================================

struct EditState {
    // Which trigger we are editing (nullptr = new trigger).
    aqua::triggers::ITrigger* target = nullptr;

    // Fields common to all trigger types.
    lv_obj_t* ta_name         = nullptr;
    lv_obj_t* sw_enabled      = nullptr;
    lv_obj_t* keyboard        = nullptr;
    lv_obj_t* kb_overlay      = nullptr;  // transparent dismiss layer (lv_layer_top)
    lv_obj_t* scroll_view     = nullptr;  // scrollable container (for resize on kbd show)

    // Schedule-specific.
    lv_obj_t* cont_schedule     = nullptr;
    lv_obj_t* roller_start      = nullptr;   // Mode 1: start time_hhmm container
    lv_obj_t* roller_stop       = nullptr;   // Mode 1: stop  time_hhmm container
    lv_obj_t* day_btns[7]       = {};        // Mon..Sun toggles
    // Schedule 3-mode selector.
    lv_obj_t* sched_mode_bar    = nullptr;   // lv_btnmatrix: 3 mode buttons
    int        sched_mode       = 0;         // 0=window, 1=daily_at, 2=every_n
    lv_obj_t* cont_sched[3]     = {};        // per-mode content containers
    // Mode 1 (daily_at): fields
    lv_obj_t* roller_at_time    = nullptr;   // time_hhmm for daily_at_min
    lv_obj_t* roller_m1_dur_num = nullptr;   // Mode 1 duration number (1-99)
    lv_obj_t* roller_m1_dur_unit= nullptr;   // Mode 1 duration unit roller
    // Mode 2 (every N): fields
    lv_obj_t* roller_every_num  = nullptr;   // Mode 2 interval number (1-99)
    lv_obj_t* roller_every_unit = nullptr;   // Mode 2 interval unit roller
    lv_obj_t* roller_m2_dur_num = nullptr;   // Mode 2 duration number (1-99)
    lv_obj_t* roller_m2_dur_unit= nullptr;   // Mode 2 duration unit roller
    lv_obj_t* lbl_sched_warn    = nullptr;   // overlap warning label

    // Solar-specific.
    lv_obj_t* cont_solar         = nullptr;
    lv_obj_t* sw_event           = nullptr;   // OFF=Sunrise ON=Sunset
    lv_obj_t* lbl_event          = nullptr;   // label beside sw_event ("Sunrise"/"Sunset")
    lv_obj_t* roller_offset      = nullptr;
    lv_obj_t* roller_dur         = nullptr;
    // "End at event" mode.
    lv_obj_t* sw_use_end_event   = nullptr;
    lv_obj_t* lbl_use_end_event  = nullptr;   // label beside sw_use_end_event — "Duration"/"Until event"
    lv_obj_t* cont_dur_mode      = nullptr;   // visible when sw_use_end_event OFF
    lv_obj_t* cont_end_evt_mode  = nullptr;   // visible when sw_use_end_event ON
    lv_obj_t* sw_end_event       = nullptr;   // OFF=Sunrise ON=Sunset (end event)
    lv_obj_t* lbl_end_event      = nullptr;   // label beside sw_end_event
    lv_obj_t* roller_end_offset  = nullptr;

    // Temp-specific.
    lv_obj_t* cont_temp        = nullptr;
    lv_obj_t* sw_sensor        = nullptr;   // OFF=Water ON=Ambient
    lv_obj_t* lbl_sensor       = nullptr;   // dynamic: "Water" / "Ambient"
    lv_obj_t* sw_condition     = nullptr;   // OFF=Above ON=Below
    lv_obj_t* lbl_condition    = nullptr;   // dynamic: "Above" / "Below"
    lv_obj_t* roller_thresh    = nullptr;   // temp_threshold container
    lv_obj_t* roller_hyst      = nullptr;   // fractional_tenth roller
    lv_obj_t* lbl_temp_summary = nullptr;   // live "Water > 28.5\xc2\xb0""C  \xc2\xb1""0.5\xc2\xb0""C"

    // TempMap-specific.
    lv_obj_t* cont_temp_map      = nullptr;
    lv_obj_t* sw_tmap_sensor     = nullptr;   // OFF=Water ON=Ambient
    lv_obj_t* lbl_tmap_sensor    = nullptr;
    lv_obj_t* sw_tmap_reverse    = nullptr;   // OFF=forward ON=reverse (Hi→Lo)
    lv_obj_t* roller_tmap_hyst  = nullptr;   // hysteresis_c
    lv_obj_t* roller_tmap_lo     = nullptr;   // temp_lo_c
    lv_obj_t* roller_tmap_hi     = nullptr;   // temp_hi_c

    // Linked device checkboxes (one per device, parallel array with IDs).
    std::vector<lv_obj_t*>  dev_checks;
    std::vector<uint8_t>    dev_ids;

    // Type for new triggers (add mode).
    aqua::triggers::TriggerType new_type = aqua::triggers::TriggerType::SCHEDULE;

    // Keyboard binding helpers.
    void on_focused(lv_obj_t* ta) {
        if (!keyboard) return;
        lv_keyboard_set_textarea(keyboard, ta);
        lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        // Resize viewport above the keyboard. update_layout() flushes child
        // positions before scroll_to_view so the scroll target is accurate.
        if (scroll_view) {
            lv_obj_set_size(scroll_view, LV_PCT(100), 480 - chrome::kHeaderH - 220);
            lv_obj_update_layout(scroll_view);
        }
        lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
    }
    void dismiss_keyboard() {
        if (!keyboard) return;
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard, nullptr);
        if (scroll_view)
            lv_obj_set_size(scroll_view, LV_PCT(100), 480 - chrome::kHeaderH);
        if (kb_overlay) {
            lv_obj_del(kb_overlay);
            kb_overlay = nullptr;
        }
    }
};

// Forward declaration — defined later in this file.
static void format_trigger_summary(const aqua::triggers::ITrigger& t,
                                   char* buf, size_t sz);

static void on_edit_ta_focused(lv_event_t* e) {
    auto* st = static_cast<EditState*>(lv_event_get_user_data(e));
    if (st) st->on_focused(lv_event_get_target_obj(e));
}
static void on_edit_ta_defocused(lv_event_t* e) {
    auto* st = static_cast<EditState*>(lv_event_get_user_data(e));
    if (st) st->dismiss_keyboard();
}
static void on_edit_kb_ready(lv_event_t* e) {
    auto* st = static_cast<EditState*>(lv_event_get_user_data(e));
    if (st) st->dismiss_keyboard();
}
static void bind_ta(lv_obj_t* ta, EditState* st) {
    lv_obj_add_event_cb(ta, on_edit_ta_focused,   LV_EVENT_FOCUSED,   st);
    lv_obj_add_event_cb(ta, on_edit_ta_defocused, LV_EVENT_DEFOCUSED, st);
}

// ---- Re-run trigger validator and push results into fault registry ----
static void run_validator() {
    const auto& ctx = aqua::ui::ui_context();
    if (!ctx.triggers || !ctx.devices || !ctx.sys_cfg) return;

    // Clear previous validation faults (codes 0x0200–0x023F).
    for (uint16_t i = 0; i < 64; ++i)
        aqua::faults::clear(static_cast<uint16_t>(0x0200u | i),
                            aqua::faults::Source::TRIGGER);

    // Use real hardware presence flags — previously hardcoded to false.
    bool water_ok   = aqua::sensors::get(aqua::sensors::Role::WATER).valid;
    bool ambient_ok = aqua::sensors::get(aqua::sensors::Role::AMBIENT).valid;
    bool has_time   = aqua::time_mgr::TimeManager::is_synced()
                   || (ctx.sys_cfg && ctx.sys_cfg->wifi_enabled);

    auto warnings = aqua::triggers::TriggerValidator::validate_all(
        *ctx.devices, *ctx.triggers, *ctx.sys_cfg,
        water_ok,
        ambient_ok,
        /*has_rtc=*/has_time,
        /*has_wifi=*/has_time);

    uint16_t code = 0x0200u;
    for (auto& w : warnings) {
        if (code >= 0x0240u) break;  // cap at 64
        // Only elevate WARN / ERROR_ to the fault registry; INFO stays silent.
        if (w.severity == aqua::triggers::WarningSeverity::INFO) continue;
        aqua::faults::raise(code++, aqua::faults::Source::TRIGGER,
                            w.message.c_str());
    }
}

// Forward declaration (defined later alongside build_schedule_section).
static uint16_t num_unit_to_sec(lv_obj_t* num_roller, lv_obj_t* unit_roller);

// ---- Save an edited trigger ----
static void apply_edit(EditState* st) {
    auto* tm = aqua::ui::ui_context().triggers;
    auto* dm = aqua::ui::ui_context().devices;
    if (!tm) return;

    aqua::triggers::ITrigger* trig = st->target;
    bool is_new = (trig == nullptr);

    // Pre-validate mode-2 schedule before creating a new trigger, so we
    // don't add a ghost entry to the manager when the save is blocked.
    if (is_new && st->new_type == aqua::triggers::TriggerType::SCHEDULE
               && st->sched_mode == 2) {
        uint16_t dur_s = num_unit_to_sec(st->roller_m2_dur_num, st->roller_m2_dur_unit);
        uint16_t ivl_s = num_unit_to_sec(st->roller_every_num,  st->roller_every_unit);
        if (ivl_s == 0 || dur_s >= ivl_s) return;
    }

    if (is_new) {
        uint8_t nid = 0;
        // Find first free id by trying up to 255.
        for (uint8_t i = 1; i != 0; ++i) {
            if (!tm->find(i)) { nid = i; break; }
        }
        if (nid == 0) {
            AC_LOGW(TAG, "No free trigger IDs");
            return;
        }
        std::string nm = lv_textarea_get_text(st->ta_name);
        std::unique_ptr<aqua::triggers::ITrigger> new_trig;
        switch (st->new_type) {
            case aqua::triggers::TriggerType::SCHEDULE:
                new_trig = std::make_unique<aqua::triggers::ScheduleTrigger>(nid, nm);
                break;
            case aqua::triggers::TriggerType::SOLAR:
                new_trig = std::make_unique<aqua::triggers::SolarTrigger>(nid, nm);
                break;
            case aqua::triggers::TriggerType::TEMP:
                new_trig = std::make_unique<aqua::triggers::TempTrigger>(nid, nm);
                break;
            case aqua::triggers::TriggerType::TEMP_MAP:
                new_trig = std::make_unique<aqua::triggers::TempMapTrigger>(nid, nm);
                break;
        }
        trig = tm->add(std::move(new_trig));
    }

    if (!trig) return;

    // Common fields.
    trig->name    = lv_textarea_get_text(st->ta_name);
    trig->enabled = lv_obj_has_state(st->sw_enabled, LV_STATE_CHECKED);

    // Type-specific fields.
    switch (trig->get_type()) {
        case aqua::triggers::TriggerType::SCHEDULE: {
            auto* s = static_cast<aqua::triggers::ScheduleTrigger*>(trig);
            switch (st->sched_mode) {
                case 0: {  // Start -> End window
                    s->use_interval = false;
                    s->daily_at     = false;
                    int hh, mm;
                    drum_roller::get_time_hhmm(st->roller_start, &hh, &mm);
                    s->start_min = (uint16_t)(hh * 60 + mm);
                    drum_roller::get_time_hhmm(st->roller_stop, &hh, &mm);
                    s->stop_min  = (uint16_t)(hh * 60 + mm);
                    break;
                }
                case 1: {  // Daily at time for duration
                    s->use_interval = true;
                    s->daily_at     = true;
                    int hh, mm;
                    drum_roller::get_time_hhmm(st->roller_at_time, &hh, &mm);
                    s->daily_at_min    = (uint16_t)(hh * 60 + mm);
                    s->on_duration_sec = num_unit_to_sec(st->roller_m1_dur_num,
                                                         st->roller_m1_dur_unit);
                    break;
                }
                default:  // Mode 2: Every N for duration
                case 2: {
                    uint16_t dur_sec   = num_unit_to_sec(st->roller_m2_dur_num,
                                                          st->roller_m2_dur_unit);
                    uint16_t every_sec = num_unit_to_sec(st->roller_every_num,
                                                          st->roller_every_unit);
                    if (every_sec == 0 || dur_sec >= every_sec) {
                        return;  // blocked — warning label is visible
                    }
                    s->use_interval    = true;
                    s->daily_at        = false;
                    s->interval_sec    = every_sec;
                    s->on_duration_sec = dur_sec;
                    break;
                }
            }
            // days: UI idx 0=Mon..6=Sun -> tm_wday: Mon=1..Sun=0
            static const uint8_t kTmWday[7] = {1,2,3,4,5,6,0};
            for (int i = 0; i < 7; ++i) {
                s->days[kTmWday[i]] =
                    lv_obj_has_state(st->day_btns[i], LV_STATE_CHECKED);
            }
            break;
        }
        case aqua::triggers::TriggerType::SOLAR: {
            auto* s = static_cast<aqua::triggers::SolarTrigger*>(trig);
            s->event = lv_obj_has_state(st->sw_event, LV_STATE_CHECKED)
                       ? aqua::triggers::SolarEvent::SUNSET
                       : aqua::triggers::SolarEvent::SUNRISE;
            s->offset_min     = (int16_t)drum_roller::get_signed_offset(st->roller_offset);
            s->use_end_event  = lv_obj_has_state(st->sw_use_end_event, LV_STATE_CHECKED);
            if (s->use_end_event) {
                s->end_event      = lv_obj_has_state(st->sw_end_event, LV_STATE_CHECKED)
                                    ? aqua::triggers::SolarEvent::SUNSET
                                    : aqua::triggers::SolarEvent::SUNRISE;
                s->end_offset_min = (int16_t)drum_roller::get_signed_offset(st->roller_end_offset);
            } else {
                s->duration_min   = (uint16_t)drum_roller::get_duration_min(st->roller_dur);
            }
            break;
        }
        case aqua::triggers::TriggerType::TEMP: {
            auto* t = static_cast<aqua::triggers::TempTrigger*>(trig);
            t->sensor_id   = lv_obj_has_state(st->sw_sensor, LV_STATE_CHECKED) ? 1u : 0u;
            t->condition   = lv_obj_has_state(st->sw_condition, LV_STATE_CHECKED)
                             ? aqua::triggers::TempCondition::BELOW
                             : aqua::triggers::TempCondition::ABOVE;
            t->threshold_c  = drum_roller::get_temp_threshold(st->roller_thresh);
            t->hysteresis_c = drum_roller::get_fractional_tenth(st->roller_hyst);
            break;
        }
        case aqua::triggers::TriggerType::TEMP_MAP: {
            auto* t = static_cast<aqua::triggers::TempMapTrigger*>(trig);
            t->sensor_id    = lv_obj_has_state(st->sw_tmap_sensor, LV_STATE_CHECKED) ? 1u : 0u;
            t->reverse      = lv_obj_has_state(st->sw_tmap_reverse, LV_STATE_CHECKED);
            t->hysteresis_c = drum_roller::get_fractional_tenth(st->roller_tmap_hyst);
            t->temp_lo_c    = drum_roller::get_temp_threshold(st->roller_tmap_lo);
            t->temp_hi_c    = drum_roller::get_temp_threshold(st->roller_tmap_hi);
            break;
        }
    }

    // Linked devices.
    trig->linked_device_ids.clear();
    for (size_t i = 0; i < st->dev_checks.size(); ++i) {
        if (lv_obj_has_state(st->dev_checks[i], LV_STATE_CHECKED))
            trig->linked_device_ids.push_back(st->dev_ids[i]);
    }

    // Persist.
    aqua::storage::save_triggers(*tm);
    run_validator();
    AC_LOGI(TAG, "Trigger %u saved", (unsigned)trig->id);
}

static void on_edit_save(lv_event_t* e) {
    auto* st = static_cast<EditState*>(lv_event_get_user_data(e));
    if (!st) return;
    st->dismiss_keyboard();
    apply_edit(st);
    screen_manager::pop(screen_manager::Transition::SLIDE_RIGHT);
}

static void on_edit_delete(lv_event_t* e) {
    auto* st = static_cast<EditState*>(lv_event_get_user_data(e));
    if (st && st->kb_overlay) {
        lv_obj_del(st->kb_overlay);
        st->kb_overlay = nullptr;
    }
    delete st;
}

// ---- Number+unit helpers (used by build_schedule_section) ----
// Convert seconds to the most natural unit+number for display.
// unit: 0=sec, 1=min, 2=hr
static void sec_to_num_unit(uint16_t sec, int* out_num, int* out_unit) {
    if (sec >= 3600 && sec % 3600 == 0) {
        *out_num  = (int)(sec / 3600);
        *out_unit = 2;
    } else if (sec >= 60 && sec % 60 == 0) {
        *out_num  = (int)(sec / 60);
        *out_unit = 1;
    } else {
        *out_num  = (int)sec;
        *out_unit = 0;
    }
    if (*out_num < 1)  *out_num = 1;
    if (*out_num > 99) *out_num = 99;
}

// Read a number+unit roller pair back to total seconds.
// Clamps to uint16_t max (65535) so n=99 hr doesn't overflow.
static uint16_t num_unit_to_sec(lv_obj_t* num_roller, lv_obj_t* unit_roller) {
    int n = drum_roller::get_integer_range(num_roller, 1);
    int u = (int)lv_roller_get_selected(unit_roller);
    int v;
    if (u == 2)      v = n * 3600;
    else if (u == 1) v = n * 60;
    else             v = n;
    return (uint16_t)(v > 65535 ? 65535 : v);
}

// Create a [1-99 number roller] + [sec/min/hr unit roller] side by side.
static void make_num_unit(lv_obj_t* parent, uint16_t init_sec,
                           lv_obj_t** out_num, lv_obj_t** out_unit) {
    int n, u;
    sec_to_num_unit(init_sec, &n, &u);
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    *out_num = drum_roller::integer_range(row, 1, 99, n);
    lv_obj_t* ur = lv_roller_create(row);
    char opts[32];
    snprintf(opts, sizeof(opts), "%s\n%s\n%s",
             tr(LangKey::TRG_UNIT_SEC),
             tr(LangKey::TRG_UNIT_MIN),
             tr(LangKey::TRG_UNIT_HR));
    lv_roller_set_options(ur, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(ur, (uint16_t)u, LV_ANIM_OFF);
    lv_roller_set_visible_row_count(ur, 3);
    lv_obj_set_width(ur, 72);
    lv_obj_set_style_bg_color(ur, theme::color_surface(),      LV_PART_MAIN);
    lv_obj_set_style_bg_color(ur, theme::color_surface_alt(),  LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(ur, LV_OPA_COVER,                  LV_PART_SELECTED);
    lv_obj_set_style_text_color(ur, theme::color_text_secondary(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ur, theme::color_text_primary(),   LV_PART_SELECTED);
    lv_obj_set_style_text_font(ur, theme::font_body(),             LV_PART_MAIN);
    lv_obj_set_style_text_font(ur, theme::font_body(),             LV_PART_SELECTED);
    lv_obj_set_style_border_width(ur, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(ur, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ur, theme::RADIUS_SM, LV_PART_MAIN);
    *out_unit = ur;
}

// ---- Build the type-specific section ---
static void build_schedule_section(EditState* st, lv_obj_t* scroll,
                                    aqua::triggers::ScheduleTrigger* trig) {
    // Mode 0: Start->End  (use_interval=false)
    // Mode 1: Daily at   (use_interval=true, daily_at=true)
    // Mode 2: Every N    (use_interval=true, daily_at=false)
    if (trig) {
        if (!trig->use_interval) st->sched_mode = 0;
        else if (trig->daily_at) st->sched_mode = 1;
        else                     st->sched_mode = 2;
    }

    st->cont_schedule = lv_obj_create(scroll);
    lv_obj_set_width(st->cont_schedule, LV_PCT(100));
    lv_obj_set_height(st->cont_schedule, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(st->cont_schedule, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st->cont_schedule, 0, 0);
    lv_obj_set_style_pad_all(st->cont_schedule, 0, 0);
    lv_obj_set_style_pad_row(st->cont_schedule, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(st->cont_schedule, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st->cont_schedule, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // ---- Mode selector: 3-button matrix ----
    {
        static const char* s_map[4];
        s_map[0] = tr(LangKey::TRG_SCHED_MODE_WINDOW);
        s_map[1] = tr(LangKey::TRG_SCHED_MODE_ONCE);
        s_map[2] = tr(LangKey::TRG_SCHED_MODE_EVERY);
        s_map[3] = "";
        lv_obj_t* mb = lv_btnmatrix_create(st->cont_schedule);
        lv_btnmatrix_set_map(mb, s_map);
        lv_obj_set_size(mb, LV_PCT(100), 48);
        lv_obj_set_style_bg_color(mb, theme::color_surface_alt(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(mb, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(mb, theme::color_outline(), LV_PART_MAIN);
        lv_obj_set_style_border_width(mb, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(mb, theme::RADIUS_SM, LV_PART_MAIN);
        lv_obj_set_style_pad_all(mb, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_gap(mb, 1, LV_PART_MAIN);
        lv_obj_set_style_text_font(mb, theme::font_caption(), LV_PART_ITEMS);
        lv_obj_set_style_text_color(mb, theme::color_text_secondary(), LV_PART_ITEMS);
        lv_obj_set_style_text_color(mb, theme::color_text_primary(),
                                    LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(mb, theme::color_accent(),
                                  LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(mb, LV_OPA_COVER,
                                LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(mb, theme::color_surface_alt(), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(mb, LV_OPA_COVER, LV_PART_ITEMS);
        lv_btnmatrix_set_btn_ctrl_all(mb, LV_BTNMATRIX_CTRL_CHECKABLE);
        lv_btnmatrix_set_one_checked(mb, true);
        lv_btnmatrix_set_btn_ctrl(mb, (uint16_t)st->sched_mode, LV_BTNMATRIX_CTRL_CHECKED);
        st->sched_mode_bar = mb;
        lv_obj_add_event_cb(mb, [](lv_event_t* ev) {
            auto* s = static_cast<EditState*>(lv_event_get_user_data(ev));
            uint16_t idx = lv_btnmatrix_get_selected_btn(s->sched_mode_bar);
            if (idx > 2) return;
            s->sched_mode = (int)idx;
            for (int i = 0; i < 3; ++i) {
                if (s->cont_sched[i]) {
                    if (i == (int)idx) lv_obj_remove_flag(s->cont_sched[i], LV_OBJ_FLAG_HIDDEN);
                    else               lv_obj_add_flag(s->cont_sched[i],    LV_OBJ_FLAG_HIDDEN);
                }
            }
        }, LV_EVENT_VALUE_CHANGED, st);
    }

    // ---- Mode 0: Start -> End ----
    {
        lv_obj_t* c = lv_obj_create(st->cont_schedule);
        lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        st->cont_sched[0] = c;
        if (st->sched_mode != 0) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* pair = make_pair_row(c);
        lv_obj_t* left  = make_pair_col(pair);
        lv_obj_t* right = make_pair_col(pair);
        make_field_label(left,  tr(LangKey::TRG_START_TIME));
        int start_hh = trig ? trig->start_min / 60 : 8;
        int start_mm = trig ? trig->start_min % 60 : 0;
        st->roller_start = drum_roller::time_hhmm(left, start_hh, start_mm);
        make_field_label(right, tr(LangKey::TRG_STOP_TIME));
        int stop_hh = trig ? trig->stop_min / 60 : 20;
        int stop_mm = trig ? trig->stop_min % 60 : 0;
        st->roller_stop = drum_roller::time_hhmm(right, stop_hh, stop_mm);
    }

    // ---- Mode 1: Daily at time for duration ----
    {
        lv_obj_t* c = lv_obj_create(st->cont_schedule);
        lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_style_pad_row(c, theme::PAD_SM, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        st->cont_sched[1] = c;
        if (st->sched_mode != 1) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* pair = make_pair_row(c);
        lv_obj_t* left  = make_pair_col(pair);
        lv_obj_t* right = make_pair_col(pair);
        make_field_label(left, tr(LangKey::TRG_AT_TIME));
        int at_hh = trig ? trig->daily_at_min / 60 : 8;
        int at_mm = trig ? trig->daily_at_min % 60 : 0;
        st->roller_at_time = drum_roller::time_hhmm(left, at_hh, at_mm);
        make_field_label(right, tr(LangKey::TRG_DURATION));
        uint16_t dur1_sec = trig ? trig->on_duration_sec : 300;
        make_num_unit(right, dur1_sec, &st->roller_m1_dur_num, &st->roller_m1_dur_unit);
    }

    // ---- Mode 2: Every N for duration ----
    {
        lv_obj_t* c = lv_obj_create(st->cont_schedule);
        lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_style_pad_row(c, theme::PAD_SM, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        st->cont_sched[2] = c;
        if (st->sched_mode != 2) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* pair = make_pair_row(c);
        lv_obj_t* left  = make_pair_col(pair);
        lv_obj_t* right = make_pair_col(pair);
        make_field_label(left, tr(LangKey::TRG_EVERY));
        uint16_t every_sec = trig ? trig->interval_sec : 3600;
        make_num_unit(left, every_sec, &st->roller_every_num, &st->roller_every_unit);
        make_field_label(right, tr(LangKey::TRG_DURATION));
        uint16_t dur2_sec = trig ? trig->on_duration_sec : 300;
        make_num_unit(right, dur2_sec, &st->roller_m2_dur_num, &st->roller_m2_dur_unit);

        // Overlap warning label.
        st->lbl_sched_warn = lv_label_create(c);
        lv_label_set_text(st->lbl_sched_warn, "");
        lv_obj_set_width(st->lbl_sched_warn, LV_PCT(100));
        lv_label_set_long_mode(st->lbl_sched_warn, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(st->lbl_sched_warn, theme::font_caption(), 0);
        lv_obj_set_style_text_color(st->lbl_sched_warn, theme::color_warning(), 0);

        auto check_m2 = [](lv_event_t* ev) {
            auto* s = static_cast<EditState*>(lv_event_get_user_data(ev));
            if (!s->lbl_sched_warn) return;
            uint16_t dur_s   = num_unit_to_sec(s->roller_m2_dur_num,  s->roller_m2_dur_unit);
            uint16_t every_s = num_unit_to_sec(s->roller_every_num,   s->roller_every_unit);
            lv_label_set_text(s->lbl_sched_warn,
                (every_s == 0 || dur_s >= every_s)
                    ? tr(LangKey::TRG_ERR_DUR_OVERLAP) : "");
        };
        lv_obj_add_event_cb(st->roller_m2_dur_num,  check_m2, LV_EVENT_VALUE_CHANGED, st);
        lv_obj_add_event_cb(st->roller_m2_dur_unit, check_m2, LV_EVENT_VALUE_CHANGED, st);
        lv_obj_add_event_cb(st->roller_every_num,   check_m2, LV_EVENT_VALUE_CHANGED, st);
        lv_obj_add_event_cb(st->roller_every_unit,  check_m2, LV_EVENT_VALUE_CHANGED, st);
        if (trig && trig->use_interval && !trig->daily_at &&
            trig->interval_sec > 0 && trig->on_duration_sec >= trig->interval_sec) {
            lv_label_set_text(st->lbl_sched_warn, tr(LangKey::TRG_ERR_DUR_OVERLAP));
        }
    }

    // ---- Active days ----
    make_field_label(st->cont_schedule, tr(LangKey::TRG_ACTIVE_DAYS));
    const char* kDayNames[7] = {
        tr(LangKey::DAY_MON), tr(LangKey::DAY_TUE), tr(LangKey::DAY_WED),
        tr(LangKey::DAY_THU), tr(LangKey::DAY_FRI), tr(LangKey::DAY_SAT),
        tr(LangKey::DAY_SUN) };
    static const uint8_t kTmWday[7] = {1,2,3,4,5,6,0};
    lv_obj_t* day_row = lv_obj_create(st->cont_schedule);
    lv_obj_set_size(day_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(day_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(day_row, 0, 0);
    lv_obj_set_style_pad_all(day_row, 0, 0);
    lv_obj_set_flex_flow(day_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(day_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(day_row, theme::PAD_XS, 0);

    for (int i = 0; i < 7; ++i) {
        lv_obj_t* btn = lv_btn_create(day_row);
        lv_obj_set_size(btn, 52, 40);
        lv_obj_set_style_radius(btn, theme::RADIUS_SM, 0);
        lv_obj_set_style_pad_all(btn, theme::PAD_XS, 0);
        bool active = trig ? trig->days[kTmWday[i]] : true;
        lv_obj_set_style_bg_color(btn,
            active ? theme::color_accent() : theme::color_surface_alt(), 0);
        lv_obj_set_style_bg_color(btn, theme::color_accent(), LV_STATE_CHECKED);
        if (active) lv_obj_add_state(btn, LV_STATE_CHECKED);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
            lv_obj_t* b = lv_event_get_target_obj(ev);
            bool checked = lv_obj_has_state(b, LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(b,
                checked ? theme::color_accent() : theme::color_surface_alt(), 0);
        }, LV_EVENT_VALUE_CHANGED, nullptr);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, kDayNames[i]);
        lv_obj_set_style_text_font(lbl, theme::font_caption(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_primary(), 0);
        lv_obj_center(lbl);

        st->day_btns[i] = btn;
    }
}

static void build_solar_section(EditState* st, lv_obj_t* scroll,
                                 aqua::triggers::SolarTrigger* trig) {
    using aqua::triggers::SolarEvent;
    using aqua::triggers::TriggerType;

    st->cont_solar = lv_obj_create(scroll);
    lv_obj_set_width(st->cont_solar, LV_PCT(100));
    lv_obj_set_height(st->cont_solar, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(st->cont_solar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st->cont_solar, 0, 0);
    lv_obj_set_style_pad_all(st->cont_solar, 0, 0);
    lv_obj_set_style_pad_row(st->cont_solar, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(st->cont_solar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st->cont_solar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // ── Today's solar reference info card ────────────────────────────────
    // Shows today's computed sunrise/sunset times as secondary context.
    {
        int16_t rise_min = -1, set_min = -1;
        bool    found    = false;

        if (trig && trig->valid && trig->sunrise_min_today >= 0) {
            rise_min = trig->sunrise_min_today;
            set_min  = trig->sunset_min_today;
            found    = true;
        }
        if (!found) {
            if (auto* tm = ui_context().triggers) {
                tm->for_each([&](aqua::triggers::ITrigger& t) {
                    if (!found && t.get_type() == TriggerType::SOLAR) {
                        auto& sol = static_cast<aqua::triggers::SolarTrigger&>(t);
                        if (sol.valid && sol.sunrise_min_today >= 0) {
                            rise_min = sol.sunrise_min_today;
                            set_min  = sol.sunset_min_today;
                            found    = true;
                        }
                    }
                });
            }
        }

        char rise_buf[8] = "--";  // ASCII fallback (em-dash U+2014 not in font)
        char set_buf[8]  = "--";
        if (found) {
            if (rise_min >= 0)
                snprintf(rise_buf, sizeof(rise_buf), "%02d:%02d",
                         rise_min / 60, rise_min % 60);
            if (set_min >= 0)
                snprintf(set_buf, sizeof(set_buf), "%02d:%02d",
                         set_min / 60, set_min % 60);
        }

        lv_obj_t* card = lv_obj_create(st->cont_solar);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, theme::color_surface_alt(), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, theme::color_outline(), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, theme::RADIUS_SM, 0);
        lv_obj_set_style_pad_hor(card, theme::PAD_MD, 0);
        lv_obj_set_style_pad_ver(card, theme::PAD_SM, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        char buf[48];
        snprintf(buf, sizeof(buf), "%s  %s: %s",
                 LV_SYMBOL_UP, tr(LangKey::TRG_SUNRISE), rise_buf);
        lv_obj_t* lbl_r = lv_label_create(card);
        lv_label_set_text(lbl_r, buf);
        lv_obj_set_style_text_font(lbl_r, theme::font_caption(), 0);
        lv_obj_set_style_text_color(lbl_r, theme::color_text_secondary(), 0);

        snprintf(buf, sizeof(buf), "%s  %s: %s",
                 LV_SYMBOL_DOWN, tr(LangKey::TRG_SUNSET), set_buf);
        lv_obj_t* lbl_s = lv_label_create(card);
        lv_label_set_text(lbl_s, buf);
        lv_obj_set_style_text_font(lbl_s, theme::font_caption(), 0);
        lv_obj_set_style_text_color(lbl_s, theme::color_text_secondary(), 0);
    }

    // ── Start event: Sunrise / Sunset toggle ─────────────────────────────
    {
        lv_obj_t* row = lv_obj_create(st->cont_solar);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);

        lv_obj_t* icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_UP);
        lv_obj_set_style_text_color(icon, theme::color_warning(), 0);
        lv_obj_set_style_text_font(icon, theme::font_body(), 0);

        make_field_label(row, tr(LangKey::TRG_EVENT));

        lv_obj_t* sw = lv_switch_create(row);
        bool is_sunset = trig && trig->event == SolarEvent::SUNSET;
        if (is_sunset) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, theme::color_warning(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        st->sw_event = sw;

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, is_sunset ? tr(LangKey::TRG_SUNSET)
                                         : tr(LangKey::TRG_SUNRISE));
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
        st->lbl_event = lbl;
    }

    // Start-event VALUE_CHANGED: update label + enforce mutual exclusion.
    lv_obj_add_event_cb(st->sw_event, [](lv_event_t* ev) {
        auto* s = static_cast<EditState*>(lv_event_get_user_data(ev));
        bool sunset = lv_obj_has_state(s->sw_event, LV_STATE_CHECKED);
        lv_label_set_text(s->lbl_event,
                          sunset ? tr(LangKey::TRG_SUNSET) : tr(LangKey::TRG_SUNRISE));
        if (lv_obj_has_state(s->sw_use_end_event, LV_STATE_CHECKED)) {
            if (sunset) {
                lv_obj_clear_state(s->sw_end_event, LV_STATE_CHECKED);
                lv_label_set_text(s->lbl_end_event, tr(LangKey::TRG_SUNRISE));
            } else {
                lv_obj_add_state(s->sw_end_event, LV_STATE_CHECKED);
                lv_label_set_text(s->lbl_end_event, tr(LangKey::TRG_SUNSET));
            }
        }
    }, LV_EVENT_VALUE_CHANGED, st);

    // ── End mode toggle ───────────────────────────────────────────────────
    {
        lv_obj_t* row = lv_obj_create(st->cont_solar);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);

        lv_obj_t* icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_LOOP);
        lv_obj_set_style_text_color(icon, theme::color_warning(), 0);
        lv_obj_set_style_text_font(icon, theme::font_body(), 0);

        make_field_label(row, tr(LangKey::TRG_END_MODE));

        lv_obj_t* sw = lv_switch_create(row);
        bool use_end = trig && trig->use_end_event;
        if (use_end) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, theme::color_warning(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        st->sw_use_end_event = sw;

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, use_end ? tr(LangKey::TRG_END_MODE_EVT)
                                       : tr(LangKey::TRG_END_MODE_DUR));
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
        st->lbl_use_end_event = lbl;
    }

    // ── Offset roller (left) + swappable Duration/End-offset (right) ─────
    lv_obj_t* pair = make_pair_row(st->cont_solar);

    // Left column: start-event offset (shown in both modes).
    {
        lv_obj_t* left = make_pair_col(pair);
        make_field_label(left, tr(LangKey::TRG_OFFSET_MIN));
        st->roller_offset = drum_roller::signed_offset(
            left, trig ? (int)trig->offset_min : 0);
    }

    // Right column: holds two mutually-exclusive sub-containers.
    lv_obj_t* right = make_pair_col(pair);

    // Duration sub-container (visible when end-event mode is OFF).
    {
        lv_obj_t* c = lv_obj_create(right);
        lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        st->cont_dur_mode = c;
        make_field_label(c, tr(LangKey::TRG_DURATION_MIN));
        st->roller_dur = drum_roller::duration_min(
            c, trig ? (int)trig->duration_min : 60);
        if (trig && trig->use_end_event)
            lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    }

    // End-event sub-container (visible when end-event mode is ON).
    {
        lv_obj_t* c = lv_obj_create(right);
        lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_style_pad_row(c, theme::PAD_XS, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        st->cont_end_evt_mode = c;
        if (!trig || !trig->use_end_event)
            lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);

        // End event Sunrise/Sunset toggle.
        {
            lv_obj_t* row = lv_obj_create(c);
            lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, theme::PAD_XS, 0);
            make_field_label(row, tr(LangKey::TRG_END_EVENT));
            lv_obj_t* sw = lv_switch_create(row);
            bool end_sunset = trig && trig->end_event == SolarEvent::SUNSET;
            if (end_sunset) lv_obj_add_state(sw, LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(sw, theme::color_warning(),
                                      LV_PART_INDICATOR | LV_STATE_CHECKED);
            st->sw_end_event = sw;
            lv_obj_t* lbl = lv_label_create(row);
            lv_label_set_text(lbl, end_sunset ? tr(LangKey::TRG_SUNSET)
                                              : tr(LangKey::TRG_SUNRISE));
            lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
            lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
            st->lbl_end_event = lbl;

            // End-event VALUE_CHANGED: update label + enforce mutual exclusion.
            lv_obj_add_event_cb(sw, [](lv_event_t* ev) {
                auto* s = static_cast<EditState*>(lv_event_get_user_data(ev));
                bool sunset = lv_obj_has_state(s->sw_end_event, LV_STATE_CHECKED);
                lv_label_set_text(s->lbl_end_event,
                                  sunset ? tr(LangKey::TRG_SUNSET) : tr(LangKey::TRG_SUNRISE));
                bool start_sunset = lv_obj_has_state(s->sw_event, LV_STATE_CHECKED);
                if (sunset == start_sunset) {
                    if (sunset) {
                        lv_obj_clear_state(s->sw_event, LV_STATE_CHECKED);
                        lv_label_set_text(s->lbl_event, tr(LangKey::TRG_SUNRISE));
                    } else {
                        lv_obj_add_state(s->sw_event, LV_STATE_CHECKED);
                        lv_label_set_text(s->lbl_event, tr(LangKey::TRG_SUNSET));
                    }
                }
            }, LV_EVENT_VALUE_CHANGED, st);
        }

        // End offset roller.
        make_field_label(c, tr(LangKey::TRG_END_OFFSET_MIN));
        st->roller_end_offset = drum_roller::signed_offset(
            c, trig ? (int)trig->end_offset_min : 0);
    }

    // ── sw_use_end_event toggle handler ───────────────────────────────────
    lv_obj_add_event_cb(st->sw_use_end_event, [](lv_event_t* ev) {
        auto* s = static_cast<EditState*>(lv_event_get_user_data(ev));
        bool end_mode = lv_obj_has_state(s->sw_use_end_event, LV_STATE_CHECKED);
        // Update the dynamic label beside the switch.
        lv_label_set_text(s->lbl_use_end_event,
                          end_mode ? tr(LangKey::TRG_END_MODE_EVT)
                                   : tr(LangKey::TRG_END_MODE_DUR));
        if (end_mode) {
            lv_obj_add_flag(s->cont_dur_mode, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s->cont_end_evt_mode, LV_OBJ_FLAG_HIDDEN);
            // Enforce mutual exclusion on activation: start and end must differ.
            bool start_sunset = lv_obj_has_state(s->sw_event, LV_STATE_CHECKED);
            bool end_sunset   = lv_obj_has_state(s->sw_end_event, LV_STATE_CHECKED);
            if (start_sunset == end_sunset) {
                if (start_sunset) {
                    lv_obj_clear_state(s->sw_end_event, LV_STATE_CHECKED);
                    lv_label_set_text(s->lbl_end_event, tr(LangKey::TRG_SUNRISE));
                } else {
                    lv_obj_add_state(s->sw_end_event, LV_STATE_CHECKED);
                    lv_label_set_text(s->lbl_end_event, tr(LangKey::TRG_SUNSET));
                }
            }
        } else {
            lv_obj_remove_flag(s->cont_dur_mode, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s->cont_end_evt_mode, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_VALUE_CHANGED, st);
}

// Updates the live summary label from current sensor/condition/threshold/hyst state.
static void update_temp_summary(lv_event_t* ev) {
    auto* s = static_cast<EditState*>(lv_event_get_user_data(ev));
    if (!s || !s->lbl_temp_summary) return;
    bool  ambient = lv_obj_has_state(s->sw_sensor, LV_STATE_CHECKED);
    bool  below   = lv_obj_has_state(s->sw_condition, LV_STATE_CHECKED);
    float thresh  = drum_roller::get_temp_threshold(s->roller_thresh);
    float hyst    = drum_roller::get_fractional_tenth(s->roller_hyst);
    char buf[80];
    snprintf(buf, sizeof(buf), "%s %s %.1f\xc2\xb0" "C  \xc2\xb1%.1f\xc2\xb0" "C",
             tr(ambient ? LangKey::SENSE_AMBIENT : LangKey::SENSE_WATER),
             below ? "<" : ">",
             (double)thresh, (double)hyst);
    lv_label_set_text(s->lbl_temp_summary, buf);
}

static void build_temp_section(EditState* st, lv_obj_t* scroll,
                                aqua::triggers::TempTrigger* trig) {
    st->cont_temp = lv_obj_create(scroll);
    lv_obj_set_width(st->cont_temp, LV_PCT(100));
    lv_obj_set_height(st->cont_temp, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(st->cont_temp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st->cont_temp, 0, 0);
    lv_obj_set_style_pad_all(st->cont_temp, 0, 0);
    lv_obj_set_style_pad_row(st->cont_temp, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(st->cont_temp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st->cont_temp, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Sensor toggle: OFF=Water, ON=Ambient.
    {
        bool is_ambient = trig && trig->sensor_id == 1;
        lv_obj_t* row = lv_obj_create(st->cont_temp);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);
        make_field_label(row, tr(LangKey::TRG_SENSOR));
        lv_obj_t* sw = lv_switch_create(row);
        if (is_ambient) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, theme::color_accent(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        st->sw_sensor = sw;
        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, tr(is_ambient ? LangKey::SENSE_AMBIENT : LangKey::SENSE_WATER));
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
        st->lbl_sensor = lbl;
    }
    lv_obj_add_event_cb(st->sw_sensor, [](lv_event_t* ev) {
        auto* s = static_cast<EditState*>(lv_event_get_user_data(ev));
        bool ambient = lv_obj_has_state(s->sw_sensor, LV_STATE_CHECKED);
        lv_label_set_text(s->lbl_sensor,
                          tr(ambient ? LangKey::SENSE_AMBIENT : LangKey::SENSE_WATER));
        update_temp_summary(ev);
    }, LV_EVENT_VALUE_CHANGED, st);

    // Condition toggle: OFF=Above (red switch), ON=Below (cyan switch).
    {
        bool is_below = trig && trig->condition == aqua::triggers::TempCondition::BELOW;
        lv_obj_t* row = lv_obj_create(st->cont_temp);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);
        make_field_label(row, tr(LangKey::TRG_CONDITION));
        lv_obj_t* sw = lv_switch_create(row);
        if (is_below) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw,
                                  is_below ? theme::color_accent() : theme::color_error(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        st->sw_condition = sw;
        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, tr(is_below ? LangKey::TRG_BELOW : LangKey::TRG_ABOVE));
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
        st->lbl_condition = lbl;
    }
    lv_obj_add_event_cb(st->sw_condition, [](lv_event_t* ev) {
        auto* s = static_cast<EditState*>(lv_event_get_user_data(ev));
        bool below = lv_obj_has_state(s->sw_condition, LV_STATE_CHECKED);
        lv_label_set_text(s->lbl_condition,
                          tr(below ? LangKey::TRG_BELOW : LangKey::TRG_ABOVE));
        lv_obj_set_style_bg_color(s->sw_condition,
                                  below ? theme::color_accent() : theme::color_error(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        update_temp_summary(ev);
    }, LV_EVENT_VALUE_CHANGED, st);

    // Threshold and hysteresis side-by-side.
    {
        lv_obj_t* pair = make_pair_row(st->cont_temp);
        lv_obj_t* left  = make_pair_col(pair);
        lv_obj_t* right = make_pair_col(pair);
        make_field_label(left,  tr(LangKey::TRG_THRESHOLD));
        float thresh = trig ? trig->threshold_c : 25.0f;
        st->roller_thresh = drum_roller::temp_threshold(left, thresh);
        make_field_label(right, tr(LangKey::TRG_HYSTERESIS));
        float hyst = trig ? trig->hysteresis_c : 0.5f;
        st->roller_hyst = drum_roller::fractional_tenth(right, hyst);
    }

    // Live summary: e.g. "Water > 28.5°C  ±0.5°C"
    {
        bool  ambient = trig && trig->sensor_id == 1;
        bool  below   = trig && trig->condition == aqua::triggers::TempCondition::BELOW;
        float thresh  = trig ? trig->threshold_c : 25.0f;
        float hyst    = trig ? trig->hysteresis_c : 0.5f;
        char buf[80];
        snprintf(buf, sizeof(buf), "%s %s %.1f\xc2\xb0" "C  \xc2\xb1%.1f\xc2\xb0" "C",
                 tr(ambient ? LangKey::SENSE_AMBIENT : LangKey::SENSE_WATER),
                 below ? "<" : ">",
                 (double)thresh, (double)hyst);
        lv_obj_t* lbl = lv_label_create(st->cont_temp);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_accent(), 0);
        lv_label_set_text(lbl, buf);
        st->lbl_temp_summary = lbl;
        // Attach to the two child rollers inside roller_thresh, and to roller_hyst.
        uint32_t nc = lv_obj_get_child_count(st->roller_thresh);
        for (uint32_t ci = 0; ci < nc; ++ci) {
            lv_obj_add_event_cb(lv_obj_get_child(st->roller_thresh, (int32_t)ci),
                                update_temp_summary, LV_EVENT_VALUE_CHANGED, st);
        }
        lv_obj_add_event_cb(st->roller_hyst, update_temp_summary,
                            LV_EVENT_VALUE_CHANGED, st);
    }
}

// Build the linked-device checkbox list.
// Devices are laid out in a 2-column row-wrap so we fill the 800 px width
// instead of one tall single-column scroller.
// For TEMP_MAP triggers relay devices are excluded — they cannot do analog levels.
static void build_device_links(EditState* st, lv_obj_t* scroll,
                                aqua::triggers::ITrigger* trig,
                                aqua::triggers::TriggerType type) {
    auto* dm = aqua::ui::ui_context().devices;
    if (!dm) return;

    make_section_label(scroll, tr(LangKey::TRG_LINKED_DEVICES));

    // For TEMP_MAP show a small note explaining why relay devices are absent.
    if (type == aqua::triggers::TriggerType::TEMP_MAP) {
        lv_obj_t* note = lv_label_create(scroll);
        lv_label_set_text(note, tr(LangKey::TRG_TMAP_RELAY_SKIP));
        lv_obj_set_style_text_font(note, theme::font_caption(), 0);
        lv_obj_set_style_text_color(note, theme::color_text_disabled(), 0);
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(note, LV_PCT(100));
    }

    lv_obj_t* grid = lv_obj_create(scroll);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_column(grid, theme::PAD_MD, 0);
    lv_obj_set_style_pad_row(grid, theme::PAD_XS, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    dm->for_each([&](aqua::devices::IDevice& dev) {
        // TEMP_MAP uses apply_analog(); relay devices can't do analog — skip.
        if (type == aqua::triggers::TriggerType::TEMP_MAP &&
            dev.get_type() == aqua::devices::DeviceType::RELAY) return;
        bool linked = false;
        if (trig) {
            for (uint8_t did : trig->linked_device_ids)
                if (did == dev.id) { linked = true; break; }
        }
        lv_obj_t* cb = lv_checkbox_create(grid);
        lv_checkbox_set_text(cb, dev.name.c_str());
        if (linked) lv_obj_add_state(cb, LV_STATE_CHECKED);
        // Each cell ~48 % of width so 2 per row with column gap.
        lv_obj_set_width(cb, LV_PCT(48));
        lv_obj_set_style_text_font(cb, theme::font_body(), 0);
        lv_obj_set_style_text_color(cb, theme::color_text_primary(), 0);
        lv_obj_set_style_bg_color(cb, theme::color_accent(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        st->dev_checks.push_back(cb);
        st->dev_ids.push_back(dev.id);
    });
}

static void build_temp_map_section(EditState* st, lv_obj_t* scroll,
                                    aqua::triggers::TempMapTrigger* trig) {
    st->cont_temp_map = lv_obj_create(scroll);
    lv_obj_set_width(st->cont_temp_map, LV_PCT(100));
    lv_obj_set_height(st->cont_temp_map, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(st->cont_temp_map, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st->cont_temp_map, 0, 0);
    lv_obj_set_style_pad_all(st->cont_temp_map, 0, 0);
    lv_obj_set_style_pad_row(st->cont_temp_map, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(st->cont_temp_map, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st->cont_temp_map, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // ── Explanatory info card ─────────────────────────────────────────────
    {
        lv_obj_t* card = lv_obj_create(st->cont_temp_map);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, theme::color_surface_alt(), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, theme::color_accent(), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, theme::RADIUS_SM, 0);
        lv_obj_set_style_pad_hor(card, theme::PAD_MD, 0);
        lv_obj_set_style_pad_ver(card, theme::PAD_SM, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, tr(LangKey::TRG_TMAP_INFO));
        lv_obj_set_style_text_font(lbl, theme::font_caption(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, LV_PCT(100));
    }

    // ── Sensor toggle: OFF=Water, ON=Ambient ─────────────────────────────
    {
        bool is_ambient = trig && trig->sensor_id == 1;
        lv_obj_t* row = lv_obj_create(st->cont_temp_map);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);
        make_field_label(row, tr(LangKey::TRG_SENSOR));
        lv_obj_t* sw = lv_switch_create(row);
        if (is_ambient) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, theme::color_accent(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        st->sw_tmap_sensor = sw;
        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, tr(is_ambient ? LangKey::SENSE_AMBIENT : LangKey::SENSE_WATER));
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
        st->lbl_tmap_sensor = lbl;
    }
    lv_obj_add_event_cb(st->sw_tmap_sensor, [](lv_event_t* ev) {
        auto* s = static_cast<EditState*>(lv_event_get_user_data(ev));
        bool ambient = lv_obj_has_state(s->sw_tmap_sensor, LV_STATE_CHECKED);
        lv_label_set_text(s->lbl_tmap_sensor,
                          tr(ambient ? LangKey::SENSE_AMBIENT : LangKey::SENSE_WATER));
    }, LV_EVENT_VALUE_CHANGED, st);

    // ── Reverse toggle: OFF=Lo→Hi (normal), ON=Hi→Lo (inverted) ─────────
    {
        bool is_reverse = trig && trig->reverse;
        lv_obj_t* row = lv_obj_create(st->cont_temp_map);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);
        make_field_label(row, tr(LangKey::TRG_TMAP_REVERSE));
        lv_obj_t* sw = lv_switch_create(row);
        if (is_reverse) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, theme::color_accent(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        st->sw_tmap_reverse = sw;
    }

    // ── Hysteresis — dead-band to prevent rapid cycling ──────────────────
    make_field_label(st->cont_temp_map, tr(LangKey::TRG_TMAP_HYSTERESIS));
    {
        float hyst = trig ? trig->hysteresis_c : 0.5f;
        st->roller_tmap_hyst = drum_roller::fractional_tenth(st->cont_temp_map, hyst);
    }

    // ── Temperature range side-by-side (Lo on left, Hi on right) ─────────
    {
        lv_obj_t* pair = make_pair_row(st->cont_temp_map);
        lv_obj_t* left  = make_pair_col(pair);
        lv_obj_t* right = make_pair_col(pair);
        make_field_label(left,  tr(LangKey::TRG_TMAP_TEMP_LO));
        float lo = trig ? trig->temp_lo_c : 20.0f;
        st->roller_tmap_lo = drum_roller::temp_threshold(left, lo);
        make_field_label(right, tr(LangKey::TRG_TMAP_TEMP_HI));
        float hi = trig ? trig->temp_hi_c : 30.0f;
        st->roller_tmap_hi = drum_roller::temp_threshold(right, hi);
    }
}

// ---- Build the edit screen ----
static lv_obj_t* build_edit_screen(aqua::triggers::ITrigger* trig,
                                    aqua::triggers::TriggerType new_type) {
    auto* st = new EditState();
    st->target   = trig;
    st->new_type = new_type;

    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, on_edit_delete, LV_EVENT_DELETE, st);

    const char* title = trig ? tr(LangKey::TRG_EDIT_TITLE) : tr(LangKey::TRG_ADD_TITLE);
    chrome::Header hdr =
        chrome::build(root, title, chrome::pop_on_back, tr(LangKey::BTN_SAVE), on_edit_save);
    // chrome::build adds on_edit_save with user_data=nullptr. Replace it so
    // the callback receives our EditState pointer.
    if (hdr.btn_action) {
        lv_obj_remove_event_cb(hdr.btn_action, on_edit_save);
        lv_obj_add_event_cb(hdr.btn_action, on_edit_save, LV_EVENT_CLICKED, st);
    }

    lv_obj_t* scroll = lv_obj_create(root);
    lv_obj_set_size(scroll, LV_PCT(100), 480 - chrome::kHeaderH);
    // TOP anchor: when height shrinks (keyboard visible) the top edge stays
    // pinned below the header.  BOTTOM_MID would slide the container DOWN.
    lv_obj_align(scroll, LV_ALIGN_TOP_MID, 0, chrome::kHeaderH);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, theme::PAD_MD, 0);
    lv_obj_set_style_pad_row(scroll, theme::PAD_SM, 0);
    st->scroll_view = scroll;
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Name — generate a default name for new triggers.
    char default_name[24] = {};
    if (!trig) {
        size_t count = 0;
        if (auto* tm = ui_context().triggers) {
            tm->for_each([&](aqua::triggers::ITrigger& t) {
                if (t.get_type() == new_type) ++count;
            });
        }
        const char* prefix =
            new_type == aqua::triggers::TriggerType::SCHEDULE ? "Sched" :
            new_type == aqua::triggers::TriggerType::SOLAR    ? "Solar" : "Temp";
        snprintf(default_name, sizeof(default_name), "%s %zu", prefix, count + 1);
    }
    make_field_label(scroll, tr(LangKey::TRG_NAME));
    st->ta_name = make_textarea(scroll, tr(LangKey::TRG_NAME_PH),
                                trig ? trig->name.c_str() : default_name);
    bind_ta(st->ta_name, st);

    // Enabled toggle.
    {
        lv_obj_t* row = lv_obj_create(scroll);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);
        make_field_label(row, tr(LangKey::DEV_ENABLED));
        st->sw_enabled = lv_switch_create(row);
        bool en = trig ? trig->enabled : true;
        if (en) lv_obj_add_state(st->sw_enabled, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(st->sw_enabled, theme::color_success(),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
    }

    // Type badge.
    aqua::triggers::TriggerType typ = trig ? trig->get_type() : new_type;
    {
        char badge_buf[32];
        snprintf(badge_buf, sizeof(badge_buf), "Type: %s", type_badge_text(typ));
        lv_obj_t* lbl = lv_label_create(scroll);
        lv_label_set_text(lbl, badge_buf);
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, type_badge_color(typ), 0);
    }

    // Type-specific section.
    switch (typ) {
        case aqua::triggers::TriggerType::SCHEDULE:
            build_schedule_section(st, scroll,
                trig ? static_cast<aqua::triggers::ScheduleTrigger*>(trig) : nullptr);
            break;
        case aqua::triggers::TriggerType::SOLAR:
            build_solar_section(st, scroll,
                trig ? static_cast<aqua::triggers::SolarTrigger*>(trig) : nullptr);
            break;
        case aqua::triggers::TriggerType::TEMP:
            build_temp_section(st, scroll,
                trig ? static_cast<aqua::triggers::TempTrigger*>(trig) : nullptr);
            break;
        case aqua::triggers::TriggerType::TEMP_MAP:
            build_temp_map_section(st, scroll,
                trig ? static_cast<aqua::triggers::TempMapTrigger*>(trig) : nullptr);
            break;
    }

    // Linked devices (relay excluded for TEMP_MAP).
    build_device_links(st, scroll, trig, typ);

    // Keyboard.
    st->keyboard = lv_keyboard_create(root);
    lv_obj_set_style_bg_color(st->keyboard, theme::color_surface(), 0);
    lv_obj_set_style_bg_color(st->keyboard, theme::color_surface_alt(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(st->keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(st->keyboard, theme::color_text_primary(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(st->keyboard, theme::font_body(), LV_PART_ITEMS);
    lv_obj_set_style_border_color(st->keyboard, theme::color_outline(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(st->keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(st->keyboard, theme::color_accent(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(st->keyboard, theme::color_background(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_flag(st->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(st->keyboard, on_edit_kb_ready, LV_EVENT_READY,  st);
    lv_obj_add_event_cb(st->keyboard, on_edit_kb_ready, LV_EVENT_CANCEL, st);

    return root;
}

// ============================================================================
// Add trigger — type picker
// ============================================================================

struct AddPickState {
    // nothing needed beyond stack allocation
};

static void on_add_pick_delete(lv_event_t* e) {
    delete static_cast<AddPickState*>(lv_event_get_user_data(e));
}

struct TypePickCtx { aqua::triggers::TriggerType type; };

static void on_type_picked(lv_event_t* e) {
    auto* ctx = static_cast<TypePickCtx*>(lv_event_get_user_data(e));
    if (!ctx) return;
    aqua::triggers::TriggerType t = ctx->type;
    screen_manager::pop(screen_manager::Transition::SLIDE_RIGHT);
    lv_obj_t* edit_scr = build_edit_screen(nullptr, t);
    screen_manager::push(edit_scr, screen_manager::Transition::SLIDE_LEFT);
}

static lv_obj_t* build_add_type_picker() {
    auto* st = new AddPickState();

    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, on_add_pick_delete, LV_EVENT_DELETE, st);

    chrome::build(root, tr(LangKey::TRG_ADD_TITLE), chrome::pop_on_back);

    lv_obj_t* cont = lv_obj_create(root);
    lv_obj_set_size(cont, LV_PCT(100), 480 - chrome::kHeaderH);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, theme::PAD_LG, 0);
    lv_obj_set_style_pad_row(cont, theme::PAD_MD, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* hdr = lv_label_create(cont);
    lv_label_set_text(hdr, tr(LangKey::TRG_SELECT_TYPE));
    lv_obj_set_style_text_font(hdr, theme::font_title(), 0);
    lv_obj_set_style_text_color(hdr, theme::color_text_primary(), 0);

    static TypePickCtx kSched   = {aqua::triggers::TriggerType::SCHEDULE};
    static TypePickCtx kSolar   = {aqua::triggers::TriggerType::SOLAR};
    static TypePickCtx kTemp    = {aqua::triggers::TriggerType::TEMP};
    static TypePickCtx kTempMap = {aqua::triggers::TriggerType::TEMP_MAP};

    struct BtnDesc { const char* lbl; TypePickCtx* ctx; lv_color_t col; };
    BtnDesc descs[] = {
        { LV_SYMBOL_LIST    " Schedule",          &kSched,   theme::color_accent()  },
        { LV_SYMBOL_REFRESH " Solar",             &kSolar,   theme::color_warning() },
        { LV_SYMBOL_WARNING " Temperature",       &kTemp,    theme::color_error()   },
        { LV_SYMBOL_SETTINGS" Temp \xe2\x86\x92 Analog", &kTempMap, theme::color_accent()  },
    };

    for (auto& d : descs) {
        lv_obj_t* btn = lv_btn_create(cont);
        lv_obj_set_size(btn, LV_PCT(80), 60);
        lv_obj_set_style_bg_color(btn, theme::color_surface_alt(), 0);
        lv_obj_set_style_border_color(btn, d.col, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_radius(btn, theme::RADIUS_MD, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, d.lbl);
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, d.col, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, on_type_picked, LV_EVENT_CLICKED, d.ctx);
    }

    return root;
}

// ============================================================================
// Trigger list screen
// ============================================================================

struct ListRowCtx {
    uint8_t trigger_id;
};

static void on_list_row_edit(lv_event_t* e) {
    auto* ctx = static_cast<ListRowCtx*>(lv_event_get_user_data(e));
    if (!ctx) return;
    auto* tm = aqua::ui::ui_context().triggers;
    if (!tm) return;
    auto* trig = tm->find(ctx->trigger_id);
    if (!trig) return;
    lv_obj_t* s = build_edit_screen(trig,
        aqua::triggers::TriggerType::SCHEDULE /*unused*/);
    screen_manager::push(s, screen_manager::Transition::SLIDE_LEFT);
}

static void on_list_row_delete(lv_event_t* e) {
    auto* ctx = static_cast<ListRowCtx*>(lv_event_get_user_data(e));
    if (!ctx) return;
    // Confirmation msgbox.
    lv_obj_t* mb = lv_msgbox_create(lv_layer_top());
    lv_msgbox_add_title(mb, tr(LangKey::TRG_DELETE_TITLE));
    lv_msgbox_add_text(mb, tr(LangKey::TRG_DELETE_CONFIRM));

    struct DelCtx { uint8_t id; lv_obj_t* mb; };
    auto* dc = new DelCtx{ctx->trigger_id, mb};

    lv_obj_t* cancel  = lv_msgbox_add_footer_button(mb, tr(LangKey::BTN_CANCEL));
    lv_obj_t* confirm = lv_msgbox_add_footer_button(mb, tr(LangKey::BTN_DELETE));
    lv_obj_set_style_bg_color(confirm, theme::color_error(), 0);

    lv_obj_add_event_cb(cancel, [](lv_event_t* ev) {
        auto* c = static_cast<DelCtx*>(lv_event_get_user_data(ev));
        lv_msgbox_close(c->mb);
        delete c;
    }, LV_EVENT_CLICKED, dc);

    lv_obj_add_event_cb(confirm, [](lv_event_t* ev) {
        auto* c = static_cast<DelCtx*>(lv_event_get_user_data(ev));
        auto* tm2 = aqua::ui::ui_context().triggers;
        if (tm2) {
            tm2->remove(c->id);
            aqua::storage::save_triggers(*tm2);
            run_validator();
        }
        lv_msgbox_close(c->mb);
        delete c;
        // Rebuild the list by popping then re-pushing the triggers screen.
        // Simpler: pop back to settings, user re-enters. Since we are
        // already on the list screen, just rebuild in place.
        // Easiest: pop to refresh — but list screen IS current. So
        // recreate the list contents via a deferred call:
        lv_async_call([](void*) {
            // Pop and re-push our own screen.
            screen_manager::pop(screen_manager::Transition::NONE);
            screen_manager::push(aqua::ui::triggers_screen::build(),
                                 screen_manager::Transition::NONE);
        }, nullptr);
    }, LV_EVENT_CLICKED, dc);
}

static void on_list_row_ctx_delete(lv_event_t* e) {
    delete static_cast<ListRowCtx*>(lv_event_get_user_data(e));
}

static void on_add_btn(lv_event_t* /*e*/) {
    screen_manager::push(build_add_type_picker(), screen_manager::Transition::SLIDE_LEFT);
}

static void on_list_screen_delete(lv_event_t* /*e*/) {
    // Nothing to free (all sub-contexts freed by their own LV_EVENT_DELETE).
}

// Populate (or re-populate) the trigger list inside the scrollable container.
// Called from build() on creation and from LV_EVENT_SCREEN_LOADED on return.
static void populate_trigger_list(lv_obj_t* scroll) {
    lv_obj_clean(scroll);

    auto* tm = aqua::ui::ui_context().triggers;
    if (!tm) {
        lv_obj_t* lbl = lv_label_create(scroll);
        lv_label_set_text(lbl, tr(LangKey::TRG_NO_MGR));
        lv_obj_set_style_text_color(lbl, theme::color_text_disabled(), 0);
        return;
    }

    size_t shown = 0;
    tm->for_each([&](aqua::triggers::ITrigger& t) {
        lv_obj_t* row = lv_obj_create(scroll);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, theme::color_surface(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, theme::color_outline(), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, theme::RADIUS_SM, 0);
        lv_obj_set_style_pad_all(row, theme::PAD_SM, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, theme::PAD_SM, 0);

        lv_obj_t* status_col = lv_obj_create(row);
        lv_obj_set_size(status_col, 36, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(status_col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(status_col, 0, 0);
        lv_obj_set_style_pad_all(status_col, 0, 0);
        lv_obj_set_style_pad_row(status_col, 2, 0);
        lv_obj_set_flex_flow(status_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(status_col, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(status_col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* dot = lv_label_create(status_col);
        lv_label_set_text(dot, t.enabled ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_font(dot, theme::font_body(), 0);
        lv_obj_set_style_text_color(dot,
            t.enabled ? theme::color_success() : theme::color_text_disabled(), 0);

        const bool active_now = t.last_state_.load(std::memory_order_relaxed);
        lv_obj_t* act = lv_label_create(status_col);
        lv_label_set_text(act, active_now ? LV_SYMBOL_PLAY : LV_SYMBOL_STOP);
        lv_obj_set_style_text_font(act, theme::font_caption(), 0);
        lv_obj_set_style_text_color(act,
            active_now ? theme::color_accent() : theme::color_text_disabled(), 0);

        lv_obj_t* name_col = lv_obj_create(row);
        lv_obj_set_flex_grow(name_col, 1);
        lv_obj_set_height(name_col, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(name_col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(name_col, 0, 0);
        lv_obj_set_style_pad_all(name_col, 0, 0);
        lv_obj_set_style_pad_row(name_col, 2, 0);
        lv_obj_set_flex_flow(name_col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(name_col, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(name_col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* top_row = lv_obj_create(name_col);
        lv_obj_set_size(top_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(top_row, 0, 0);
        lv_obj_set_style_pad_all(top_row, 0, 0);
        lv_obj_set_style_pad_column(top_row, theme::PAD_SM, 0);
        lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl_name = lv_label_create(top_row);
        lv_label_set_text(lbl_name, t.name.c_str());
        lv_obj_set_style_text_font(lbl_name, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl_name, theme::color_text_primary(), 0);

        lv_obj_t* lbl_type = lv_label_create(top_row);
        lv_label_set_text(lbl_type, type_badge_text(t.get_type()));
        lv_obj_set_style_text_font(lbl_type, theme::font_caption(), 0);
        lv_obj_set_style_text_color(lbl_type, type_badge_color(t.get_type()), 0);
        lv_obj_set_style_bg_color(lbl_type, theme::color_surface_alt(), 0);
        lv_obj_set_style_bg_opa(lbl_type, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(lbl_type, theme::RADIUS_SM, 0);
        lv_obj_set_style_pad_hor(lbl_type, theme::PAD_XS, 0);
        lv_obj_set_style_pad_ver(lbl_type, 2, 0);

        char summary[96];
        format_trigger_summary(t, summary, sizeof(summary));
        // Append linked device count.
        {
            char dev_info[32];
            size_t dc = t.linked_device_ids.size();
            snprintf(dev_info, sizeof(dev_info), "  |  %u device%s",
                     (unsigned)dc, dc == 1 ? "" : "s");
            strncat(summary, dev_info, sizeof(summary) - strlen(summary) - 1);
        }
        lv_obj_t* lbl_sum = lv_label_create(name_col);
        lv_label_set_text(lbl_sum, summary);
        lv_obj_set_style_text_font(lbl_sum, theme::font_caption(), 0);
        lv_obj_set_style_text_color(lbl_sum, theme::color_text_secondary(), 0);
        lv_obj_set_width(lbl_sum, LV_PCT(100));
        lv_label_set_long_mode(lbl_sum, LV_LABEL_LONG_DOT);

        auto* ctx = new ListRowCtx{t.id};

        lv_obj_t* btn_edit = lv_btn_create(row);
        lv_obj_set_size(btn_edit, 80, 44);
        lv_obj_set_style_bg_color(btn_edit, theme::color_surface_alt(), 0);
        lv_obj_set_style_radius(btn_edit, theme::RADIUS_SM, 0);
        lv_obj_t* lbl_edit = lv_label_create(btn_edit);
        lv_label_set_text(lbl_edit, tr(LangKey::BTN_EDIT));
        lv_obj_set_style_text_font(lbl_edit, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl_edit, theme::color_text_primary(), 0);
        lv_obj_center(lbl_edit);
        lv_obj_add_event_cb(btn_edit, on_list_row_edit,       LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(btn_edit, on_list_row_ctx_delete, LV_EVENT_DELETE,  ctx);

        lv_obj_t* btn_del = lv_btn_create(row);
        lv_obj_set_size(btn_del, 44, 44);
        lv_obj_set_style_bg_color(btn_del, theme::color_error(), 0);
        lv_obj_set_style_radius(btn_del, theme::RADIUS_SM, 0);
        lv_obj_t* lbl_del = lv_label_create(btn_del);
        lv_label_set_text(lbl_del, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_font(lbl_del, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl_del, theme::color_text_primary(), 0);
        lv_obj_center(lbl_del);
        auto* del_ctx = new ListRowCtx{t.id};
        lv_obj_add_event_cb(btn_del, on_list_row_delete,      LV_EVENT_CLICKED, del_ctx);
        lv_obj_add_event_cb(btn_del, on_list_row_ctx_delete,  LV_EVENT_DELETE,  del_ctx);

        ++shown;
    }, false);

    if (shown == 0) {
        lv_obj_t* lbl = lv_label_create(scroll);
        lv_label_set_text(lbl, tr(LangKey::TRG_NONE_YET));
        lv_obj_set_style_text_font(lbl, theme::font_body(), 0);
        lv_obj_set_style_text_color(lbl, theme::color_text_secondary(), 0);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    }
}

static void on_list_screen_loaded(lv_event_t* e) {
    auto* scroll = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    if (scroll) populate_trigger_list(scroll);
}

// Build a human-readable single-line summary of a trigger's settings.
// Used in the list rows so the user can see what each trigger does at a
// glance without having to enter the edit screen.
static void format_trigger_summary(const aqua::triggers::ITrigger& t,
                                    char* buf, size_t buf_sz) {
    using namespace aqua::triggers;
    if (buf_sz == 0) return;
    buf[0] = '\0';
    switch (t.get_type()) {
        case TriggerType::SCHEDULE: {
            const auto& s = static_cast<const ScheduleTrigger&>(t);
            // Days summary: show "Daily", "Weekdays", "Weekends" or a
            // comma-separated list of short names (Mon, Tue, …).
            // s.days indexed 0=Sun..6=Sat; UI order Mon..Sun.
            static const uint8_t kTmWday[7] = {1,2,3,4,5,6,0};
            int n_active = 0;
            for (int i = 0; i < 7; ++i) if (s.days[kTmWday[i]]) ++n_active;

            char days[48] = {};
            if (n_active == 0) {
                snprintf(days, sizeof(days), "-");
            } else if (n_active == 7) {
                snprintf(days, sizeof(days), "%s", tr(LangKey::TRG_ACTIVE_DAYS));
            } else {
                // short names, e.g. "Mon Wed Fri"
                static const LangKey kDayKeys[7] = {
                    LangKey::DAY_MON, LangKey::DAY_TUE, LangKey::DAY_WED,
                    LangKey::DAY_THU, LangKey::DAY_FRI, LangKey::DAY_SAT,
                    LangKey::DAY_SUN
                };
                size_t pos = 0;
                bool first = true;
                for (int i = 0; i < 7 && pos < sizeof(days) - 1; ++i) {
                    if (!s.days[kTmWday[i]]) continue;
                    if (!first) {
                        days[pos++] = ' ';
                    }
                    first = false;
                    const char* dn = tr(kDayKeys[i]);
                    while (*dn && pos < sizeof(days) - 1) days[pos++] = *dn++;
                }
                days[pos] = '\0';
            }
            snprintf(buf, buf_sz, "%02u:%02u-%02u:%02u  %s",
                     (unsigned)(s.start_min / 60), (unsigned)(s.start_min % 60),
                     (unsigned)(s.stop_min  / 60), (unsigned)(s.stop_min  % 60),
                     days);
            break;
        }
        case TriggerType::SOLAR: {
            const auto& s = static_cast<const SolarTrigger&>(t);
            const char* ev = (s.event == SolarEvent::SUNSET) ? "Sunset" : "Sunrise";
            snprintf(buf, buf_sz, "%s %+d min, dur %u min",
                     ev, (int)s.offset_min, (unsigned)s.duration_min);
            break;
        }
        case TriggerType::TEMP: {
            const auto& s = static_cast<const TempTrigger&>(t);
            const char* sensor = s.sensor_id == 1 ? "Amb" : "Water";
            const char* cmp = (s.condition == TempCondition::BELOW) ? "<" : ">";
            snprintf(buf, buf_sz, "%s %s %.1f\xC2\xB0""C  hyst %.1f",
                     sensor, cmp, (double)s.threshold_c, (double)s.hysteresis_c);
            break;
        }
        case TriggerType::TEMP_MAP: {
            const auto& s = static_cast<const TempMapTrigger&>(t);
            const char* sensor = s.sensor_id == 1 ? "Amb" : "Water";
            snprintf(buf, buf_sz, "%s %.1f\xC2\xB0""C \xe2\x86\x92 %.1f\xC2\xB0""C",
                     sensor, (double)s.temp_lo_c, (double)s.temp_hi_c);
            break;
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// build() — public
// ---------------------------------------------------------------------------

lv_obj_t* build() {
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, on_list_screen_delete, LV_EVENT_DELETE, nullptr);

    chrome::build(root, tr(LangKey::NAV_TRIGGERS), chrome::pop_on_back, tr(LangKey::TRG_ADD), on_add_btn);

    lv_obj_t* scroll = lv_obj_create(root);
    lv_obj_set_size(scroll, LV_PCT(100), 480 - chrome::kHeaderH);
    lv_obj_align(scroll, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, theme::PAD_SM, 0);
    lv_obj_set_style_pad_row(scroll, theme::PAD_SM, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Rebuild list every time this screen becomes visible (e.g. after
    // returning from the edit screen) — Issue #6.
    lv_obj_add_event_cb(root, on_list_screen_loaded, LV_EVENT_SCREEN_LOADED, scroll);

    populate_trigger_list(scroll);
    return root;
}

}  // namespace aqua::ui::triggers_screen
