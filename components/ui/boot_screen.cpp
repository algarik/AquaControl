#include "boot_screen.h"

#include <cstdio>
#include <cstring>

#include "ac_logger.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "theme.h"

namespace aqua::ui {

static const char* TAG = "BootScreen";

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_log_label = nullptr;
static lv_obj_t* s_title_label = nullptr;

// Buffer holding the full boot log text. Grown line by line.
// Sized for ~40 lines × ~80 chars each — plenty for Phase 1.
static constexpr size_t LOG_BUF_SIZE = 4096;
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

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, theme::PAD_MD, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_title_label = lv_label_create(s_screen);
    lv_label_set_text(s_title_label, "AquaControl  v0.1.0  -  booting...");
    lv_obj_set_style_text_color(s_title_label, theme::color_accent(), 0);
    lv_obj_set_style_text_font(s_title_label, theme::font_body(), 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_log_label = lv_label_create(s_screen);
    lv_label_set_long_mode(s_log_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_log_label, LV_PCT(100));
    lv_obj_set_style_text_color(s_log_label, theme::color_success(), 0);
    lv_obj_set_style_text_font(s_log_label, theme::font_small(), 0);
    lv_obj_align_to(s_log_label, s_title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, theme::PAD_MD);
    lv_label_set_text(s_log_label, "");

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
    lv_label_set_text(s_log_label, s_log_buf);
    lvgl_port_unlock();
}

}  // namespace aqua::ui
