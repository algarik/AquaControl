#include "boot_screen.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "ac_logger.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i18n.h"
#include "lvgl.h"
#include "theme.h"

namespace aqua::ui {

static const char* TAG = "BootScreen";

static lv_obj_t* s_screen     = nullptr;
static lv_obj_t* s_log_label  = nullptr;
static lv_obj_t* s_title_label = nullptr;
static lv_obj_t* s_pause_label = nullptr;  // "PAUSED — tap to resume"

// Pause state (set/cleared from LVGL task via touch callback).
static std::atomic<bool> s_paused{false};

// Buffer holding the full boot log text. Grown line by line.
static constexpr size_t LOG_BUF_SIZE = 6144;
static char s_log_buf[LOG_BUF_SIZE] = {0};
static size_t s_log_used = 0;

static void append_to_buffer(const char* text) {
    size_t len = strlen(text);
    if (s_log_used + len + 1 >= LOG_BUF_SIZE) {
        // Drop oldest ~25% to make room (keep boot log scrolling)
        size_t drop = LOG_BUF_SIZE / 4;
        memmove(s_log_buf, s_log_buf + drop, s_log_used - drop + 1);
        s_log_used -= drop;
    }
    memcpy(s_log_buf + s_log_used, text, len + 1);
    s_log_used += len;
}

void boot_screen_show() {
    if (!lvgl_port_lock(200)) {
        AC_LOGE(TAG, "Could not acquire LVGL lock");
        return;
    }

    // Override the LVGL default theme font to roboto_14 (Roboto Regular with
    // full Cyrillic + icon glyphs). LVGL auto-initialises the theme with
    // lv_font_montserrat_14 (Latin-only), so any widget that inherits the
    // default font shows empty boxes for Cyrillic characters.  Calling
    // lv_theme_default_init here — before any widget is created — replaces
    // font_small/normal/large with our roboto_14, fixing all inheriting
    // widgets (keyboard LV_PART_ITEMS, textarea cursors, dropdown lists, etc.)
    lv_theme_default_init(lv_display_get_default(),
                          lv_palette_main(LV_PALETTE_BLUE),
                          lv_palette_main(LV_PALETTE_RED),
                          true,                   // dark theme
                          theme::font_caption());  // roboto_14 with Cyrillic

    // lv_layer_top() is created by LVGL before the theme is initialised, so
    // it is never passed through the theme apply callback and its text font
    // stays as LV_FONT_DEFAULT (Montserrat 14, Latin-only).  Any msgbox or
    // other widget parented to lv_layer_top() would therefore inherit the
    // wrong font and show empty boxes for Cyrillic text.  Setting it here
    // explicitly fixes inheritance for all pop-up overlays.
    lv_obj_set_style_text_font(lv_layer_top(), theme::font_caption(), 0);

    s_screen = lv_obj_create(nullptr);
    lv_obj_add_event_cb(s_screen,
        [](lv_event_t*) {
            s_screen = nullptr;
            s_log_label = nullptr;
            s_title_label = nullptr;
            s_pause_label = nullptr;
            s_paused.store(false);
        },
        LV_EVENT_DELETE, nullptr);

    // Tap anywhere to toggle pause.
    lv_obj_add_event_cb(s_screen, [](lv_event_t*) {
        bool was_paused = s_paused.load();
        s_paused.store(!was_paused);
        if (s_pause_label) {
            if (!was_paused) {
                // Now paused — show indicator.
                lv_label_set_text(s_pause_label, LV_SYMBOL_PAUSE "  PAUSED — tap to resume");
                lv_obj_set_style_text_color(s_pause_label, theme::color_warning(), 0);
                lv_obj_clear_flag(s_pause_label, LV_OBJ_FLAG_HIDDEN);
            } else {
                // Resuming.
                lv_obj_add_flag(s_pause_label, LV_OBJ_FLAG_HIDDEN);
                // Flush any buffered lines immediately.
                if (s_log_label) lv_label_set_text(s_log_label, s_log_buf);
            }
        }
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_bg_color(s_screen, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, theme::PAD_MD, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Title card -------------------------------------------------------
    lv_obj_t* card = lv_obj_create(s_screen);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, theme::RADIUS_LG, 0);
    lv_obj_set_style_border_color(card, theme::color_accent(), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_pad_all(card, theme::PAD_MD, 0);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, theme::PAD_SM, 0);

    // Accent dot indicator
    lv_obj_t* dot = lv_obj_create(card);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, theme::color_accent(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);

    s_title_label = lv_label_create(card);
    lv_label_set_text(s_title_label, "AquaControl  —  booting...");
    lv_obj_set_style_text_color(s_title_label, theme::color_accent(), 0);
    lv_obj_set_style_text_font(s_title_label, theme::font_title(), 0);

    // ---- Log area ---------------------------------------------------------
    s_log_label = lv_label_create(s_screen);
    lv_label_set_long_mode(s_log_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_log_label, LV_PCT(100));
    lv_obj_set_style_text_color(s_log_label, theme::color_success(), 0);
    lv_obj_set_style_text_font(s_log_label, theme::font_small(), 0);
    lv_obj_align_to(s_log_label, card, LV_ALIGN_OUT_BOTTOM_LEFT, 0, theme::PAD_MD);
    lv_label_set_text(s_log_label, "");

    // ---- Pause indicator (hidden by default) ------------------------------
    s_pause_label = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_pause_label, theme::font_body(), 0);
    lv_obj_set_style_text_color(s_pause_label, theme::color_warning(), 0);
    lv_obj_align(s_pause_label, LV_ALIGN_BOTTOM_MID, 0, -theme::PAD_MD);
    lv_label_set_text(s_pause_label, LV_SYMBOL_PAUSE "  PAUSED — tap to resume");
    lv_obj_add_flag(s_pause_label, LV_OBJ_FLAG_HIDDEN);

    // ---- Hint (bottom-right) ----------------------------------------------
    lv_obj_t* hint = lv_label_create(s_screen);
    lv_obj_set_style_text_font(hint, theme::font_caption(), 0);
    lv_obj_set_style_text_color(hint, theme::color_text_secondary(), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_RIGHT, -theme::PAD_MD, -theme::PAD_SM);
    lv_label_set_text(hint, aqua::ui::i18n::tr(aqua::ui::i18n::LangKey::BOOT_TAP_PAUSE));

    lv_scr_load(s_screen);
    lvgl_port_unlock();

    AC_LOGI(TAG, "Boot screen shown");
}

void boot_screen_log(const char* line, const char* marker) {
    if (line == nullptr) return;

    char formatted[160];
    if (marker != nullptr) {
        // Right-pad message to align the marker (simple fixed-width style)
        snprintf(formatted, sizeof(formatted), "%-56.56s [ %s ]\n", line, marker);
    } else {
        snprintf(formatted, sizeof(formatted), "%s\n", line);
    }

    // Always echo to serial so we see boot progress even before the panel is up
    AC_LOGI(TAG, "%s", formatted);

    if (s_log_label == nullptr) {
        // Buffer-only mode: panel not ready yet — store and flush later.
        append_to_buffer(formatted);
        return;
    }

    if (!lvgl_port_lock(50)) {
        // LVGL busy — at least keep the buffered text for the next flush
        append_to_buffer(formatted);
        return;
    }
    append_to_buffer(formatted);
    // Don't update display while paused — will flush on resume.
    if (!s_paused.load() && s_log_label) {
        lv_label_set_text(s_log_label, s_log_buf);
    }
    lvgl_port_unlock();
}

void boot_screen_finish(uint32_t hold_ms) {
    // Wait while paused (user is reading the log).
    while (s_paused.load()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Show "Done — launching dashboard..." message then flush buffered log.
    boot_screen_log("---");
    boot_screen_log("Boot complete. Launching dashboard...", "OK");

    // Ensure the final state is rendered before the hold.
    if (lvgl_port_lock(200)) {
        if (s_log_label) lv_label_set_text(s_log_label, s_log_buf);
        // Update hint text.
        if (s_pause_label) lv_obj_add_flag(s_pause_label, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(hold_ms));
}

}  // namespace aqua::ui
