# AquaControl — Copilot Agent Operating Guide

> This file is the authoritative operating guide for GitHub Copilot (Claude) when working
> on this project. It defines conventions, commands, tools, and best practices. Always
> consult the implementation plan for *what* to build; consult this file for *how* to do it.
>
> **Implementation plan:** `.claude/implementation_plan.md`  
> **Hardware target:** ElecRow 5" ESP32-S3-WROOM-1-N4R8 (800×480, ILI6122/ILI5960 RGB)  
> **Stack:** ESP-IDF 5.5.4 · C++17 · LVGL v9.2+ · FreeRTOS

---

## Table of Contents

1. [Development Environment](#1-development-environment)
2. [Project Layout](#2-project-layout)
3. [Build, Flash & Monitor](#3-build-flash--monitor)
4. [C++ / ESP-IDF Coding Conventions](#4-c--esp-idf-coding-conventions)
5. [FreeRTOS & Multi-Core Conventions](#5-freertos--multi-core-conventions)
6. [LVGL UI Development Conventions](#6-lvgl-ui-development-conventions)
7. [I2C Driver Conventions](#7-i2c-driver-conventions)
8. [Debugging Techniques](#8-debugging-techniques)
9. [UI Validation Without Physical Hardware](#9-ui-validation-without-physical-hardware)
10. [Live Documentation Tools](#10-live-documentation-tools)
11. [Common Pitfalls & Anti-Patterns](#11-common-pitfalls--anti-patterns)

---

## 1. Development Environment

### Installed Tools

| Tool | Version | Purpose |
|------|---------|---------|
| VS Code extension `espressif.esp-idf-extension` v2.1.0 | — | Build/flash/debug GUI, terminal integration |
| ESP-IDF | **5.5.4** (installed via EIM) | Full toolchain + idf.py |
| Python 3.x | bundled | idf.py host scripts |

### How to Open the ESP-IDF Terminal

All `idf.py` commands **must** run in a terminal where the ESP-IDF environment
is activated. The VS Code extension creates this automatically — use the
**"ESP-IDF EIM"** terminal already open, or invoke it via:

> VS Code Command Palette → **ESP-IDF: Open ESP-IDF Terminal**

This terminal sets `IDF_PATH`, adds xtensa toolchain to `PATH`, and activates
the Python venv. Do NOT run `idf.py` in a plain PowerShell terminal.

### VS Code Extension Capabilities

The extension provides:
- Build/Flash/Monitor buttons in the status bar  
- `menuconfig` GUI via command palette (`ESP-IDF: SDK Configuration Editor`)  
- Size analysis (`ESP-IDF: Size Analysis of the Binaries`)  
- Device manager for COM port selection  

---

## 2. Project Layout

The ESP-IDF project lives at the workspace root `p:\Work\AquaControl\Code\`.
All source files follow the structure defined in §5 of the implementation plan.
Abridged reference:

```
(workspace root)/
├── CMakeLists.txt             # Root: lists main + components subdirs
├── sdkconfig.defaults         # Committed baseline; never edit sdkconfig directly
├── partitions.csv             # Custom partition table (see §4 plan)
├── idf_component.yml          # Managed component dependencies
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp               # app_main(); task init only — no logic here
│   └── app_config.h           # Board constants (GPIOs, I2C addrs)
└── components/
    ├── display/
    ├── touch/
    ├── i2c_bus/
    ├── drivers/               # Per-device I2C drivers
    ├── devices/               # Logical device abstraction
    ├── triggers/
    ├── scheduler/
    ├── storage/
    ├── rtc/
    ├── wifi/
    ├── solar/
    ├── ui/                    # All LVGL screens and widgets
    ├── logger/
    └── ota/
```

### File Naming

| Entity | Convention | Example |
|--------|-----------|---------|
| Component files | `snake_case.cpp` / `snake_case.h` | `pcf8575.cpp`, `display_driver.h` |
| Classes | `PascalCase` | `class RelayDevice` |
| Methods / functions | `snake_case` | `void apply(bool active)` |
| Constants / macros | `UPPER_SNAKE_CASE` | `AC_I2C_SDA_PIN` |
| Private members | trailing underscore | `uint8_t id_` |
| Namespaces | `snake_case` | `namespace aqua::display` |
| All UI string keys | `enum class LangKey` values | `LangKey::DEVICE_ON` |

### Every Component's CMakeLists.txt Pattern

```cmake
idf_component_register(
    SRCS "pcf8575.cpp"
    INCLUDE_DIRS "."
    REQUIRES "i2c_bus" "logger"      # list only direct deps
)
```

### Creating a New Component

```
idf.py create-component -C components <component_name>
```

---

## 3. Build, Flash & Monitor

All commands run inside the **ESP-IDF terminal**. The project target is
already set to `esp32s3` via `sdkconfig.defaults`.

### Core Workflow

```powershell
# Full build
idf.py build

# Build + flash + open serial monitor (most common during development)
idf.py -p COM<N> flash monitor

# Build only (check for errors without flashing)
idf.py build 2>&1 | Select-String -Pattern "error:|warning:" | Select-Object -First 30

# Open serial monitor only (device already flashed)
idf.py -p COM<N> monitor

# Exit monitor: Ctrl+]
```

### Finding the COM Port

```powershell
# List connected serial ports
[System.IO.Ports.SerialPort]::getportnames()
# Or in Device Manager look for "USB Serial Device" or "CP210x"
```

The ElecRow board exposes the ESP32-S3 built-in **USB Serial** on the
USB-C connector. No external USB-UART adapter needed.

### First-Time Project Setup

```powershell
# Set the IDF target (only needed once or after fullclean)
idf.py set-target esp32s3

# Verify sdkconfig.defaults was applied
idf.py menuconfig   # review; do NOT save unless you intend to override defaults
```

### Configuration Management

- **Never commit `sdkconfig`** — it is generated. Add it to `.gitignore`.
- All non-default settings go in **`sdkconfig.defaults`** (committed).
- After adding new Kconfig options, run `idf.py reconfigure`.
- To reset to clean config: `idf.py fullclean` then `idf.py build`.

### Key `sdkconfig.defaults` Settings (must be present)

```ini
CONFIG_IDF_TARGET="esp32s3"

# PSRAM — Octal 8MB
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384   # ≤16KB alloc → internal SRAM

# C++ standard
CONFIG_COMPILER_CXX_STANDARD=17

# LVGL memory (PSRAM heap for LVGL)
CONFIG_LV_MEM_SIZE_KILOBYTES=1024
CONFIG_LV_COLOR_DEPTH=16                    # RGB565

# Flash/partition
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y

# USB Serial console via USB-C connector
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y

# Stack/heap guards during development
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_SYSTEM_CHECK_INT_LEVEL_5=y
```

### Binary Size Analysis

After every significant feature addition, check flash usage:

```powershell
idf.py size           # total: text, data, bss sizes
idf.py size-components  # per component breakdown
idf.py size-files       # per source file breakdown
```

**Budget constraint:** App binary must stay under 2.8 MB to leave headroom
in the 3 MB `ota_0` partition. Alert if `text + data` exceeds 2.5 MB.

---

## 4. C++ / ESP-IDF Coding Conventions

### Language Standard

C++17 (`CONFIG_COMPILER_CXX_STANDARD=17`). Use:
- `std::string` for user-visible names stored in NVS
- `std::vector` for device/trigger registries (allocated at boot, fixed after)
- `std::optional` for "maybe a value" returns from drivers
- **No exceptions** (`CONFIG_COMPILER_CXX_EXCEPTIONS=n`) — ESP-IDF default
- **No RTTI** unless explicitly needed — disable if not used

### Error Handling

Always use `esp_err_t` return values for driver/init functions. **Never ignore
`esp_err_t` silently.** Use these patterns:

```cpp
// Pattern 1: abort on critical init failure (display, critical I2C device)
ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle_));

// Pattern 2: log + return for recoverable errors
esp_err_t err = i2c_bus_.read(addr_, reg, &data, 1);
if (err != ESP_OK) {
    AC_LOGE(TAG, "SHT30 read failed: %s", esp_err_to_name(err));
    return std::nullopt;
}

// Pattern 3: ESP_RETURN_ON_ERROR macro (cleaner for chains)
ESP_RETURN_ON_ERROR(pcf8575_write(state_), TAG, "PCF8575 write failed");

// NEVER do this:
esp_lcd_new_rgb_panel(&cfg, &handle);  // ignoring return value — FORBIDDEN
```

### Memory Allocation Rules

| Data type | Location | How to allocate |
|-----------|----------|----------------|
| LVGL frame buffers (750 KB each) | PSRAM | `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` |
| DMA bounce buffers (16 KB each) | Internal SRAM | `heap_caps_malloc(size, MALLOC_CAP_INTERNAL \| MALLOC_CAP_DMA)` |
| LVGL widgets / objects | PSRAM (via LVGL heap) | `lv_malloc()` / LVGL API — never `malloc()` |
| FreeRTOS task stacks | Internal SRAM | default `xTaskCreatePinnedToCore` |
| Strings in drivers | Stack / small alloc | `std::string` fine for config-time use |
| Large runtime buffers | PSRAM | `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` |
| **Never**: dynamic alloc in hot paths | — | Pre-allocate at boot; no alloc in loops |

```cpp
// Correct: allocate frame buffer in PSRAM
uint8_t *fb = static_cast<uint8_t*>(
    heap_caps_malloc(800 * 480 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
assert(fb != nullptr);  // fatal if PSRAM not available

// Correct: allocate bounce buffer in internal SRAM (DMA-capable)
uint8_t *bb = static_cast<uint8_t*>(
    heap_caps_malloc(800 * 10 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
```

### Header Guards

```cpp
#pragma once   // preferred over #ifdef guards throughout this project
```

### Logging

Use the project's logging macros (wrapping `ESP_LOGx`):

```cpp
// In every .cpp file:
static const char* TAG = "PCF8575";

// Use these macros exclusively:
AC_LOGE(TAG, "Write failed: %s", esp_err_to_name(err));  // ERROR
AC_LOGW(TAG, "Relay %d polarity inverted", ch);           // WARN
AC_LOGI(TAG, "Initialized at 0x%02X", addr);             // INFO
AC_LOGD(TAG, "Writing state 0x%04X", state);             // DEBUG (compiled out in production)

// Never use printf() or ESP_LOGI() directly — always AC_LOGx
```

Set `AC_LOG_LEVEL = AC_LOG_INFO` in production `sdkconfig.defaults`.

### `app_config.h` — Board Constants

All GPIO numbers and I2C addresses are defined here. No magic numbers in
component code:

```cpp
// components must include app_config.h or have it passed via CMake
#define AC_I2C_SDA_PIN      GPIO_NUM_19
#define AC_I2C_SCL_PIN      GPIO_NUM_20
#define AC_I2C_FREQ_HZ      400000
#define AC_ADDR_PCF8575     0x20
#define AC_ADDR_PCA9685     0x40
#define AC_ADDR_SHT30_WATER 0x44
#define AC_ADDR_SHT30_AMB   0x45
#define AC_ADDR_DS1307      0x68
#define AC_ADDR_GT911       0x14
#define AC_PIN_BACKLIGHT    GPIO_NUM_2
#define AC_PIN_LCD_PCLK     GPIO_NUM_0
// ... (see implementation_plan.md §2 for full pin table)
```

### NVS Keys

All NVS keys are defined in `storage/config_schema.h`. Never hardcode key
strings inline:

```cpp
// config_schema.h
static constexpr const char* NVS_NS       = "aquactl";
static constexpr const char* KEY_SYS_CFG  = "sys_cfg";
static constexpr const char* KEY_DEV_LIST = "dev_list";
// etc.
```

---

## 5. FreeRTOS & Multi-Core Conventions

### Task Creation — Always Pin to Core

```cpp
// All tasks MUST be pinned. No floating tasks.
xTaskCreatePinnedToCore(
    scheduler_task_fn,     // function
    "Scheduler",           // name (visible in debug)
    4096,                  // stack size in bytes
    nullptr,               // parameter
    3,                     // priority (1=lowest app, 5=highest app)
    &scheduler_task_,      // handle
    0                      // core: 0 = Control, 1 = UI/LVGL
);
```

**Core assignment summary:**
| Core 0 (Control) | Core 1 (UI) |
|-----------------|------------|
| WiFi/MQTT, Scheduler, I2C drivers, Touch polling, NTP, Web server/OTA | LVGL rendering, Screen manager |

### Inter-Core Communication

Use only the two designated queues. Never share raw pointers between cores
without synchronization:

```cpp
// Defined in main.cpp, extern'd where needed:
extern QueueHandle_t g_ui_event_queue;  // Core 0 → Core 1
extern QueueHandle_t g_cmd_queue;       // Core 1 → Core 0

// Sending from Core 0:
UIEvent ev { UIEventType::DEVICE_STATE_CHANGED, device_id, active ? 1.0f : 0.0f };
xQueueSend(g_ui_event_queue, &ev, pdMS_TO_TICKS(10));

// Receiving in LVGL callback (Core 1), inside lv_lock:
UIEvent ev;
while (xQueueReceive(g_ui_event_queue, &ev, 0) == pdTRUE) {
    update_device_card(ev.device_id, ev.value);
}
```

### Stack Size Guidelines

| Task type | Minimum stack |
|-----------|-------------|
| Simple polling tasks (touch, watchdog) | 2 KB |
| Scheduler, NTP, RTC | 4 KB |
| WiFi/MQTT | 8 KB |
| LVGL rendering task | 8 KB |
| Web server / OTA | 6 KB |

After implementing a task, verify stack high-water mark in logs:
```cpp
AC_LOGD(TAG, "Stack HWM: %d bytes", uxTaskGetStackHighWaterMark(nullptr));
```
If HWM < 512 bytes, increase stack size.

### Mutex / Semaphore Patterns

```cpp
// Create (at init time):
SemaphoreHandle_t mutex_ = xSemaphoreCreateMutex();

// Use (always with timeout — never portMAX_DELAY in hot paths):
if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
    // ... critical section ...
    xSemaphoreGive(mutex_);
} else {
    AC_LOGW(TAG, "Mutex timeout — I2C bus busy");
}
```

---

## 6. LVGL UI Development Conventions

### Thread Safety — Non-Negotiable Rule

**ALL LVGL API calls** from any task other than the LVGL task (Core 1) **must**
be wrapped with `lvgl_port_lock` / `lvgl_port_unlock`:

```cpp
// Correct: updating a label from Core 0 (scheduler callback)
if (lvgl_port_lock(50)) {           // 50 ms timeout
    lv_label_set_text(temp_label_, "26.8°C");
    lvgl_port_unlock();
} else {
    AC_LOGW("UI", "LVGL lock timeout, skipping update");
}

// FORBIDDEN: direct LVGL call from Core 0 without lock
lv_label_set_text(temp_label_, "26.8°C");  // DATA RACE — NEVER DO THIS
```

### Screen Architecture

Each screen is a C++ class with this interface:

```cpp
class BootScreen {
public:
    void create();          // Called once; creates all lv_obj_t widgets
    void destroy();         // Called when leaving; deletes screen
    void update(const BootLogLine& line);  // Thread-safe update (uses lv_lock inside)
private:
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* log_label_ = nullptr;
};
```

`ScreenManager` owns the stack and calls `create()` / `destroy()`. Never hold
raw `lv_obj_t*` pointers across screen transitions — LVGL deletes objects when
parent screen is deleted.

### Theme System — No Hardcoded Colors or Sizes

All colors, font sizes, and spacing come from `ui/theme.h`:

```cpp
// CORRECT: use theme constants
lv_obj_set_style_bg_color(card, theme::COLOR_SURFACE, 0);
lv_obj_set_style_text_font(label, theme::FONT_BODY, 0);
lv_obj_set_style_text_color(label, theme::COLOR_TEXT_PRIMARY, 0);

// FORBIDDEN: hardcoded values
lv_obj_set_style_bg_color(card, lv_color_hex(0x1B2A3B), 0);  // NO
```

Theme constants to define:
```cpp
// theme.h
namespace theme {
  // Colors (dark aquatic theme)
  inline constexpr lv_color_t COLOR_BACKGROUND   = lv_color_hex(0x0D1B2A);
  inline constexpr lv_color_t COLOR_SURFACE       = lv_color_hex(0x1B2A3B);
  inline constexpr lv_color_t COLOR_ACCENT        = lv_color_hex(0x00B4D8);
  inline constexpr lv_color_t COLOR_SUCCESS       = lv_color_hex(0x52B788);
  inline constexpr lv_color_t COLOR_WARNING       = lv_color_hex(0xF4A261);
  inline constexpr lv_color_t COLOR_ERROR         = lv_color_hex(0xE63946);
  inline constexpr lv_color_t COLOR_TEXT_PRIMARY  = lv_color_hex(0xE0E0E0);
  inline constexpr lv_color_t COLOR_TEXT_SECONDARY= lv_color_hex(0x90A4AE);

  // Fonts (declared extern; defined in theme.cpp after lv_font_conv output)
  extern const lv_font_t* FONT_SMALL;    // Roboto 14px Latin+Cyrillic
  extern const lv_font_t* FONT_BODY;     // Roboto 18px Latin+Cyrillic
  extern const lv_font_t* FONT_HEADING;  // Roboto 24px Latin+Cyrillic
  extern const lv_font_t* FONT_LARGE;    // Roboto 32px Latin+Cyrillic

  // Spacing
  inline constexpr int16_t PAD_SM = 8;
  inline constexpr int16_t PAD_MD = 16;
  inline constexpr int16_t PAD_LG = 24;
}
```

### Localization — All User-Visible Strings

**Every** string shown in the UI must go through `tr()`:

```cpp
// CORRECT
lv_label_set_text(btn_label, tr(LangKey::SAVE));

// FORBIDDEN: raw string literals in UI code
lv_label_set_text(btn_label, "Save");   // NO — not localizable
```

Language switch is instant. On language change event, iterate all active
screens and call their `refresh_lang()` method (or rebuild the screen).

### Drum-Roller Pickers for All Numeric Input

Time fields, number selections, and duration fields all use `lv_roller` or
`lv_spinbox`. **No on-screen keyboard for numeric input.** All time picker
values snap to discrete steps.

### LVGL v9 API Key Differences from v8

- `lv_disp_t*` → `lv_display_t*`
- `lv_disp_get_scr_act(disp)` → `lv_display_get_screen_active(disp)`
- `lv_disp_set_rotation` → `lv_display_set_rotation`
- `lv_color_t` initialization: use `lv_color_hex(0xRRGGBB)` — not `LV_COLOR_MAKE`
- Styles: use `lv_style_t` with `lv_obj_add_style()`, not local-style hacks
- Events: `lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user_data)`
- `lv_label_set_text_fmt(label, "Temp: %.1f°C", val)` still works in v9

**Consult Context7 for LVGL v9 API** before writing any widget code — the API
changes between v8 and v9 are significant.

### RGB Display — `lvgl_port_add_disp_rgb()`

For our RGB parallel panel, use the dedicated function:
```cpp
const lvgl_port_display_rgb_cfg_t rgb_cfg = {
    .disp_cfg = {
        .panel_handle = panel_handle,
        .buffer_size = 800 * 10,    // bounce buffer: 10 lines
        .double_buffer = true,       // two bounce buffers
        .hres = 800,
        .vres = 480,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,        // bounce buffers are DMA-capable internal SRAM
            .buff_spiram = false,    // bounce buffers NOT in PSRAM
        },
    },
    .flags = {
        .bb_mode = true,             // bounce buffer mode (PSRAM frame buffer)
    },
};
disp_handle_ = lvgl_port_add_disp_rgb(&rgb_cfg);
```

### Font Generation Command

When adding or changing font sizes:
```bash
lv_font_conv --font Roboto-Regular.ttf \
  --size 18 \
  --format lvgl \
  --range 0x20-0x7F \
  --range 0x400-0x4FF \
  -o components/ui/fonts/font_roboto_18.c

# Repeat for sizes 14, 24, 32
# For monospace boot screen font:
lv_font_conv --font RobotoMono-Regular.ttf \
  --size 16 --format lvgl \
  --range 0x20-0x7F \
  -o components/ui/fonts/font_mono_16.c
```

---

## 7. I2C Driver Conventions

### Never Access I2C Directly

All I2C operations go through `I2CBus` which owns the mutex:

```cpp
// Driver constructor receives reference to shared bus
class Pcf8575 {
public:
    explicit Pcf8575(I2CBus& bus) : bus_(bus) {}
private:
    I2CBus& bus_;
    i2c_master_dev_handle_t dev_handle_;
};

// Reads and writes:
esp_err_t err = bus_.read(dev_handle_, reg, buf, len);
esp_err_t err = bus_.write(dev_handle_, reg, buf, len);
```

### New i2c_master API (ESP-IDF v5.x)

Use `i2c_master` driver — not the legacy `i2c` driver:

```cpp
#include "driver/i2c_master.h"

// Bus init (done once in I2CBus::init()):
i2c_master_bus_config_t bus_cfg = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = AC_I2C_SDA_PIN,
    .scl_io_num = AC_I2C_SCL_PIN,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};
ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle_));

// Device add (once per device, in driver init):
i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = AC_ADDR_PCF8575,
    .scl_speed_hz = AC_I2C_FREQ_HZ,
};
ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_));

// Transmit:
ESP_ERROR_CHECK(i2c_master_transmit(dev_handle_, write_buf, len, pdMS_TO_TICKS(50)));

// Receive:
ESP_ERROR_CHECK(i2c_master_receive(dev_handle_, read_buf, len, pdMS_TO_TICKS(50)));

// Transmit then receive (register read pattern):
ESP_ERROR_CHECK(i2c_master_transmit_receive(
    dev_handle_, reg_buf, 1, read_buf, len, pdMS_TO_TICKS(50)));
```

### I2C Bus Scan at Boot

Always call `I2CScanner::scan()` during boot and log results. This populates
device presence flags used by the watchdog and fault logic. Missing a critical
device (PCF8575, PCA9685) triggers the critical fault screen.

---

## 8. Debugging Techniques

### Copy-Pasteable Diagnostic Dumps (`AC_DUMP_*`)

**Convention:** any time we add code that exposes important runtime state
(boot summary, scan results, sensor reads, fault transitions, network status,
…), also mirror that state into a `AC_DUMP` block so the user can copy the
block from the serial monitor and paste it back into chat for diagnosis.

Header: `components/logger/include/ac_debug_dump.h`  
Gate:   `AC_DEBUG_DUMP_ENABLED` in `main/app_config.h`
        (default `1` during development; set to `0` for the release build —
        every macro then compiles to a no-op).

Block format (always wrapped in greppable markers, prefixed lines):

```cpp
AC_DUMP_BEGIN("boot-summary");
AC_DUMP("Firmware:   %s v%s", AC_FIRMWARE_NAME, AC_FIRMWARE_VERSION);
AC_DUMP("Free heap:  %lu", esp_get_free_heap_size());
AC_DUMP_END("boot-summary");
```

renders to:

```
===== AC-DUMP BEGIN: boot-summary =====
  Firmware:   AquaControl v0.1.0
  Free heap:  256432
===== AC-DUMP END:   boot-summary =====
```

**When to emit a dump:**
- Boot summary at the end of `app_main` (already done).
- I2C watchdog fault/recovery transitions (already done).
- Any new "state change worth telling the user about" (NVS load result,
  WiFi state changes, OTA progress, scheduler tick decisions during
  bring-up, etc.).
- One-shot diagnostic CLI commands (e.g. `scan`, `status`).

**Do NOT emit a dump on a hot path** — they bypass log levels and always
print. Use them for once-per-boot or once-per-event state, not for the
10 Hz scheduler tick.

### Serial Monitor (Primary Debugging Tool)

```powershell
idf.py -p COM<N> monitor
```

The monitor automatically decodes panic backtraces using the firmware's ELF.
Key monitor shortcuts:
- `Ctrl+]` — exit monitor
- `Ctrl+T Ctrl+R` — reset device
- `Ctrl+T Ctrl+A` — toggle timestamps

**Serial commands** (type directly in the monitor console, 115200 baud):

| Command | Output |
|---------|--------|
| `status` | Heap free, PSRAM free, uptime, WiFi state, device states |
| `scan` | I2C bus scan showing all responding addresses |
| `relay 1 on` | Manually activate relay 1 (Core 0 command) |
| `pwm 2 75` | Set PWM channel 2 to 75% |
| `reset` | Software restart |
| `nvs_clear` | Factory reset (asks for confirmation: type `YES`) |

### Heap & Stack Monitoring

Add to any task's loop during development:

```cpp
// Heap
AC_LOGD(TAG, "Free heap: %lu, min ever: %lu, PSRAM free: %lu",
    esp_get_free_heap_size(),
    esp_get_minimum_free_heap_size(),
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

// Task stack
AC_LOGD(TAG, "Stack HWM: %u bytes free", uxTaskGetStackHighWaterMark(nullptr));
```

### Panic / Crash Analysis

When the device crashes, the monitor prints a backtrace like:
```
Backtrace: 0x4037a8a4:0x3fc97810 0x40381b4f:0x3fc97830 ...
```

The monitor automatically resolves symbols. If not, run:
```powershell
xtensa-esp32s3-elf-addr2line -pfiaC -e build/aquacontrol.elf 0x4037a8a4
```

### Core Dump via UART

**There is no hardware debugger. All debugging is serial-only.**

Enable UART core dump in `sdkconfig.defaults`:
```ini
CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
```

After a crash the monitor prints a base64-encoded core dump between
`================= CORE DUMP START =================` and
`================= CORE DUMP END ===================` markers.

To analyze it:
```powershell
# 1. Copy the base64 block (between the marker lines) into a text file:
#    Save as: coredump_b64.txt

# 2. Decode to binary:
[Convert]::FromBase64String(
    (Get-Content coredump_b64.txt) -join '') |
    Set-Content -Encoding Byte coredump.bin

# 3. Run esp-coredump analysis (inside ESP-IDF terminal):
esp-coredump.py info_corefile \
    --core coredump.bin \
    --core-format elf \
    build/aquacontrol.elf
```

This prints all task backtraces, register dumps, and local variables —
equivalent to a full backtrace without a hardware debugger.

### Serial-Only Debugging Strategies

Because there is no hardware debugger, debugging relies entirely on:

**1. Instrumented logging** — the primary technique. When investigating a bug,
temporarily raise log verbosity for the relevant component:
```cpp
// Temporarily add in the suspected component during investigation:
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
```
Remove before committing.

**2. Assertion checkpoints** — insert `assert()` or `ESP_ERROR_CHECK()` at
every assumption. A panic with a clear backtrace is far faster to diagnose
than a silent wrong-value bug.

**3. State dumps via serial command** — the `status` command (see table above)
must cover all runtime state. Extend it whenever a new subsystem is added.

**4. `esp_backtrace_print()`** — print a backtrace from any point in code
without crashing:
```cpp
#include "esp_debug_helpers.h"
esp_backtrace_print(10);  // print up to 10 frames
```
Useful to trace unexpected code paths without stopping execution.

**5. Watchdog trip analysis** — if the device resets without a panic, the
Task Watchdog (TWDT) or Interrupt Watchdog (IWDT) may have tripped. Enable
detailed WDT logging:
```ini
# sdkconfig.defaults
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_INT_WDT=y
```
The WDT panic message names the task and core that was stuck.

**6. `addr2line` for manual backtrace decoding** — if the monitor does not
auto-resolve addresses (e.g., viewing an old log file):
```powershell
# Inside ESP-IDF terminal:
xtensa-esp32s3-elf-addr2line -pfiaC -e build/aquacontrol.elf 0x4037a8a4
# Repeat for each address in the backtrace
```

### LVGL Performance Monitor

Add to `sdkconfig.defaults` to enable the built-in FPS overlay:
```ini
CONFIG_LV_USE_OBSERVER=y
CONFIG_LV_USE_SYSMON=y
CONFIG_LV_USE_PERF_MONITOR=y
```

Target: ≥30 FPS on the dashboard. If below 30, check flush callback timing
and bounce buffer size.

---

## 9. UI Validation Without Physical Hardware

Since iterating on hardware requires a flash cycle, use the **Screenshot API**
to validate UI changes remotely:

### Screenshot Workflow

1. Build and flash a version where the web server is running  
2. In a browser, navigate to `http://<device-ip>/`  
3. Click through to the screen you want to inspect  
4. Call `POST http://<device-ip>/api/screenshot/capture`  
5. Download: `GET http://<device-ip>/api/screenshot/download`  
6. Open the downloaded `.bmp` on the dev machine  

Or use curl:
```powershell
# Capture
Invoke-WebRequest -Method POST "http://192.168.1.XXX/api/screenshot/capture"

# Download
Invoke-WebRequest -OutFile "screenshot.bmp" "http://192.168.1.XXX/api/screenshot/download"

# Open
Start-Process "screenshot.bmp"
```

**Note:** The capture needs ~1.1 MB of free PSRAM. Ensure the LVGL heap has
headroom. Screenshot API buffers are freed immediately after download.

### When No Hardware Is Available (Design Phase)

Use the LVGL simulator. The PC-based LVGL simulator can render LVGL code
natively on Windows without hardware. To use:
1. Clone `lvgl/lv_port_pc_visual_studio` (Visual Studio project)
2. Copy the `ui/` component files and `theme.h` into the simulator
3. Provide stub implementations of hardware-dependent calls
4. Build and run in Visual Studio to preview layouts and animations

This is valuable for §7 UI work (screens, widget sizing, color checks) before
any hardware is available.

---

## 10. Live Documentation Tools

Two MCP tools are available for live, up-to-date API documentation during
coding. Use them instead of guessing API signatures.

### Context7 — ESP-IDF API Reference

```
Library ID: espressif/esp-idf   (trust score 9.1)
```

Example usage patterns:
- "I need to initialize an RGB panel with `esp_lcd_new_rgb_panel`" →  
  query Context7: `esp_lcd_rgb_panel_config_t esp_lcd_new_rgb_panel esp32s3`
- "How does `i2c_master_transmit_receive` work?" →  
  query Context7: `i2c_master_transmit_receive i2c_master_dev_handle`
- "What are the esp_lcd_touch_gt911 init parameters?" →  
  query Context7: `esp_lcd_touch_new_i2c_gt911 esp_lcd_touch_config_t`

### Context7 — LVGL v9 Reference

```
Library ID: lvgl/lvgl   (trust score 9.4)
```

Example queries:
- `lv_roller` widget API and options
- `lv_style_t` properties for LVGL v9
- `lvgl_port_lock lvgl_port_unlock` thread safety
- `lv_display_t` display configuration v9

**When to use Context7:**  
Before writing any LVGL widget, ESP-LCD, I2C master, or LEDC call — always
verify the exact struct fields and function signatures against the v9 / v5.5.4
docs. API changes between major versions are significant.

### idf_component.yml — Adding a Dependency

When a new managed component is needed:
```yaml
# idf_component.yml
dependencies:
  lvgl/lvgl:
    version: ">=9.2.0"
    public: true
  espressif/esp_lvgl_port:
    version: ">=2.3.0"
  espressif/esp_lcd_touch_gt911:
    version: ">=1.1.0"
  idf:
    version: ">=5.3.0"
```

After editing `idf_component.yml`, run `idf.py reconfigure` to download
new components. Components land in `managed_components/`.

---

## 11. Common Pitfalls & Anti-Patterns

### LVGL Thread Safety Violations

```cpp
// BUG: calling LVGL from Core 0 without lock
void on_temperature_update(float temp) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°C", temp);
    lv_label_set_text(water_temp_label_, buf);  // DATA RACE — will corrupt LVGL state
}

// CORRECT:
void on_temperature_update(float temp) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°C", temp);
    if (lvgl_port_lock(20)) {
        lv_label_set_text(water_temp_label_, buf);
        lvgl_port_unlock();
    }
}
```

### Stack Overflow in FreeRTOS Tasks

Tasks that call `cJSON_Print()`, `snprintf()` with large buffers, or do deep
function call chains need larger stacks. WiFi event handler callbacks execute
on the IDF system event task — don't do heavy work there; post to a queue.

### NVS Blob Size Limit

NVS blobs are limited to **~4000 bytes** per key. If device or trigger JSON
exceeds this (unlikely but possible with 16 relays + 20 triggers), split across
multiple keys. Check blob sizes with:
```cpp
AC_LOGD("NVS", "dev_list JSON size: %zu bytes", json_str.size());
```

### GT911 Address Fixed at 0x14

This board's GT911 INT pin is unconnected. **Do not attempt address
negotiation** (the address selection mechanism requires driving INT before
RESET). Hardcode to `0x14`. Polling mode only — no interrupt-driven touch.

### PCA9685 All-Call Address Conflict

Address `0x70` is both the PCA9685 all-call broadcast AND the SHTC3 sensor
address. **Never use SHTC3 sensors** with this hardware. Only SHT30 (0x44) and
SHT40 (0x44) are compatible. The I2C scanner must not flag 0x70 as an unexpected
device — it is the PCA9685 all-call.

### cJSON Memory Leaks

Always free the root object and any printed strings:
```cpp
cJSON* root = cJSON_CreateObject();
cJSON_AddStringToObject(root, "name", device.name.c_str());

char* json_str = cJSON_PrintUnformatted(root);
cJSON_Delete(root);                    // free the object tree

std::string result(json_str);
cJSON_free(json_str);                  // free the printed string (not free()!)
return result;
```

### PSRAM and DMA Incompatibility

PSRAM buffers are **not DMA-capable** on ESP32-S3. Bounce buffers (the small
DMA-read buffers used by the RGB LCD DMA) **must** be in internal SRAM. Only
allocate them with `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`. The large frame
buffer lives in PSRAM and is accessed by the CPU, not DMA.

### RGB Display Timing — Do Not Guess

The display timing parameters are **precisely defined** in `app_config.h`
and must match the reference config exactly:
- `pclk_hz` = 15,000,000
- `hsync_front_porch` = 8, `hsync_pulse_width` = 4, `hsync_back_porch` = 43
- `vsync_front_porch` = 8, `vsync_pulse_width` = 4, `vsync_back_porch` = 12
- `flags.pclk_active_neg` = true

Any deviation risks a blank or garbled display. Do not "optimize" these.

### `sdkconfig` vs `sdkconfig.defaults`

- `sdkconfig` is generated — **never commit it**, never edit it directly.
- `sdkconfig.defaults` is the committed source of truth — edit this.
- After changing `sdkconfig.defaults`, run `idf.py fullclean && idf.py build`
  to ensure the new defaults take effect cleanly.

### Avoid `std::cout` and `printf`

Both bypass the ESP-IDF logging infrastructure and miss timestamps, core
numbers, and log-level filtering. Use `AC_LOGx` macros exclusively.

### Language Strings — No Raw Literals in UI Code

Even during development / debugging, use `tr(LangKey::XXX)`. Adding a raw
string literal creates a technical debt that is easy to miss at
localization time. If a key doesn't exist yet, add it to both EN and RU
tables first.
