#include "lvgl_port.h"

#include "ac_logger.h"
#include "app_config.h"
#include "display_driver.h"
#include "esp_lvgl_port.h"

namespace aqua::display {

static const char* TAG = "LVGLPort";
static lv_display_t* s_disp = nullptr;

esp_err_t lvgl_init() {
    if (s_disp != nullptr) return ESP_OK;

    esp_lcd_panel_handle_t panel_handle = panel();
    if (panel_handle == nullptr) {
        AC_LOGE(TAG, "Display panel not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Init the LVGL port task (pinned to Core 1; UI lives there)
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    // Deep flex-layout passes + custom event chains can briefly use several
    // KB of LVGL-task stack. 8 KB is the esp_lvgl_port default but with
    // our card-stack screens we want extra headroom to avoid silent stack
    // overflows that reboot the device with no panic output.
    port_cfg.task_stack = 12288;
    port_cfg.task_affinity = 1;             // Core 1 = UI
    port_cfg.timer_period_ms = 5;
    esp_err_t err = lvgl_port_init(&port_cfg);
    if (err != ESP_OK) {
        AC_LOGE(TAG, "lvgl_port_init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Attach the RGB display in bounce-buffer mode (validated config for
    // ElecRow CrowPanel 5.0). Two PSRAM FBs + avoid_tearing was tried and
    // produced screen-wide PSRAM-DMA contention noise on touch/redraws,
    // because both FBs are continuously scanned out of PSRAM. Here the
    // panel's bounce buffer (in internal SRAM) feeds the LCD DMA, so the
    // CPU can write the single PSRAM FB without starving the panel.
    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.panel_handle = panel_handle;
    disp_cfg.buffer_size = AC_LCD_BOUNCE_PX;  // LVGL draw buffer size
    disp_cfg.double_buffer = true;
    disp_cfg.hres = AC_LCD_H_RES;
    disp_cfg.vres = AC_LCD_V_RES;
    disp_cfg.monochrome = false;
    disp_cfg.rotation.swap_xy = false;
    disp_cfg.rotation.mirror_x = false;
    disp_cfg.rotation.mirror_y = false;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma = false;
    // LVGL draw buffers in *internal SRAM*, not PSRAM.  The panel scans
    // the PSRAM framebuffer continuously through the bounce buffer
    // (~24 MB/s).  If LVGL's draw buffers also live in PSRAM, every
    // partial redraw is a PSRAM-write *and* the eventual flush is a
    // PSRAM-to-PSRAM memcpy — three concurrent PSRAM clients fighting
    // for one bus.  On heavy-scroll screens (time/location) the bounce
    // buffer misses its hsync deadline and the panel image shifts left
    // and stays shifted.  Putting the 2×64 KB draw buffers in internal
    // SRAM (fits comfortably on ESP32-S3) leaves only one PSRAM
    // consumer (the FB↔bounce DMA) and one SRAM→PSRAM flush.
    disp_cfg.flags.buff_spiram = false;
    disp_cfg.flags.swap_bytes = false;

    lvgl_port_display_rgb_cfg_t rgb_cfg = {};
    rgb_cfg.flags.bb_mode = true;           // bounce-buffer DMA from SRAM
    rgb_cfg.flags.avoid_tearing = false;    // not compatible with bb_mode

    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (s_disp == nullptr) {
        AC_LOGE(TAG, "lvgl_port_add_disp_rgb failed");
        return ESP_FAIL;
    }

    AC_LOGI(TAG, "LVGL ready, display attached");
    return ESP_OK;
}

lv_display_t* lvgl_display() { return s_disp; }

}  // namespace aqua::display
