// AquaControl — Board-level configuration constants
// Single source of truth for GPIOs, I2C addresses, and panel timing.
// All component code must reference these — no magic numbers anywhere else.
#pragma once

#include "driver/gpio.h"

// -----------------------------------------------------------------------------
// Firmware identity
// -----------------------------------------------------------------------------
#define AC_FIRMWARE_NAME    "AquaControl"
#define AC_FIRMWARE_VERSION "0.1.0"

// -----------------------------------------------------------------------------
// Debug / diagnostics
// -----------------------------------------------------------------------------
// When non-zero, key state (boot summary, I2C scan, sensor reads, fault
// transitions) is also printed as a clearly-delimited block to the serial
// monitor so it can be copy-pasted into a bug report.
// Set to 0 for release builds (default). Override via build flag:
//   idf.py build -DEXTRA_CXXFLAGS="-DAC_DEBUG_DUMP_ENABLED=1"
#ifndef AC_DEBUG_DUMP_ENABLED
#define AC_DEBUG_DUMP_ENABLED 0
#endif

// When non-zero, verbose boot-time diagnostics are printed to UART:
//   - per-device I2C scan results
//   - driver smoke-test sensor readings (T/RH at startup)
//   - RTC raw time register read
//   - TimeManager seeded-from-RTC timestamp
//   - sensor sampler first-sample readings
// These are useful during bringup and hardware debugging but add ~15 lines
// per boot and are not needed in production. Set to 0 for release (default).
// Override via build flag:
//   idf.py build -DEXTRA_CXXFLAGS="-DAC_VERBOSE_BOOT=1"
#ifndef AC_VERBOSE_BOOT
#define AC_VERBOSE_BOOT 0
#endif

// -----------------------------------------------------------------------------
// I2C bus (shared: GT911 touch + all external modules)
// -----------------------------------------------------------------------------
#define AC_I2C_PORT        I2C_NUM_0
#define AC_I2C_SDA_PIN     GPIO_NUM_19
#define AC_I2C_SCL_PIN     GPIO_NUM_20
#define AC_I2C_FREQ_HZ     400000

// I2C device addresses (7-bit)
// GT911 has two possible I2C addresses (0x14 or 0x5D) selected by the INT pin
// level at reset. On this board INT/RST are unconnected, so the chip powers up
// at its factory default — usually 0x5D, but some panels ship pre-programmed
// to 0x14. Touch init probes both.
#define AC_ADDR_GT911_PRIMARY   0x5D
#define AC_ADDR_GT911_ALT       0x14
#define AC_ADDR_PCF8575       0x20   // 16-bit GPIO expander (relays)
#define AC_ADDR_PCA9685       0x40   // 16-ch PWM (PWM + RGB)
#define AC_ADDR_PCA9685_ALL   0x70   // PCA9685 all-call (always responds; NOT a separate device)
#define AC_ADDR_SHT30_WATER   0x44   // Water sensor (waterproof probe, critical)
#define AC_ADDR_SHT30_AMBIENT 0x45   // Ambient sensor (optional)
#define AC_ADDR_DS1307        0x68   // Real-time clock

// -----------------------------------------------------------------------------
// Display — ILI6122 + ILI5960 RGB parallel, 800×480
// Timing from validated LovyanGFX reference config (see SKILL.md §2 in .claude/)
// -----------------------------------------------------------------------------
#define AC_LCD_H_RES        800
#define AC_LCD_V_RES        480
#define AC_LCD_PIXEL_CLOCK_HZ  (12 * 1000 * 1000)   // 12 MHz - lowered from 15 MHz
                                                    // to eliminate LCD DMA FIFO
                                                    // underrun on touch/render
                                                    // (visible as horizontal pixel
                                                    // shift + flicker).

// Sync timings (porch / pulse-width in pixels / lines)
#define AC_LCD_HSYNC_FRONT_PORCH  8
#define AC_LCD_HSYNC_PULSE_WIDTH  4
#define AC_LCD_HSYNC_BACK_PORCH   43
#define AC_LCD_VSYNC_FRONT_PORCH  8
#define AC_LCD_VSYNC_PULSE_WIDTH  4
#define AC_LCD_VSYNC_BACK_PORCH   12

// RGB panel control pins
#define AC_PIN_LCD_PCLK    GPIO_NUM_0
#define AC_PIN_LCD_HSYNC   GPIO_NUM_39
#define AC_PIN_LCD_VSYNC   GPIO_NUM_41
#define AC_PIN_LCD_DE      GPIO_NUM_40   // H-Enable

// Backlight (LEDC PWM)
#define AC_PIN_BACKLIGHT       GPIO_NUM_2
#define AC_BACKLIGHT_LEDC_FREQ_HZ  5000      // 5 kHz — flicker-free
#define AC_BACKLIGHT_LEDC_TIMER    LEDC_TIMER_0
#define AC_BACKLIGHT_LEDC_CHANNEL  LEDC_CHANNEL_0
#define AC_BACKLIGHT_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define AC_BACKLIGHT_LEDC_RES_BITS 10        // 0..1023 duty

// RGB data pins — 5 bits Blue, 6 bits Green, 5 bits Red (RGB565)
// Order matches ESP-IDF esp_lcd_rgb_panel data_gpio_nums[16]:
// [0..4] = B0..B4, [5..10] = G0..G5, [11..15] = R0..R4
#define AC_LCD_DATA_PINS_INIT { \
    GPIO_NUM_8,  GPIO_NUM_3,  GPIO_NUM_46, GPIO_NUM_9,  GPIO_NUM_1,  /* B0..B4 */ \
    GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7,  GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_4, /* G0..G5 */ \
    GPIO_NUM_45, GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_21, GPIO_NUM_14  /* R0..R4 */ \
}

// -----------------------------------------------------------------------------
// LVGL bounce buffer geometry
// (See implementation_plan.md §4 / SKILL.md §6 — bounce buffer mode)
// -----------------------------------------------------------------------------
#define AC_LCD_BOUNCE_LINES   40                              // lines per bounce buffer
                                                              // (40×800×2 = 64 KB internal
                                                              // SRAM) — enough headroom
                                                              // to absorb CPU PSRAM
                                                              // bursts during LVGL render.
#define AC_LCD_BOUNCE_PX      (AC_LCD_H_RES * AC_LCD_BOUNCE_LINES)
