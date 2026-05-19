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
// I2C bus (shared: GT911 touch + all external modules)
// -----------------------------------------------------------------------------
#define AC_I2C_PORT        I2C_NUM_0
#define AC_I2C_SDA_PIN     GPIO_NUM_19
#define AC_I2C_SCL_PIN     GPIO_NUM_20
#define AC_I2C_FREQ_HZ     400000

// I2C device addresses (7-bit)
#define AC_ADDR_GT911         0x14   // Capacitive touch (INT unconnected → polling)
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
#define AC_LCD_PIXEL_CLOCK_HZ  (15 * 1000 * 1000)   // 15 MHz — validated safe

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
#define AC_LCD_BOUNCE_LINES   20                              // lines per bounce buffer
#define AC_LCD_BOUNCE_PX      (AC_LCD_H_RES * AC_LCD_BOUNCE_LINES)
