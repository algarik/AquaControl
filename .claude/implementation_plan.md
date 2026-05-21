# AquaControl — Implementation Plan

> ⚠️ **STATUS: DISABLED — superseded by [`continued_implementation.md`](continued_implementation.md)**  
> A deep code audit was performed. All remaining work is now tracked in `continued_implementation.md`.

> **Status:** Draft v1.2 — updated with design review improvements  
> **Date:** May 2026  
> **Stack:** ESP-IDF 5.5.4 · C++ · LVGL v9.2+ · FreeRTOS  
> **Target hardware:** ElecRow 5" ESP32-S3 Display (800×480, ILI6122/ILI5960 RGB)

---

## Table of Contents

1. [Project Goals Summary](#1-project-goals-summary)  
2. [Hardware Overview & Pin Mapping](#2-hardware-overview--pin-mapping)  
3. [Technology Stack Decisions](#3-technology-stack-decisions)  
4. [Memory Architecture](#4-memory-architecture)  
5. [Project File Structure](#5-project-file-structure)  
6. [Component Design](#6-component-design)  
   - 6.0 Multi-Core Task Architecture  
   - 6.1 Display & Touch Layer  
   - 6.2 I2C Device Drivers  
   - 6.3 Devices Abstraction  
   - 6.4 Triggers Engine  
   - 6.5 Scheduler / Execution Loop  
   - 6.6 NVS Storage  
   - 6.7 WiFi, MQTT & NTP  
   - 6.8 Solar Calculator  
   - 6.9 UI / LVGL Layer  
   - 6.10 Logger  
   - 6.11 OTA & Web Dashboard  
7. [UI/UX Design](#7-uiux-design)  
8. [Localization (EN / RU)](#8-localization-en--ru)  
9. [HomeAssistant MQTT Discovery](#9-homeassistant-mqtt-discovery)  
10. [Implementation Phases](#10-implementation-phases)  
11. [Risks & Open Questions](#11-risks--open-questions)  

---

## 1. Project Goals Summary

AquaControl is a standalone aquarium automation controller running on a 5″
ESP32-S3 touchscreen display. The core concept has **two layers**:

| Layer | What it is |
|-------|-----------|
| **Devices** | Physical hardware outputs (relays, PWM channels, RGB LED strips) |
| **Triggers** | Rules that activate/deactivate devices (time schedule, solar events, temperature thresholds) |

Users configure which devices are connected to which triggers. The device
operates fully offline; WiFi is optional and enables HomeAssistant integration
and NTP sync.

---

## 2. Hardware Overview & Pin Mapping

### Main CPU Board

| Parameter | Value |
|-----------|-------|
| SoC | ESP32-S3-WROOM-1-N4R8 |
| CPU | Xtensa LX7 dual-core 240 MHz |
| Flash | 4 MB (SPI) |
| PSRAM | 8 MB (OPI, wired as Octal) |
| Display driver chips | ILI6122 + ILI5960 (RGB parallel) |
| Display resolution | 800 × 480 px |
| Touch controller | GT911 (Capacitive, I2C) |

### RGB Display Bus Pins (from reference config)

| Signal | GPIO |
|--------|------|
| PCLK | 0 |
| HSYNC | 39 |
| VSYNC | 41 |
| DE (H-Enable) | 40 |
| B[0..4] | 8, 3, 46, 9, 1 |
| G[0..5] | 5, 6, 7, 15, 16, 4 |
| R[0..4] | 45, 48, 47, 21, 14 |
| Backlight PWM | 2 |

### I2C Bus (shared: Touch + all external modules)

| Signal | GPIO |
|--------|------|
| SDA | 19 |
| SCL | 20 |
| Frequency | 400 kHz |

### I2C Device Addresses

All addresses confirmed from module datasheets and schematics (no live hardware available during planning):

| Device | Address | Notes |
|--------|---------|-------|
| GT911 Touch | **0x14** | INT pin unconnected; address is fixed at 0x14 |
| PCF8575 | **0x20** | A0-A2 all pulled LOW |
| PCA9685 | **0x40** | A0-A5 all pulled LOW (primary address) |
| PCA9685 all-call | **0x70** | PCA9685 reserved broadcast address — always responds |
| SHT30 (water) | **0x44** | ADDR pin LOW (primary — submerged waterproof probe) |
| SHT30 (ambient) | **0x45** | ADDR pin HIGH (optional; external ambient sensor) |
| DS1307 RTC | **0x68** | Fixed, no address pins |

> **No address conflicts.** The 0x70 response is the PCA9685 hardware
> all-call feature (not a second device). Note: SHTC3 also uses 0x70, so
> SHTC3 sensors are **not compatible** with this build — only SHT30/SHT40
> at 0x44/0x45 can be used. A bus scan at boot verifies all expected devices.

### PCF8575 — Relay Channel Mapping (proposed)

PCF8575 provides 16 GPIO lines (P00–P07, P10–P17). Relays use only 5.

| PCF8575 Pin | Relay # | Description |
|-------------|---------|-------------|
| P00 | Relay 1 | Always active (relay count ≥ 1) |
| P01 | Relay 2 | Active when relay count ≥ 2 |
| P02 | Relay 3 | Active when relay count ≥ 3 |
| P03 | Relay 4 | Active when relay count ≥ 4 |
| P04 | Relay 5 | Active when relay count ≥ 5 (initial default) |
| P05 | Relay 6 | Activated by increasing relay count in Settings |
| P06–P17 | Relay 7–16 | Activated by increasing relay count in Settings |

> **✅ Decision (Relay count):** The active relay count (1–16) is configured during the first-run wizard (default: 5) and adjustable later in **Settings → Devices → Relay Configuration**. Only activated channels appear as relay devices in the UI; unactivated pins are held at their polarity-correct safe (OFF) state. This allows starting with 5 relays and scaling to 16 without any firmware changes.

> **✅ Decision (Relay polarity):** Relay polarity is configurable per-channel in the device settings UI. Each relay channel can be independently set to active-HIGH or active-LOW to support different relay module hardware designs. The PCF8575 driver reads the polarity setting from NVS at boot and inverts the output bit as required.

### PCA9685 — PWM Channel Mapping (proposed)

| PCA9685 Ch | Logical output | Type |
|------------|---------------|------|
| 0 | PWM 1 | Single-channel PWM |
| 1 | PWM 2 | Single-channel PWM |
| 2 | PWM 3 | Single-channel PWM |
| 3 | PWM 4 | Single-channel PWM |
| 4 | PWM 5 | Single-channel PWM |
| 5 | RGB Strip 1 — R | RGB |
| 6 | RGB Strip 1 — G | RGB |
| 7 | RGB Strip 1 — B | RGB |
| 8 | RGB Strip 2 — R | RGB |
| 9 | RGB Strip 2 — G | RGB |
| 10 | RGB Strip 2 — B | RGB |
| 11–15 | Reserved | |

PCA9685 PWM frequency: **1 kHz** (prescale = 5; all outputs drive through
MOSFET drivers — flicker-free and audibly silent at low duty cycles).

---

## 3. Technology Stack Decisions

| Decision | Choice | Reason |
|----------|--------|--------|
| Build system | **ESP-IDF 5.5.4 native** (idf.py + CMake) | Full control, best LVGL v9 integration |
| Language | **C++17** | OOP for device/trigger model, LVGL object patterns |
| Graphics | **LVGL v9.2+** via `idf_component.yml` | Industry standard, ESP-IDF integration via `esp_lvgl_port` |
| Display driver | **esp_lcd_new_rgb_panel()** (ESP-IDF built-in) | Native, no 3rd-party display library dependency |
| Storage | **NVS** (ESP-IDF) | Non-volatile, wear-leveled, key-value store |
| WiFi | **esp_wifi** (ESP-IDF) | Native |
| MQTT | **esp_mqtt** (ESP-IDF) | Native, plaintext port 1883, no TLS required |
| OTA | **esp_https_ota** or web upload | Memory-permitting (see §4) |
| Web server | **esp_http_server** (ESP-IDF) | Minimal, async |
| Fonts | **Roboto** (Latin + Cyrillic) | Converted with LVGL font tool |
| Solar calc | **SolarCalculator** library (embedded-friendly) | Pure math, no internet needed |
| RTC | **DS1307** driver (custom) | Simple I2C register reads |
| I2C API | **ESP-IDF i2c_master** (v5.x new API) | Modern, thread-safe |

### Component Manager (`idf_component.yml`)

Key dependencies:
```yaml
dependencies:
  lvgl/lvgl: ">=9.2.0"
  espressif/esp_lvgl_port: ">=2.3.0"
  espressif/esp_lcd_touch_gt911: ">=1.1.0"
  chrisjoyce911/SolarCalculator: ">=1.0.0"   # or local copy
  idf: ">=5.3.0"
```

> **Note on mccahan repo:** That repo targets a 480×480 display with ST7701
> over PlatformIO + Arduino + LVGL 8. We will **not** adopt it directly —
> our display is different (800×480, ILI6122 RGB), framework is different
> (ESP-IDF), and LVGL version is different (v9). However, the **screenshot
> API concept is directly useful for UI debugging** and will be implemented
> in our web dashboard (see §6.11). The approach: capture the active LVGL
> PSRAM frame buffer to a BMP in memory and serve it via HTTP — adapted to
> our ESP-IDF + PSRAM frame buffer architecture.

---

## 4. Memory Architecture

### Flash (4 MB total) — Partition Table

Using a single large app slot (no dual-slot OTA). This gives 3 MB to the
firmware and ~896 KB to SPIFFS — a much more comfortable budget than dual-slot
OTA (which would limit each slot to ~1.75 MB).

```
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   0x5000     # 20 KB NVS
otadata,  data, ota,      0xe000,   0x2000     # 8 KB OTA state
app0,     app,  ota_0,    0x10000,  0x300000   # 3 MB firmware
spiffs,   data, spiffs,   0x310000, 0xF0000    # 960 KB event history log storage
```

> **No `nvs_keys` partition:** NVS encryption is not enabled in this build
> (no sensitive secrets are stored — the AP fallback password is
> regenerable, and MQTT/WiFi credentials are protected by the device
> being physically owned). The full remaining flash is given to SPIFFS
> for the event history log.

> **Tradeoff vs dual-slot OTA:** With only one `ota_0` slot, OTA updates
> **still work** — the new firmware is written directly to the single app
> slot, with `otadata` tracking the active slot as normal. What is lost is
> the **rollback capability**: if the newly flashed firmware fails to boot,
> the device cannot automatically revert to the previous version. For a
> fish-tank controller this is acceptable — a failed OTA can be recovered
> by reflashing over USB. Fonts and icons are compiled directly into the
> binary as LVGL font/image arrays (converted at build time — no runtime
> SPIFFS access required). The SPIFFS partition is **repurposed for
> persistent event history log storage**: device state changes, temperature
> readings sampled every 5 minutes, alarms, and MQTT events — approximately
> 30 days of history. This data is viewable on the System Status screen and
> accessible via the `/api/history` web endpoint. SPIFFS wear-leveling
> ensures longevity even with regular writes.

### PSRAM (8 MB) — RAM Layout

| Area | Size | Location |
|------|------|----------|
| Frame buffer 1 (RGB565) | 750 KB | PSRAM |
| Frame buffer 2 (RGB565) | 750 KB | PSRAM |
| LVGL draw buffer (partial) | 160 KB | Internal SRAM (DMA-safe) |
| LVGL heap / widget pool | 1 MB | PSRAM (via `lv_mem_alloc`) |
| Application heap | ~5 MB | PSRAM remainder |

**LVGL draw strategy:** Use the **bounce buffer** mode with single PSRAM
frame buffer. This is the recommended approach for the ESP32-S3 with 8 MB
PSRAM:
- One full-frame buffer in PSRAM (750 KB)
- Two small bounce buffers in internal SRAM (e.g., 10 lines × 800 px × 2 B = 16 KB each)
- DMA reads from bounce buffers (fast), CPU copies from PSRAM frame buffer

This avoids tearing without needing double PSRAM frame buffers, freeing
~750 KB PSRAM for the application.

> **Alternative if bounce buffer causes issues:** Switch to full double-
> buffer in PSRAM (two 750 KB buffers). Less PSRAM for app but tearing-free.

---

## 5. Project File Structure

```
aquacontrol/
├── CMakeLists.txt                    # Root CMake
├── sdkconfig.defaults                # PSRAM, LVGL, I2C settings
├── partitions.csv                    # Custom partition table
├── idf_component.yml                 # IDF component manager deps
│
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp                      # app_main(), FreeRTOS task init
│   └── app_config.h                  # Board-level constants (GPIOs, addresses)
│
└── components/
    │
    ├── display/                      # LCD + LVGL port initialization
    │   ├── display_driver.cpp/.h     # esp_lcd_new_rgb_panel() setup
    │   ├── lvgl_port.cpp/.h          # LVGL init, draw buffer, tick timer
    │   └── backlight.cpp/.h          # PWM backlight (LEDC), dim/bright
    │
    ├── touch/                        # GT911 touch driver
    │   └── touch_gt911.cpp/.h        # Uses esp_lcd_touch_gt911 component
    │
    ├── i2c_bus/                      # Shared I2C bus manager
    │   ├── i2c_bus.cpp/.h            # Thread-safe I2C master bus
    │   └── i2c_scanner.cpp/.h        # Boot-time bus scan + device detection
    │
    ├── drivers/                      # Per-device I2C drivers
    │   ├── pcf8575.cpp/.h            # 16-bit I2C GPIO expander (relays)
    │   ├── pca9685.cpp/.h            # 16-ch PWM driver (PWM + RGB)
    │   ├── ds1307.cpp/.h             # RTC driver
    │   └── sht30.cpp/.h              # Temp/humidity sensor (SHT30/31/SHT4x auto-detect)
    │
    ├── devices/                      # Logical device abstraction
    │   ├── device_base.h             # Abstract base class: IDevice
    │   ├── relay_device.cpp/.h       # Wraps PCF8575 channel → on/off
    │   ├── pwm_device.cpp/.h         # Single PWM: on/off/level/fade
    │   ├── rgb_device.cpp/.h         # RGB strip: color, brightness, fade
    │   └── device_manager.cpp/.h     # Registry of all devices, NVS persistence
    │
    ├── triggers/                     # Trigger engine
    │   ├── trigger_base.h            # Abstract ITrigger: is_active() → bool
    │   ├── schedule_trigger.cpp/.h   # Time-based: start/stop time, daily
    │   ├── solar_trigger.cpp/.h      # Sunrise/sunset + offset (minutes)
    │   ├── temp_trigger.cpp/.h       # Temperature threshold trigger
    │   ├── trigger_manager.cpp/.h    # Registry, evaluation loop, device linking
    │   └── trigger_validator.cpp/.h  # Cross-check logic (§6.4.1); runs on save + boot
    │
    ├── scheduler/                    # Core execution loop
    │   └── scheduler.cpp/.h          # Dual-rate FreeRTOS task (10s time / 30s full)
    │
    ├── rtc/                          # Time management
    │   ├── time_manager.cpp/.h       # Unified time source: NTP → DS1307 → fallback
    │   └── ntp_sync.cpp/.h           # SNTP sync with configurable servers
    │
    ├── wifi/                         # WiFi + network
    │   ├── wifi_manager.cpp/.h       # Connect, AP fallback, event handling
    │   └── mqtt_client.cpp/.h        # HA MQTT discovery, publish, subscribe
    │
    ├── solar/                        # Solar calculations
    │   └── solar_calc.cpp/.h         # Wraps SolarCalculator, returns sunrise/sunset
    │
    ├── ui/                           # All LVGL screens and widgets
    │   ├── theme.cpp/.h              # Colors, styles, fonts (Roboto 4 sizes)
    │   ├── i18n.cpp/.h               # Translation strings EN/RU
    │   ├── screen_manager.cpp/.h     # Screen stack, transitions
    │   ├── boot_screen.cpp/.h        # Linux-style boot log screen
    │   ├── first_run_wizard.cpp/.h   # Multi-step first-run setup wizard
    │   ├── dashboard.cpp/.h          # Main dashboard
    │   ├── override_dialog.cpp/.h    # Bottom-sheet override options dialog
    │   └── settings/
    │       ├── settings_menu.cpp/.h  # Settings root menu
    │       ├── general_settings.cpp/.h
    │       ├── device_settings.cpp/.h
    │       ├── trigger_settings.cpp/.h
    │       └── system_status.cpp/.h
    │
    ├── logger/                       # Debug logging
    │   └── ac_logger.cpp/.h          # Compile-time level filter, serial output
    │
    ├── storage/                      # NVS abstraction + event history
    │   ├── nvs_store.cpp/.h          # Type-safe NVS wrapper (strings, blobs, ints)
    │   ├── config_schema.h           # All config key names + default values
    │   └── history_log.cpp/.h        # SPIFFS append-only circular event history
    │
    └── ota/                          # OTA + web dashboard
        ├── web_server.cpp/.h         # esp_http_server: status, OTA, history API
        └── ota_handler.cpp/.h        # Firmware update handler
```

---

## 6. Component Design

### 6.0 Multi-Core Task Architecture

The ESP32-S3 has two Xtensa LX7 cores. Splitting responsibilities across
cores keeps the UI responsive even when I2C operations block or WiFi
processing spikes.

**Core assignment:**

| Core | Responsibilities | Priority range |
|------|-----------------|----------------|
| **Core 0** (Control) | WiFi/MQTT stack, Scheduler, I2C device drivers, I2C watchdog, Touch polling (GT911), NTP sync, Web server / OTA | 1–5 |
| **Core 1** (UI) | LVGL rendering, Screen manager, Touch event dispatch to LVGL | 3–5 |

All FreeRTOS tasks are explicitly pinned via `xTaskCreatePinnedToCore()`.

**Inter-core communication — two FreeRTOS queues:**

```cpp
// Control → UI: notify of device state or sensor value changes
QueueHandle_t g_ui_event_queue;  // items: UIEvent { type, device_id, value }

// UI → Control: user touch commands (manual device override)
QueueHandle_t g_cmd_queue;       // items: Command { type, device_id, value }
```

**Data flow:**

```
Core 0 (Control)                         Core 1 (UI)
────────────────────             ────────────────────
Scheduler evaluates triggers             LVGL task renders widgets
  ↓ device state changes                 ↑ lv_lock() / lv_unlock()
Post UIEvent → g_ui_event_queue ─────→ UI drains queue, updates
                                          labels and card states
Touch polls I2C (GT911) every 16 ms
  ↓ touch coordinates
Post to LVGL input device queue ─────→ LVGL processes touch event
                                          ↓ user taps a device card
  ← g_cmd_queue drained by Core 0 ───── Post Command → g_cmd_queue
  ↓ device.apply(override)
I2C write to hardware
```

**LVGL thread safety:**
- LVGL is **not** thread-safe. All widget updates from Core 0 (e.g., draining
  `g_ui_event_queue`) must be wrapped in `lv_lock()` / `lv_unlock()` as
  provided by `esp_lvgl_port`.
- The UI task drains `g_ui_event_queue` at the start of each LVGL timer
  callback before rendering.

**I2C bus on Core 0 only:**
- All I2C transactions (PCF8575, PCA9685, DS1307, SHT30, GT911 touch) happen
  exclusively on Core 0 through the mutex-protected `I2CBus` wrapper. Touch
  polling runs as a dedicated short-cycle Core 0 task that posts coordinate
  events to LVGL's input device queue (which is itself thread-safe). This
  eliminates any cross-core I2C contention.

**Task summary:**

| Task | Core | Stack | Priority |
|------|------|-------|----------|
| LVGL rendering | 1 | 8 KB | 4 |
| Scheduler | 0 | 4 KB | 3 |
| I2C watchdog | 0 | 2 KB | 2 |
| WiFi / MQTT | 0 | 8 KB | 5 |
| Touch poll (GT911) | 0 | 2 KB | 4 |
| Web server / OTA | 0 | 6 KB | 2 |
| NTP sync | 0 | 4 KB | 1 |

---

### 6.1 Display & Touch Layer

**Display initialization flow:**
1. Call `esp_lcd_new_rgb_panel()` with exact timing from the reference config  
   - PCLK: 15 MHz (reference uses 15 000 000 Hz — safe for this display)  
   - Timings: hsync FP=8, PW=4, BP=43; vsync FP=8, PW=4, BP=12  
   - `pclk_active_neg = true`
2. Register `on_vsync` callback to signal LVGL flush complete  
3. Allocate bounce buffers from internal SRAM (2 × 10 lines × 800 × 2 B = ~32 KB total)  
4. Init LVGL via `esp_lvgl_port_init()` from `esp_lvgl_port` component  
5. Register display with `esp_lvgl_port_add_disp()`

**LVGL v9 flush callback strategy:**
- In the VSYNC ISR: signal a semaphore  
- Flush function: copies LVGL draw buffer → PSRAM frame buffer region, then signals `lv_display_flush_ready()`  
- LVGL tick: driven by `esp_lvgl_port` internal timer (1 ms)

**Backlight:**
- LEDC peripheral on GPIO 2 (PWM capable)  
- API: `set_brightness(uint8_t percent)` — used for dimming and fault alerts  
- Dim timeout: configurable (1–60 min), resets on any touch event  
- On fault: force brightness to 100%

**Touch (GT911):**
- Uses Espressif's `esp_lcd_touch_gt911` IDF component  
- GT911 at I2C address 0x14 (INT pin is unconnected on this board; address
  is fixed at 0x14)
- **Polling mode:** Touch data is read by I2C polling every 16 ms (dedicated
  Core 0 task), as the INT pin is not available. Touch coordinates are
  forwarded to LVGL's input device queue (thread-safe).
- Coordinate mapping: X=0–799, Y=0–479 (landscape mode)

### 6.2 I2C Device Drivers

**Thread safety:** All I2C operations go through a single mutex-protected
`I2CBus` wrapper. No driver calls I2C directly; they call `i2c_bus.read()`
/ `i2c_bus.write()`.

**PCF8575 driver:**
- Write 2 bytes to control all 16 outputs
- Cache current output state (read-modify-write pattern)
- **Configurable channel count:** The active relay count (1–16) is loaded
  from NVS key `relay_count` at boot. Only activated channels are
  instantiated as `RelayDevice` objects; unactivated pins are held at
  their polarity-correct safe (OFF) state. The count is set during the
  first-run wizard and can be changed later in **Settings → Devices →
  Relay Configuration** (count change triggers a device list rebuild and
  NVS save).
- Per-channel polarity (active-HIGH or active-LOW), configurable in the UI
  and stored in NVS
- Watchdog: verify readback matches written value

**PCA9685 driver:**
- Set up at **1 kHz** PWM frequency (prescale = 5); all outputs go through
  MOSFET drivers — safe, flicker-free, and audibly silent.
  > **Prescale note:** PCA9685 formula: `prescale = ⌊f_osc / (4096 × f_pwm)⌋ − 1`
  > = `⌊25 000 000 / (4 096 × 1 000)⌋ − 1` = **5** (datasheet min is 3; this is safe)
- Per-channel API: `set_pwm(ch, duty_4096)` — 12-bit resolution
- RGB helper: `set_rgb(strip_id, r, g, b)` where r/g/b are 0–4095
- **Fade engine:** Software timer (FreeRTOS) updates channel every 100 ms:
  ```cpp
  // Fade config per PWM channel:
  struct FadeState {
      uint16_t current;    // current 12-bit duty
      uint16_t target;     // target 12-bit duty
      uint32_t step_ms;    // ms per step
      TimerHandle_t timer;
  };
  ```
- Fade duration 0 = immediate; 1–360 min = smooth ramp

**DS1307 driver:**
- Read/write BCD time registers  
- Detect and clear CH (clock halt) bit on first use  
- Expose: `get_time()`, `set_time()`, `is_valid()`

**SHT30/31 driver:**
- Send measurement command (0x2C06 for single-shot high repeatability)
- Parse 6-byte response (temp MSB, temp LSB, CRC, hum MSB, hum LSB, CRC)
- CRC-8 validation (polynomial 0x31)
- **Two SHT30 sensors:**
  - **Primary — Water sensor** (in waterproof enclosure, submerged): this is
    the critical sensor. Its failure is a high-priority fault: temperature
    triggers depending on it are suspended and a dashboard alarm is shown.
  - **Secondary — Ambient sensor** (outside tank, reports temp + humidity):
    optional. Absence is a warning; ambient display shows "–".
- **Auto-detect:** At boot, `i2c_scanner` probes 0x44 and 0x45 for SHT30/SHT40
  sensors. Note: 0x40 (HDC1080) and 0x70 (SHTC3) are both occupied by the
  PCA9685 module (primary + all-call addresses), so only SHT30 and SHT40 are
  compatible alternatives in this build. Detected sensor role assignments are
  stored in NVS and shown in the sensor settings screen.

**I2C Watchdog:**
- A separate FreeRTOS task pings all expected I2C devices every 5 seconds
- **Graduated recovery:** on first failure: log warning; on second consecutive
  failure: attempt I2C bus reset (master deinit → re-init) and retry; on
  third failure: set fault flag, log error, trigger display alert
- PCF8575/PCA9685 failure is **critical** (fish safety): triggers relay-safe-state and dashboard alarm
- SHT30 failure is **warning** (temp triggers fall back to "unavailable" state)
- DS1307 failure is **info** (time continues from RTC memory until reboot)

### 6.3 Devices Abstraction

```cpp
class IDevice {
public:
    uint8_t id;
    std::string name;      // User-defined, stored in NVS
    bool enabled;          // User-toggleable
    
    virtual void apply(bool active) = 0;  // Called by trigger engine
    virtual DeviceType get_type() const = 0;
    virtual std::string serialize() const = 0;  // Serialize to JSON string via cJSON; used for NVS + MQTT
};

class RelayDevice : public IDevice { ... };    // 1–16 instances (count from NVS `relay_count`)
class PwmDevice : public IDevice {             // 5 instances (PCA9685 ch 0-4)
    uint8_t level;           // 0-100 %
    uint16_t fade_in_min;    // 0 = immediate
    uint16_t fade_out_min;
    void apply(bool active) override;
    // Returns FADING_IN / FADING_OUT when Pca9685::is_fading() is true; IDLE otherwise.
    enum class FadeStatus { IDLE, FADING_IN, FADING_OUT };
    FadeStatus fade_status() const;
};

struct RgbColor { uint8_t r, g, b; };          // UI-framework-independent; convert to lv_color_t in UI layer only

class RgbDevice : public IDevice {             // 2 instances (ch 5-7, 8-10)
    RgbColor color;
    uint8_t brightness;
    uint16_t fade_in_min;
    uint16_t fade_out_min;
    void apply(bool active) override;
};
```

`DeviceManager` holds a `std::vector<IDevice*>`. The relay device list is
built at boot from NVS `relay_count` (1–16 entries); PWM (5) and RGB (2)
lists are fixed. No dynamic allocation occurs after boot initialization.

### 6.4 Triggers Engine

```cpp
class ITrigger {
public:
    uint8_t id;
    std::string name;
    bool enabled;
    std::vector<uint8_t> linked_device_ids;  // devices this trigger controls
    
    virtual bool evaluate() = 0;  // Returns true if trigger should activate devices
    virtual TriggerType get_type() const = 0;
};

class ScheduleTrigger : public ITrigger {
    hhmm_t start_time;   // hh:mm (minutes from midnight)
    hhmm_t stop_time;
    bool days[7];        // day of week mask
    bool evaluate() override;
};

class SolarTrigger : public ITrigger {
    SolarEventType event;    // SUNRISE or SUNSET
    int16_t offset_min;      // -360 to +360 minutes
    Duration duration_min;   // how long to stay active after event
    bool evaluate() override;
};

class TempTrigger : public ITrigger {
    uint8_t sensor_id;
    float threshold;
    TempCondition condition;  // ABOVE or BELOW
    bool evaluate() override;
};
```

A trigger can be linked to **multiple devices**. A device can be linked to
**up to 3 triggers** — the device is active if **any** linked trigger is
active (OR logic). The 3-trigger-per-device limit is sufficient for all
practical aquarium automation scenarios and bounds NVS storage requirements.

### 6.4.1 Trigger Cross-Validation

When a user saves a trigger configuration, the device runs cross-checks and
displays **inline warnings** (non-blocking — user can acknowledge and save
anyway, or correct the issue). Warnings are also re-evaluated at boot.

| # | Check | Condition | Warning shown |
|---|-------|-----------|---------------|
| 1 | Empty trigger | Trigger has no linked devices | "This trigger controls no devices and has no effect" |
| 2 | Idle device | Device has no linked triggers | "This device will never activate automatically" |
| 3 | Schedule overlap | Two schedule triggers for the same device have overlapping time windows on the same day | "Triggers overlap — device stays ON during overlap (OR logic)" |
| 4 | Solar + no location | Solar trigger created but lat/lon not configured | "Location required for solar triggers — configure in Time & Location settings" |
| 5 | Solar duration past midnight | Solar trigger + offset + duration extends past midnight | "Duration extends past midnight; device stays on until 23:59" |
| 6 | Temp sensor unavailable | Temperature trigger references a sensor not detected at boot | "Sensor not detected — trigger is suspended until sensor is found" |
| 7 | Temp condition impossible | A device has both ABOVE threshold X and BELOW threshold Y where X ≥ Y | "Thresholds overlap — device may always be active or never active" |
| 8 | Hysteresis too large | Hysteresis value ≥ the gap between an ABOVE and BELOW threshold on the same sensor | "Hysteresis larger than threshold gap; device may never switch off" |
| 9 | No time source | Time-based trigger configured but no RTC detected and WiFi disabled | "No reliable time source — trigger accuracy not guaranteed" |
| 10 | Fade longer than schedule | PWM/RGB device fade duration > schedule trigger window | "Fade duration exceeds active window — device may never reach full brightness" |

```cpp
// TriggerValidator runs after each save and at boot
class TriggerValidator {
public:
    std::vector<ValidationWarning> validate_all(
        const std::vector<ITrigger*>& triggers,
        const std::vector<IDevice*>& devices,
        const SystemConfig& cfg);
};
```

Warnings are stored in a `std::vector<ValidationWarning>` and shown:
- Inline on the trigger/device config screen (yellow banner)
- As a count badge on the Triggers menu item in Settings ("⚠ 2 warnings")
- In the System Status screen (full list, dismissible)

### 6.5 Scheduler / Execution Loop

```
FreeRTOS Task: scheduler_task (priority 3, 4KB stack)

  Fast loop (every 10 seconds) — time-boundary detection:
    1. Get current time from TimeManager
    2. Evaluate all time-based triggers only (Schedule, Solar)
    3. If any trigger state changed since last cycle:
         → apply affected device states immediately
         → publish MQTT state change
         → post UIEvent to update dashboard cards

  Full evaluation loop (every 30 seconds):
    1. Get current time + temperatures from SHT30 driver
    2. Evaluate ALL trigger types (Schedule, Solar, Temperature)
    3. For each enabled device:
         desired_state = any linked trigger is active (OR logic)
         override = device.has_active_override()
         if override: use override_state, skip trigger
         else if desired_state != device.current_state:
             device.apply(desired_state)
             publish MQTT state change
    4. Update dashboard display values (time, temp, solar times)
    5. Publish sensor readings to MQTT
    6. Append sensor readings to SPIFFS event history (every 5 min)
    7. Expire any timed overrides that have elapsed
```

The dual-rate design ensures time-based triggers fire within **±10 seconds**
of configured time while keeping I2C sensor reads at a comfortable 30-second
cadence.

**Manual override policy:**
When a user taps a device card on the dashboard, a **bottom-sheet dialog**
appears with override options:

| Option | Behavior |
|--------|----------|
| **Until next trigger change** | Override clears the next time the scheduler would naturally change this device’s state |
| **For 1 h / 2 h / 4 h / 8 h** | Timed override; countdown shown on the device card |
| **Indefinitely** | Override persists until user manually clears it; “✋” icon shown on card |
| **Cancel override** | Shown only when device is currently overridden |

Override state is kept in RAM only — cleared on power cycle. On power cycle,
all devices revert to trigger-controlled state. Override state is published
to MQTT as `"override": true` in the state payload.

**Override precedence:** Manual override → trigger engine → default OFF.

### 6.6 NVS Storage

All configuration stored in NVS under namespace `aquactl`:

| Key | Type | Content |
|-----|------|---------|
| `sys_cfg` | blob | JSON: WiFi SSID/pass, NTP servers, lat/lon, dim timeout, language, temp unit |
| `dev_list` | blob | JSON array of all device configs |
| `trig_list` | blob | JSON array of all trigger configs, each including its `linked_device_ids` array |
| `relay_count` | uint8 | Number of active relay channels (1–16; default 5) |
| `ap_pass` | string | Auto-generated 8-char AP fallback password (set once at first boot) |
| `rtc_valid` | uint8 | 1 if DS1307 has been set at least once |
| `first_run` | uint8 | 0 = first-run wizard not completed; 1 = completed |

> Language and temperature unit live inside the `sys_cfg` JSON blob — not as separate keys — so all general system preferences are saved atomically.

**JSON library:** `cJSON` (bundled with ESP-IDF — zero extra dependencies,
well-tested, thread-safe when used with separate context objects). All NVS
blobs and MQTT payloads are produced via cJSON. The `IDevice::serialize()`
and `ITrigger::serialize()` methods return `std::string` containing the
cJSON-formatted representation.

### 6.7 WiFi, MQTT & NTP

**WiFi modes:**
1. **Station mode** — connects to configured AP  
2. **AP fallback** — if station fails after 3 attempts, creates AP
   `AquaControl-Setup` with a **randomly generated 8-character alphanumeric
   password** (uppercase letters + digits, generated once at first boot
   using `esp_random()`, stored in NVS key `ap_pass`). The password is
   displayed on the device screen during AP mode so the user can connect.
   This prevents unauthorized access to the configuration AP.
3. **Offline mode** — WiFi disabled by user in settings; all features work locally

**MQTT:**
- Broker IP/port configurable in settings (no hardcoded HA address)
- Client ID: `aquacontrol_{chip_id_hex}`
- LWT topic: `aquacontrol/{id}/availability` = `offline`
- State publishing after each device state change
- Command subscription: HA can control devices via MQTT commands
- **Reconnect strategy (exponential backoff):** retry after 5 s, 15 s, 30 s,
  then every 60 s indefinitely. After 10 consecutive failures, a persistent
  MQTT warning badge appears on the dashboard status bar. Reconnect resets
  to 5 s on any successful connection.

**NTP:**
- Primary: `pool.ntp.org`  
- Secondary: user-configurable (default `time.google.com`)  
- Timezone: simple UTC±HH:MM offset (no DST/POSIX timezone strings)  
- On sync: update both system time and DS1307 RTC  
- If no WiFi: read time from DS1307 at boot, set system time

### 6.8 Solar Calculator

Using `SolarCalculator` or implementing the NOAA algorithm:

```cpp
// Input: date (year, month, day), latitude, longitude
// Output: sunrise/sunset as minutes from midnight (local time)
struct SolarTimes {
    int sunrise_min;   // e.g. 360 = 6:00 AM
    int sunset_min;    // e.g. 1200 = 8:00 PM
    bool valid;        // false in polar regions
};

SolarTimes calculate_solar(float lat, float lon, struct tm *date);
```

Times are pre-calculated daily at midnight (or at boot). The SolarTrigger
stores the pre-computed times and evaluates against current time + offset.

> **Polar region edge case:** When sunrise/sunset doesn't occur (polar day
> or night), the trigger is automatically considered inactive and a warning
> is shown in the UI.

### 6.9 UI / LVGL Layer

See §7 for full UX design. Key architectural points:

- **Screen manager:** Stack-based navigation (`push_screen()` / `pop_screen()`)  
- **LVGL task:** Runs in its own FreeRTOS task via `esp_lvgl_port`; all UI
  updates from other tasks must go through `lv_lock()` / `lv_unlock()`
- **Theme:** Single global theme applied at init; dark mode default  
- **Fonts:** 4 sizes of Roboto (converted with LVGL font tool at build time),
  both Latin and Cyrillic glyph sets  
- **Icons:** Material Design Icons subset, converted to LVGL font or stored as
  PNG on SPIFFS and loaded as `lv_img`

### 6.10 Logger

```cpp
// Levels: ERROR, WARN, INFO, DEBUG, VERBOSE
// Compile-time: #define AC_LOG_LEVEL AC_LOG_INFO  (disable DEBUG in prod)
// Runtime: ESP-IDF serial output (115200 baud)

AC_LOGI("I2C", "PCA9685 found at 0x%02X", addr);
AC_LOGE("Trigger", "SHT30 read failed, disabling temp triggers");
```

The logger wraps ESP-IDF's `ESP_LOGx` macros. To disable completely in
production: set `AC_LOG_LEVEL = AC_LOG_NONE` in `sdkconfig`.

Serial commands (115200 baud via USB):
| Command | Response |
|---------|----------|
| `status` | Heap, uptime, WiFi, device states |
| `scan` | I2C bus scan result |
| `reset` | Restart device |
| `nvs_clear` | Factory reset (requires confirmation) |

### 6.11 OTA & Web Dashboard

**Memory feasibility check:**
- App slot: **3 MB** (single large slot, see §4)
- Estimated firmware: 800–1100 KB
- Remaining: ~2 MB headroom → OTA + web server fully feasible

**Web dashboard (minimal):**
- Served from internal HTML (stored as C string literals, not SPIFFS)
- Endpoints:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Status page (heap, uptime, WiFi, device states, I2C status) |
| `/update` | GET | OTA firmware upload form |
| `/update` | POST | Upload `.bin` file |
| `/api/status` | GET | JSON status |
| `/api/restart` | POST | Restart device |
| `/api/i2c/scan` | GET | I2C bus scan result |
| `/api/history` | GET | JSON event history (last 100 events; `?days=N` for range) |
| `/api/screenshot/capture` | POST | Capture LVGL frame buffer to BMP in RAM |
| `/api/screenshot/download` | GET | Download captured BMP as file |

**Screenshot API (for UI debugging, adapted from mccahan concept):**
1. `POST /api/screenshot/capture`: reads the active LVGL PSRAM frame buffer
   (RGB565, 800×480) and encodes it as a 24-bit BMP in a temporary PSRAM heap
   allocation (~1.1 MB needed temporarily).
2. `GET /api/screenshot/download`: streams the BMP to the browser via chunked
   HTTP transfer.
3. After download, the BMP buffer is freed immediately.

This enables iterating on UI visuals without physical access to the device.
The screenshot buffer is allocated on-demand and freed after download to avoid
permanently consuming PSRAM.

---

## 7. UI/UX Design

### Theme & Visual Language

**Color palette (dark aquatic theme):**
| Role | Color |
|------|-------|
| Background | `#0D1B2A` (deep ocean dark blue) |
| Surface / cards | `#1B2A3B` |
| Primary accent | `#00B4D8` (electric blue) |
| Secondary accent | `#48CAE4` |
| Success / active | `#52B788` (green) |
| Warning | `#F4A261` (amber) |
| Error / alarm | `#E63946` (red) |
| Text primary | `#E0E0E0` |
| Text secondary | `#90A4AE` |

**Fonts (Roboto, 4 sizes, Latin + Cyrillic):**
| Name | Size | Use |
|------|------|-----|
| `font_small` | 14 px | Labels, subtitles |
| `font_body` | 18 px | Body text, values |
| `font_heading` | 24 px | Screen titles, card headers |
| `font_large` | 32 px | Dashboard key values (time, temp) |

### Screen 0 — First-Run Setup Wizard

Shown **once** on first boot (NVS `first_run` = 0). After completion,
`first_run` is set to 1 and the wizard never appears again (unless
factory-reset). Wizard can also be re-entered from **Settings → Display &
System → Run Setup Wizard**.

The wizard is a **multi-step card carousel** (swipe or tap Next/Back):

| Step | Content | Required? |
|------|---------|-----------|
| **1 — Welcome** | Language selector (EN / RU), large buttons | Yes |
| **2 — Relay count** | Drum roller to select 1–16 active relay channels (default 5). Explains that unused channels are hidden. | Yes |
| **3 — WiFi** | SSID field + scan button + password field. "Skip" button available. | Optional |
| **4 — Time & Timezone** | UTC±HH:MM offset drum roller + manual date/time pickers. If WiFi was configured and NTP sync succeeded, pre-populated and user just confirms. Mandatory if WiFi was skipped. | Required if no WiFi |
| **5 — Location** | Latitude + longitude drum rollers (or decimal input). Needed for solar triggers. "Skip" button available; solar triggers show a warning if location is absent. | Optional |
| **6 — Done** | Summary card: "AquaControl is ready." Shows AP fallback password (generated at first boot). Offers "Name your devices" shortcut or "Go to Dashboard". | — |

> **AP password display:** During WiFi AP fallback, the 8-char password is
> shown at the top of the screen in a clearly visible box so the user can
> connect from a phone or laptop.

### Screen 1 — Boot Console

Mimics a Linux boot sequence. Full-screen dark background, monospace-style
font (Roboto Mono if available, else regular), green text on dark.

```
AquaControl v1.0 — booting...

[  0.12s] I2C bus initialized (SDA=19, SCL=20) ............. OK
[  0.18s] PCF8575 relay driver ................ found @ 0x20  OK
[  0.21s] PCA9685 PWM driver .................. found @ 0x40  OK
[  0.25s] DS1307 RTC .......................... found @ 0x68  OK
[  0.31s] SHT30 temperature sensor ............ found @ 0x44  OK
[  0.35s] GT911 touch controller .............. found @ 0x14  OK
[  0.40s] Loading configuration from NVS ................... OK
[  0.55s] Applying last device states ....................... OK
[  0.58s] WiFi: connecting to "YourNetwork"
[  1.82s] WiFi connected — IP 192.168.1.105 ............... OK
[  1.85s] NTP sync ................................ 14:32:07   OK
[  2.01s] MQTT: connecting to 192.168.1.10:1883 ............OK

System ready.
```

Failed items show in red `[FAIL]`. On critical failure (PCF8575/PCA9685)
the boot halts and shows a full error screen. 

After 2 seconds (or on touch), automatically transitions to Dashboard.

### Screen 2 — Dashboard

Layout (800×480):

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  🌊 AquaControl       Mon 19 May 2026  14:32        WiFi●  MQTT●   [⚙]     │
├────────────────────┬────────────────────┬────────────────────────────────────┤
│  🌡 Ambient        │  🌡 Water          │   🌅 Solar                        │
│    24.3°C  61% RH  │    26.8°C          │   Sunrise 06:14  Sunset 21:47     │
├────────────────────┴────────────────────┴────────────────────────────────────┤
│  DEVICES                                                                     │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐       │
│  │ 💡 Main Lamp│  │ 💧 Pump     │  │ 🔵 LED 1    │  │ ⚡ Heater   │       │
│  │  ████░░  73%│  │  ● ON       │  │  ████████   │  │  ○ OFF      │       │
│  │  Fade 15min │  │  Schedule   │  │  Solar+5min │  │  Temp >25°C │       │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                         │
│  │ ⚡ CO2      │  │ 🔵 LED 2    │  │ 💡 Night L. │                         │
│  │  ● ON       │  │  ████░░  60%│  │  ○ OFF      │                         │
│  │  Schedule   │  │  Schedule   │  │  Solar-30m  │                         │
│  └─────────────┘  └─────────────┘  └─────────────┘                         │
└──────────────────────────────────────────────────────────────────────────────┘
```

- Status bar: time, date, WiFi/MQTT indicators, settings gear  
- Sensor cards: ambient temp+humidity, water temp, sunrise/sunset times; each shows a "last updated" timestamp (e.g. "2 min ago") — turns amber if reading is >5 min old  
- Device cards: icon, name, state (ON/OFF + level bar for PWM), active trigger name  
  - **Override badge:** if manually overridden, shows `⏱ 1h 42m` (timed) or `✋` (indefinite) in the card corner  
- Tapping a device card: opens **override bottom-sheet dialog** (Until next change / 1h / 2h / 4h / 8h / Indefinitely / Cancel override)  
- Tapping settings gear: → Settings menu

**Dimming:** After configured inactivity timeout, backlight dims to
configured level. Touch restores full brightness.  
**Fault state:** On I2C device failure, affected device cards turn red,
backlight forced to 100%, and a dismissible alert banner appears at top.  
**Validation warnings:** If trigger cross-validation detected issues, a
"⚠ N warnings" banner links to the System Status screen.

### Screen 3 — Settings

Top-level settings menu (list with icons):

| Icon | Item |
|------|------|
| 📶 | WiFi & Network |
| 🕐 | Time, NTP & Location |
| 🔧 | Devices Configuration |
| ⚡ | Triggers Configuration |
| 🖥 | Display & System |
| 📊 | System Status & Logs |
| 🌐 | Language |

### Screen 3a — WiFi & Network

- WiFi enable/disable toggle  
- SSID field + scan button (shows list of found networks)  
- Password field (masked, show/hide button)  
- MQTT broker IP, port, username, password fields  
- "Save & Connect" button  
- Current connection status indicator

### Screen 3b — Time, NTP & Location

- Current time display  
- Manual time set (when WiFi/NTP unavailable): date/time pickers  
- NTP server 1 & 2 fields  
- Timezone offset (UTC±HH:MM)  
- Latitude / Longitude fields (for solar calculation)  
- "Sync Now" button (if WiFi connected)

### Screen 3c — Devices Configuration

List of all active devices (**N** relays based on `relay_count` + 5 PWM + 2 RGB). Tapping a device opens its config screen. A **"Relay Configuration"** button at the top allows changing the active relay count (1–16):
- Drum roller to select count
- Warning shown if decreasing count: "Relay N will be removed — its trigger assignments will be cleared"
- Confirmation required before reducing count

**Relay device config:**
- Name field  
- Active/disabled toggle  
- Polarity (active-HIGH / active-LOW)  

**PWM device config:**
- Name field  
- Active/disabled toggle  
- Default level slider (0–100%)  
- Fade-in duration drum roller (0–360 min)  
- Fade-out duration drum roller (0–360 min)  

**RGB device config:**
- Name field  
- Active/disabled toggle  
- Color picker (HSV wheel)  
- Brightness slider  
- Fade-in / Fade-out duration drum rollers  

### Screen 3d — Triggers Configuration

List of all triggers. “Add” button creates a new trigger. Each trigger:

> **Input controls:** All time and numeric fields use **drum-roller
> (scroll-wheel) pickers** — large, touch-friendly columns that snap to
> discrete values. No on-screen keyboard required. Drag up/down to scroll;
> release to confirm. Decimal values (temperature) use two separate rollers
> (integer + fractional) side by side.

> **Validation:** On save, the device runs cross-checks (§6.4.1). Warnings
> appear as a yellow inline banner below affected fields. User can
> acknowledge and save anyway, or correct the issue first.

**Schedule trigger config:**
- Name, enable toggle  
- Start time (HH:MM drum roller)  
- Stop time (HH:MM drum roller)  
- Day-of-week checkboxes (Mon–Sun) with “Every day” / “Weekdays” / “Weekend” shortcuts  
- Linked devices (multi-select list)  

**Solar trigger config:**
- Name, enable toggle  
- Event: Sunrise / Sunset (toggle)  
- Offset: ±360 min drum roller (“30 min after sunrise”; shows computed absolute time preview)  
- Active duration: drum roller 1–360 min  
- Linked devices (multi-select list)  

**Temperature trigger config:**
- Name, enable toggle  
- Sensor: Ambient / Water (shows real-time reading + availability status)  
- Condition: Above / Below (toggle)  
- Threshold: dual drum roller (integer + 0.1° fractional, in configured unit)  
- Hysteresis: drum roller 0–5.0° in 0.1° steps (default 0.5°; prevents rapid cycling)  
- Linked devices (multi-select list)  

### Screen 3e — Display & System

- Screen brightness slider (current, active)  
- Dim level slider (dimmed brightness)  
- Inactivity timeout drum roller (1–60 min, or “Never”)  
- **Temperature unit:** °C / °F toggle (applies globally to all displays, trigger thresholds, and MQTT payloads)  
- **Run Setup Wizard** button (re-enters first-run wizard without factory reset)  
- Factory reset button (with two-step confirmation dialog: “Are you sure?” → “Type RESET to confirm”)  
- OTA firmware update (button shows device IP and opens web UI instructions)

### Screen 3f — System Status & Logs

- I2C device status table (address, name, status: OK / FAIL, last ping time)  
- Trigger validation warnings list (from §6.4.1; each entry dismissible)  
- **Event history** (last 100 events from SPIFFS log, scrollable; filter by type: State / Sensor / Alarm)  
- Memory stats (heap free, PSRAM free, SPIFFS used/free, uptime)  
- Last 50 serial log entries (scrollable, colored by level: ERROR=red, WARN=amber, INFO=white, DEBUG=gray)  
- Chip info (IDF version, firmware version, MAC address, chip ID)

---

## 8. Localization (EN / RU)

LVGL supports UTF-8 natively. All UI strings are stored in a central
translation table:

```cpp
// i18n.h
enum class LangKey : uint16_t {
    DASHBOARD_TITLE,
    SETTINGS_WIFI,
    DEVICE_ON,
    DEVICE_OFF,
    TRIGGER_SCHEDULE,
    TRIGGER_SOLAR,
    TRIGGER_TEMP,
    // ... ~200 keys total
};

const char* tr(LangKey key);  // Returns EN or RU string based on current lang

// Example:
lv_label_set_text(label, tr(LangKey::DEVICE_ON));
```

**Roboto font with Cyrillic** — font generation command:
```bash
# Generate font bitmaps from Roboto TTF
lv_font_conv --font Roboto-Regular.ttf \
  --size 18 \
  --format lvgl \
  --range 0x20-0x7F \      # ASCII
  --range 0x400-0x4FF \    # Cyrillic
  -o font_roboto_18.c
```

Repeat for sizes 14, 24, 32. Total estimated glyph data: ~150–250 KB
(depends on exact character ranges). Stored compiled into the binary.

Language switching: instant (no reboot required). Setting persisted to NVS.

---

## 9. HomeAssistant MQTT Discovery

Published on WiFi connect and on each MQTT reconnect.

### Discovery Topics

```
homeassistant/switch/aquactl_relay1/config     # PCF8575 Relay 1
homeassistant/switch/aquactl_relay2/config     # ...
homeassistant/light/aquactl_pwm1/config        # PCA9685 PWM 1 (dimmable light)
homeassistant/light/aquactl_rgb1/config        # PCA9685 RGB Strip 1 (color light)
homeassistant/sensor/aquactl_temp_ambient/config
homeassistant/sensor/aquactl_temp_water/config
homeassistant/sensor/aquactl_humidity/config
```

### Example — Relay Discovery Payload

```json
{
  "name": "Main Pump",
  "unique_id": "aquactl_AABBCC_relay1",
  "state_topic": "aquacontrol/AABBCC/relay/1/state",
  "command_topic": "aquacontrol/AABBCC/relay/1/set",
  "payload_on": "ON",
  "payload_off": "OFF",
  "availability_topic": "aquacontrol/AABBCC/availability",
  "device": {
    "identifiers": ["aquacontrol_AABBCC"],
    "name": "AquaControl",
    "model": "ElecRow 5inch ESP32-S3",
    "manufacturer": "AquaControl",
    "sw_version": "1.0.0"
  }
}
```

### Example — PWM Light Discovery Payload

```json
{
  "name": "Main Lamp",
  "unique_id": "aquactl_AABBCC_pwm1",
  "schema": "json",
  "state_topic": "aquacontrol/AABBCC/pwm/1/state",
  "command_topic": "aquacontrol/AABBCC/pwm/1/set",
  "brightness": true,
  "brightness_scale": 100,
  "availability_topic": "aquacontrol/AABBCC/availability"
}
```

MQTT command from HA overrides the trigger state for that device until the
next trigger re-evaluation cycle. An "automation override" flag is stored
per device.

---

## 10. Implementation Phases

### Phase 1 — Foundation (Display + LVGL + Boot Screen) — ✅ **COMPLETE**
**Goal:** See pixels on screen, touch working, boot console displayed.

- [x] ESP-IDF project skeleton (`idf.py create-project`)
- [x] `sdkconfig.defaults`: PSRAM enable, SPIRAM_MODE_OCT, LVGL config
- [x] `logger/ac_logger.h`: Logging macros (needed by everything below)
- [x] `i2c_bus/i2c_bus.cpp`: Mutex-protected I2C master bus (touch + later drivers all share it)
- [x] `display/display_driver.cpp`: `esp_lcd_new_rgb_panel()` with reference timing
- [x] `display/backlight.cpp`: LEDC PWM on GPIO 2
- [x] `touch/touch_gt911.cpp`: GT911 init via `esp_lcd_touch_gt911` component (uses shared I2C bus)
- [x] `display/lvgl_port.cpp`: LVGL init, flush callback, tick timer
- [x] `ui/boot_screen.cpp`: Boot console (static demo text first)
- [x] `ui/theme.cpp`: Colors + fonts (first pass)
- [x] **Deliverable:** Device boots, shows boot screen, touch hardware verified
       (touch→dashboard navigation deferred to Phase 4 with `screen_manager`)

> ⚠️ **Field note (GT911 address):** The plan assumed 0x14, but the actual
> board enumerated at **0x5D**. Touch driver now probes both candidates
> (`AC_ADDR_GT911_PRIMARY = 0x5D`, `AC_ADDR_GT911_ALT = 0x14`) and uses
> whichever responds. A boot-time I2C scan in `main.cpp` logs all bus
> responders for diagnostics.

### Phase 2 — I2C Drivers + Watchdog ✅ COMPLETE
**Goal:** All external hardware detected and controllable from CLI.

- [x] `i2c_bus/i2c_scanner.cpp`: Boot-time device scan (extends the bus from Phase 1)
- [x] `drivers/pcf8575.cpp`: Relay on/off
- [x] `drivers/pca9685.cpp`: PWM + fade engine (FreeRTOS timer)
- [x] `drivers/ds1307.cpp`: RTC read/write
- [x] `drivers/sht30.cpp`: Temp/humidity read + auto-detect variants (driver = `sht3x.cpp`)
- [x] `i2c_bus`: Watchdog task (graduated recovery: warn → bus pulse → fault flag + callback)
- [x] Boot screen integration: show real I2C scan results
- [ ] Serial command: `scan`, `relay 1 on`, `pwm 2 50` — **deferred** (driver self-tests via logs only, per session decision)
- [x] **Build green** (`idf.py build` clean, 78% partition free)
- [ ] **Deliverable:** All hardware responds, boot log shows real device status — **pending flash to COM3**

> Field note (PCA9685): `MAX_DUTY=4095`, 1 kHz output, init enables AI+ALLCALL,
> fade engine uses Q8.8 fixed-point steps and a single 100 ms FreeRTOS timer
> that stops itself when no fades are active.
>
> **Fade bug fix (May 2026):** When `|delta| × 256 < ticks`, integer division
> truncates `step_q8` to 0. For up-fades this caused the accumulator to never
> advance (stuck at 0 forever); for down-fades the direction check `step_q8 >= 0`
> was immediately true → fade completed in one tick. Fixed by clamping:
> `if (f.step_q8 == 0 && delta != 0) f.step_q8 = (delta < 0) ? -1 : 1;`
>
> **`is_fading(chan)` API added (May 2026):** `Pca9685::is_fading(uint8_t chan) const`
> returns `fades_[chan].active`. Used by `PwmDevice::fade_status()`.
>
> Field note (SHT3x): Uses cmd `0x24 0x00` (single-shot high-rep, no clock
> stretch), 20 ms wait, CRC-8 poly 0x31 init 0xFF on each 2-byte word.
>
> Field note (Watchdog): ESP-IDF v5.5 master API has no hot bus-reset, so the
> "reset" step issues a probe at 0x00 (never ACKs) to drive SCL high — usually
> enough to unstick a line. A full bus deinit/re-init would invalidate every
> driver's `i2c_master_dev_handle_t`; left for Phase 3 if needed.

> **Strategy note (May 19 2026):** Phase 4 (UI) is intentionally deferred
> until every backend that feeds the UI is implemented and verified. The
> agreed order is **Block A (data plumbing) → B (config persistence) →
> C (engine completion: solar + validator + history) → D (Phase 5 network:
> WiFi/NTP/MQTT) → E (polish: serial-log ring, chip info, i18n) → Phase 4 UI**.
> Rationale: every UI screen binds to a stable backend API; building UI on
> placeholders forces rewiring later. Agent is to proceed autonomously
> through this list without re-asking; only stop on hardware-blocking
> failures or ambiguous design questions. UI work begins only when every
> "needed-by-UI" backend listed in this section is checked off.

### Phase 3 — Core Logic (Devices + Triggers + Storage)
**Goal:** Configure devices and triggers via code, see scheduler run.

**Status (foundation IN PROGRESS — solar / validator / history deferred):**
- [x] `storage/nvs_store.cpp`: NVS wrapper (init handles NO_FREE_PAGES/NEW_VERSION; u8/u16/u32/i32/str/blob)
- [x] `devices/`: `IDevice` + override engine, `RelayDevice` (PCF8575), `PwmDevice` (PCA9685 12-bit + fade), `RgbDevice` (3-channel R/G/B with brightness scaling), `DeviceManager`
- [x] `triggers/`: `ITrigger` + `ScheduleTrigger` (with wrap-past-midnight), `TempTrigger` (with hysteresis), `SolarTrigger` skeleton, `TriggerManager` (per-device OR aggregation)
- [ ] `triggers/trigger_validator.cpp`: Cross-validation logic (§6.4.1) — **deferred to Phase 3 follow-up**
- [x] `scheduler/scheduler.cpp`: Dual-rate FreeRTOS task (10 s fast / 30 s full), pinned Core 0, priority 3, 4 KB stack; uses `IDevice::resolve_active` for overrides
- [x] `rtc/time_manager.cpp` (under `components/time_mgr/`): DS1307-backed `settimeofday` seed, UTC-offset-aware `now_local()` / `minutes_since_midnight()`
- [ ] `solar/solar_calc.cpp`: SolarCalculator integration — **deferred to Phase 3 follow-up**
- [ ] `storage/history_log.cpp`: SPIFFS event history writer — **deferred to Phase 3 follow-up**
- [ ] Load/save all config to NVS — **deferred to Phase 4 (UI drives writes)**
- [x] Build-time RTC fallback (parses `__DATE__`/`__TIME__`) seeds DS1307 when invalid or year < 2024
- [x] Hardcoded test config wired in `main.cpp`: 1 `RelayDevice` on PCF8575 ch 0 + 1 `ScheduleTrigger` firing ~1 min after boot for 3 min
- [x] Boot-summary AC_DUMP extended with NVS / time-synced / device count / trigger count / scheduler-running lines
- [x] **Build verified:** `idf.py build` — clean (Flash code 506 KB, Flash data 142 KB, DIRAM 23 %)
- [x] **Hardware verification DONE (May 19 2026):** boot at 17:33:36 → `Relay 1 (test) -> ON` at exactly 17:34:00 (window start) → `Relay 1 (test) -> OFF` at exactly 17:37:00 (window stop). End devices not yet wired but logical pipeline (trigger → scheduler → resolve_active → driver) fully exercised.
- [x] **Display fix (Phase 2 follow-up):** `display_driver.cpp` now zeroes the PSRAM framebuffer immediately after `esp_lcd_panel_init`, eliminating the random/shifted boot-screen contents on each reset (LVGL bounce-buffer partial mode was revealing stale PSRAM).

- [x] **Deliverable (partial):** Hardcoded test config + scheduler activates relay at schedule time — VERIFIED.
- [ ] **Deliverable (remainder):** History entries appear in SPIFFS — pending `storage/history_log.cpp`.

### Phase 3.5 — Backend-First Pre-UI Backlog
**Goal:** Implement every backend module the UI binds to, so Phase 4 is pure wiring.

**Block A — data plumbing (no network):**
- [x] A1 `sensors/sensor_sampler.cpp`: periodic SHT3x task; caches `{temp_c, rh, ts, valid}` per sensor; thread-safe getters
- [x] A2 Wire `TempTrigger` to sensor cache via scheduler (calls `update_temperature` each fast tick)
- [x] A3 Active-trigger tracking on `IDevice` (`last_driver_trigger_id`); scheduler writes it on every pass
- [x] A4 Touch-activity tracker (single `last_input_ts` updated by LVGL input callback)
- [x] A5 Fault registry (singleton; I2C watchdog pushes faults; UI queries list)

**Block B — config persistence:**
- [x] B6 `SystemConfig` struct + NVS load/save (lat/lon, tz, dim levels/timeout, language, temp unit, relay_count, first_run, ntp servers)
- [x] B7 Device + Trigger cJSON serialise/deserialise; `save_devices/load_devices/save_triggers/load_triggers` (config_storage.cpp)
- [x] B8 `TimeManager::set_time(struct tm)` writes DS1307 + system time (used by wizard / Screen 3b / NTP)

**Block C — engine completion:**
- [x] C9 `solar/solar_calc.cpp`: NOAA algorithm; scheduler invokes daily at local midnight + on lat/lon change
- [x] C10 `validators/trigger_validator.cpp`: 10 cross-checks (§6.4.1); warning list per trigger id (moved out of triggers component to avoid storage circular dep)
- [x] C11 `history/history_log.cpp`: SPIFFS append-only circular ~96 KB primary + .bak rotation; BOOT + I2C fault events wired

**Block D — network (Phase 5 brought forward):**
- [x] D12 `wifi/wifi_manager.cpp`: station + AP fallback + random 8-char AP password in NVS
- [x] D12+ `wifi/wifi_manager.h/.cpp`: `scan_networks_blocking(max_count, timeout_ms)` — blocking scan called from a background task; saves/restores WiFi mode; returns `std::vector<ScanResult>` sorted by descending RSSI. Used by wizard SSID scan button.
- [x] D13 `rtc/ntp_sync.cpp`: SNTP → `TimeManager::set_time`
- [x] D14 `mqtt_aqua/mqtt_client_aqua.cpp`: HA discovery + state publish + command subscribe + auto-reconnect (folder renamed from `mqtt/` to avoid collision with IDF mqtt component)

**Block E — polish:**
- [x] E15 Serial-log ring buffer (`logger/log_ring.cpp`: 50-line tee via `esp_log_set_vprintf`, snapshot/clear API)
- [x] E16 Chip-info accessor (`logger/chip_info.cpp`: model/rev/cores/flash/PSRAM/heap/IDF/app-desc/sha256)
- [x] E17 i18n table (EN/RU full common-vocabulary, ~80 keys, compile-time table-size check)

**Each block ends with:** `idf.py build` clean + hardware smoke test where applicable.

> **✅ Phase 3.5 complete (May 19, 2026).** All blocks A1–E17 built clean and hardware-verified on COM3. Boot log shows: log-ring tee active before any logging, SystemConfig defaults applied, SPIFFS auto-formatted on first boot, all 5 expected I2C devices present (ambient SHT30 correctly absent), DS1307 seeded TimeManager, sensor sampler caching water T/RH every 5 s, scheduler running fast=10s/full=30s, solar calc producing sunrise/sunset at default coordinates, faults=0. Network modules (wifi/rtc/mqtt_aqua) compiled in but inert — they wait for Phase 4 first-run wizard to supply credentials. One known cosmetic item: status line reports `idle=UINT64_MAX ms` when no touch event has ever occurred — clamp in Phase 4 UI polish.

### Phase 4 — Full UI
**Goal:** Complete touch-driven settings interface.

#### Slice 1 — Dashboard skeleton + screen manager ✅ COMPLETE (May 19, 2026)
- [x] `ui/screen_manager.cpp`: screen stack with `Transition::NONE/FADE` swap
- [x] `ui/dashboard.cpp`: full dashboard — header (date/time, WiFi, MQTT), fault banner, sensor row (water T/RH, ambient, solar sunrise/sunset), device grid with per-card state + override badges, network/faults refresh, throttled label updates (`label_set_if_changed`)
- [x] `ui/ui_context.cpp`: cross-component dependency injection
- [x] `ui/dim_manager.cpp`: inactivity dim (80% → 15% after 120 s)
- [x] Touch event dispatch from GT911 polling task into LVGL input device (Core 0 → LVGL queue)

#### Slice 1.5 — Display & font polish ✅ COMPLETE (May 19, 2026)
- [x] **RGB tearing investigation:** tried `num_fbs=2 + avoid_tearing` (PSRAM-DMA contention noise on touch) → reverted to single FB + bounce-buffer mode (validated config)
- [x] **LCD timing tuned:** pclk 15 → 12 MHz, bounce buffer 20 → 40 lines (64 KB internal SRAM) — eliminates DMA FIFO underrun ("content shifts on touch") and dashboard flicker
- [x] **Boot→dashboard transition:** `Transition::FADE` → `Transition::NONE` (single-frame swap; bounce mode can't sustain full-screen fade overdraw)
- [x] **Modern design system:** new "deep-water aquatech" palette (slate-100 on navy with cyan-400 accents), card recipe with `RADIUS_LG` + hairline outline + subtle shadow + pressed state. Documented in `/memories/repo/aquacontrol-ui-design-system.md`.
- [x] **Roboto fonts (14/18/24/32) with Cyrillic + FA icons:** generated via `lv_font_conv` 1.5.3; ranges: ASCII + Latin-1 (`°`, `±`) + Cyrillic (U+0400–U+04FF) + FontAwesome solid subset matching `LV_SYMBOL_*` macros. Build flag `LV_LVGL_H_INCLUDE_SIMPLE` set on the `ui` component so `lv_font_conv`'s `#include "lvgl/lvgl.h"` resolves.
- [x] **Iconography:** WiFi, upload (MQTT), warning (faults), up/down (sunrise/sunset), power (relay/RGB state), pause (HOLD), bell (timed override) — all via `LV_SYMBOL_*` (no PNG pipeline).
- [x] **Display driver invariants captured in repo memory** (`aquacontrol-board-quirks.md`).
- Final binary: 930 KB / 3 MB partition (70% free); hardware-verified clean boot, stable touch, no flicker.

#### Slice 1.6 — NVS/RGB cache-race fix ✅ COMPLETE (May 20, 2026)
- [x] **Root cause identified:** RGB-LCD bounce-buffer EOF ISR `memcpy`s from PSRAM framebuffer (read requires cache); SPI flash erase/program disables cache for the full op → CacheError (EXCCAUSE 0x7) in interrupt context whenever NVS writes overlapped LCD refresh.
- [x] **JEDEC ID confirmed via esptool:** board ships Zbit ZB25VQ32 (`0x5E4016`). ESP-IDF v5.5.4 has no driver for vendor 0x5E → falls back to generic driver → generic refuses to advertise `SPI_FLASH_CHIP_CAP_SUSPEND` → naive `CONFIG_SPI_FLASH_AUTO_SUSPEND=y` panics at flash init.
- [x] **Custom chip driver shipped:** `components/flash_chip_zbit/` — probe on vendor 0x5E, whitelist 0x5E4016 for SUSPEND, Winbond-equivalent suspend command config per ZB25VQ32 datasheet (0x75 suspend, 0x7A resume, SUS=SR2[7] via 0x35). All other ops inherit from `spi_flash_chip_generic_*`. Registered via project-local `default_registered_chips[]`.
- [x] **sdkconfig.defaults:** `CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST=y` + `CONFIG_SPI_FLASH_AUTO_SUSPEND=y` (required deleting and regenerating sdkconfig — old `is not set` entries override new defaults).
- [x] **Verified on hardware:** boot log shows `spi_flash: detected chip: zbit` + `Flash suspend feature is enabled`; `load_devices` and `Boot complete` reached without CacheError.
- [x] **screen_manager + device_detail_screen** rewrites kept: deferred pop via `lv_async_call`, per-widget ActionCtx, save_timer debounce (good engineering even though they were not the actual fix).
- [x] **Repo memory updated:** `aquacontrol-board-quirks.md` cache-race section marked RESOLVED with Zbit driver + datasheet + sdkconfig stickiness gotcha documented.

#### Remaining Phase 4 slices (not started)
- [x] **Slice 2 — Override dialog:** `ui/override_dialog.cpp` bottom-sheet modal (Until next change / Timed 1·2·4·8 h / Indefinite / Cancel); wire from dashboard card tap
  > **Note (2026-05):** Implemented inline in `device_detail_screen.cpp` as a full device-settings screen rather than a bottom-sheet modal. No separate `override_dialog.cpp` exists. Functionality is complete. Status: done (different implementation path).
  > **Fade status display (2026-05):** `device_detail_screen` `refresh()` timer now shows **FADING IN** (accent colour) / **FADING OUT** (secondary colour) in `lbl_state_big` for PWM devices while `PwmDevice::fade_status()` != IDLE; falls back to ON/OFF when idle.
- [ ] **Slice 3 — Settings screens:** `ui/settings/settings_menu.cpp` + general/device/trigger/system_status subscreens (CRUD via drum-roller)
- [x] **Slice 4 — First-run wizard:** `ui/wizard.cpp` single-screen wizard (step counter replaces push/pop navigation); gated by NVS `first_run` flag. Layout: fixed-pixel header (48 px) + scrollable content (360 px) + nav bar (72 px) = 480 px — avoids invalid `LV_PCT(100) - N` arithmetic. Back button hidden on step 0, visible on steps 1–3. Skip button on WiFi step only. Step-dot indicators. Keyboard persists as overlay child of root across step transitions. Scan button on WiFi step triggers background `xTaskCreate` → `lv_async_call` result callback. State freed only on root `LV_EVENT_DELETE` (not on step navigation). Completes by calling `screen_manager::pop(FADE)` and writing WiFi + `SystemConfig` to NVS.
- [ ] **Slice 5 — i18n EN/RU:** verify all dashboard/settings strings render Cyrillic correctly; add language switcher in general settings
- [ ] **Slice 6 — Drum-roller numeric input widget:** reusable scroll-wheel picker for times, brightness, temperatures
- [ ] **Slice 7 — Validation warning banner:** show top-of-screen banner when `trigger_validator` reports cross-check failures (already computed in backend)
- [ ] **Slice 8 — WiFi/MQTT status hooks:** connect dashboard `lbl_net_wifi`/`lbl_net_mqtt` to real `wifi_manager` + `mqtt_aqua` state changes (currently shows `-` placeholder)
- [ ] **Slice 9 — Polish:** clamp `idle=UINT64_MAX ms` cosmetic bug; restyle boot_screen with new card recipe; final font subset trim if size matters
- [ ] **Deliverable:** Complete UI, all settings configurable via touch; first-run wizard guides new user

### Phase 5 — WiFi, NTP, MQTT (backend complete via Phase 3.5 Block D; UI pending)
**Goal:** Connect to network, sync time, publish to HomeAssistant.

- [x] `wifi/wifi_manager.cpp`: Station + AP fallback (with random 8-char AP password in NVS) — done in Phase 3.5 D12
- [x] `rtc/ntp_sync.cpp`: SNTP sync → `TimeManager::set_time` → DS1307 — done in Phase 3.5 D13
- [x] `mqtt_aqua/mqtt_client_aqua.cpp`: HA discovery + state publish + command subscribe + auto-reconnect — done in Phase 3.5 D14 (folder renamed from `mqtt/` to avoid collision with IDF mqtt component)
- [ ] WiFi settings UI integration — pending Phase 4 Slice 4 (first-run wizard) + Slice 8 (status hooks)
- [ ] **Deliverable:** HA sees all devices, controls work from HA — pending UI to supply credentials (modules are compiled in but inert until then)

### Phase 6 — OTA + Web Dashboard
**Goal:** Firmware updates without USB cable.

- [ ] Verify partition table (single 3 MB `ota_0` slot + 896 KB SPIFFS — defined in §4)
- [ ] `ota/web_server.cpp`: Status page + OTA upload + history API + screenshot API
- [ ] `ota/ota_handler.cpp`: esp_https_ota handler
- [ ] Memory budget verification (3 MB app slot — confirm firmware + web server fit)
- [ ] **Deliverable:** Upload `.bin` via browser; event history via `/api/history`; screenshot capture for UI debugging

### Phase 7 — Testing & Polish
- [ ] All trigger types tested (schedule, solar, temperature)
- [ ] Trigger cross-validation: confirm all 10 warning conditions fire correctly
- [ ] Override mechanism: all options tested (timed countdown, indefinite, cancel)
- [ ] I2C watchdog tested (unplug device, verify graduated recovery + alert)
- [ ] Power cycle: verify NVS restore + correct initial device states
- [ ] First-run wizard: verify all steps, skip paths, and NVS `first_run` flag
- [ ] Fade timing accuracy
- [ ] Edge cases: polar coordinates, WiFi loss, MQTT disconnect/reconnect backoff
- [ ] SPIFFS history log: verify circular overwrite, 30-day capacity, `/api/history` output
- [ ] Memory leak check (monitor heap + PSRAM over 24h)
- [ ] Temperature unit switching: verify °C/°F conversion across all UI labels and MQTT payloads
- [ ] Final font tuning
- [ ] Set `AC_LOG_LEVEL = AC_LOG_ERROR` for production build

---

## 11. Risks & Open Questions

### Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|-----------|
| Display blank/garbled with ESP-IDF RGB driver | Medium | High | Start with exact reference timing; test PCLK frequencies 10–15 MHz |
| Firmware too large for 3 MB app slot | Low | Low | Profile early; 3 MB is generous for estimated 700–1100 KB firmware |
| I2C bus contention (touch + 4 devices at 400 kHz) | Low | High | Mutex in I2C wrapper; adequate delay between transactions |
| LVGL v9 API changes from v8 | Medium | Medium | Follow esp_lvgl_port examples; LVGL v9 porting guide |
| SHT30 CRC errors at 400 kHz I2C | Low | Low | Reduce bus speed to 100 kHz if needed |
| PCA9685 fade accuracy | Low | Low | FreeRTOS timer jitter ±5 ms is acceptable for light fading |

### Decisions Made

All questions from the initial draft and design review have been resolved:

| # | Question | Decision |
|---|----------|----------|
| 1 | Relay count | Configurable 1–16 at first-run wizard (default 5); only active relays shown in UI |
| 2 | Relay polarity | Configurable per-channel (active-HIGH / active-LOW) in device settings UI |
| 3 | Water temperature sensor | Two SHT30 sensors at 0x44 (water, primary/critical) and 0x45 (ambient, optional) |
| 4 | PCF8575 unused pins | Held at polarity-correct safe state; revealed in UI only when relay count is increased |
| 5 | PCA9685 output type | All through MOSFET drivers → **1 kHz** PWM (prescale = 5) |
| 6 | Max triggers per device | 3 triggers per device (OR logic) |
| 7 | Display orientation | Landscape (800×480) confirmed |
| 8 | GT911 INT pin | Unconnected → polling mode every 16 ms on Core 0 |
| 9 | Timezone | Simple UTC±HH:MM offset |
| 10 | MQTT security | Plaintext port 1883, no TLS |
| 11 | JSON library | `cJSON` (ESP-IDF built-in) — no extra dependencies |
| 12 | SPIFFS usage | Repurposed for event history log storage (device states, temps, alarms ≈ 30 days) |
| 13 | Scheduler rate | Dual-rate: 10 s for time-based triggers, 30 s full evaluation with sensors |
| 14 | Manual override | Bottom-sheet dialog: Until next change / Timed (1/2/4/8 h) / Indefinite |
| 15 | WiFi AP password | Randomly generated 8-char alphanumeric via `esp_random()`, stored in NVS, shown on screen |
| 16 | Temperature units | °C/°F user setting (Settings → Display & System); applies globally |
| 17 | Numeric input UX | Drum-roller (scroll-wheel) pickers for all time and numeric settings |
| 18 | Trigger validation | 10 cross-checks run on save and at boot; warnings shown inline and in System Status |

### Remaining Open Questions

None. All hardware addresses confirmed — no I2C conflicts:

| Device | Confirmed Address |
|--------|------------------|
| GT911 touch | 0x14 |
| PCF8575 GPIO expander | 0x20 |
| PCA9685 PWM | 0x40 (+ 0x70 all-call) |
| SHT30 water sensor | 0x44 |
| DS1307 RTC | 0x68 |

---

*End of Plan v1.2*

---

## Appendix — Developer Design Notes (integrated 2026-05)

The following items were recorded by the developer in `.claude/notes.md` and are queued for implementation in the appropriate phase. They do not block the correction plan gate but should be incorporated before Phase 7 testing.

### DN-1 — Temperature sensor smoothing + threshold deadband for TempTrigger

**Phase**: 4 (add to TempTrigger backend, wire to trigger settings UI)

The `TempTrigger` currently compares raw sensor readings against `high_threshold` / `low_threshold`. Two improvements needed:
1. **EMA smoothing**: apply an exponential moving average (`alpha ≈ 0.2`) to `current_temp_` inside `TempTrigger::update_temperature()`. The smoothed value is what `evaluate()` compares against thresholds. This prevents rapid on/off cycling from sensor noise.
2. **Hysteresis deadband**: when temperature crosses the threshold to activate, require it to go `deadband_delta` past the threshold before deactivating. Prevents chatter around the setpoint. The deadband value (default `0.5 °C`) should be configurable per trigger.

```cpp
// Proposed additions to TempTrigger:
float ema_temp_    = 0.0f;    // smoothed reading
float deadband_c_  = 0.5f;    // hysteresis deadband
```

---

### DN-2 — Solar trigger event-type selector (SUNRISE / SUNSET) in UI

**Phase**: 4, Slice 3 (trigger settings sub-screens)

The `SolarTrigger` data model already has `SolarEvent event` (SUNRISE or SUNSET) and the scheduler correctly populates both `sunrise_min_today` and `sunset_min_today`. However the trigger creation/edit UI currently hard-codes SUNRISE (no selector shown).

**Implementation**: Add a two-option toggle (SUNRISE / SUNSET) to the solar trigger settings screen. Wire to `SolarTrigger::event` field. The toggle style should match the trigger-type pill pattern from `add_device_screen`.

---

### DN-3 — Water heater safety watchdog subsystem

**Phase**: 4 (scheduler integration) + 5 (MQTT alert)

Safety requirement: detect potentially dangerous heater fault conditions and raise an alert / cut power.

**Condition 1 — Heater stuck ON with no temperature response**:
- If a device linked to a PWM heater trigger has been continuously active for more than `N` minutes (configurable, default 60 min) AND the water temperature has not risen by at least `delta` °C (configurable, default 0.5 °C) over that period, raise a fault.
- Fault code: `0x0200` — "Heater: no temperature response"

**Condition 2 — Temperature rising with heater OFF**:
- If no heating device is active AND water temperature is rising more than `rate` °C/min (configurable, default 0.5 °C/min) over a 5-minute rolling window, raise an alert (not a fault — could be external heat source).
- Fault code: `0x0201` — "Temp: rising without heater"

**Implementation location**: New component `components/safety/heater_watchdog.cpp` with a periodic check called from the scheduler's `pre_eval` hook or as a separate 60-second timer task on Core 0.

---

### DN-4 — RGB fade with HSV interpolation instead of linear RGB lerp

**Phase**: 4 (RgbDevice fade implementation)

Current `RgbDevice::apply()` issues an immediate (or faded via `fade_to()`) step to the target duty cycle. The PCA9685 `fade_to()` linearly interpolates PWM duty (i.e., linear RGB). For smooth colour transitions this produces a visually "muddy" midpoint.

**Improvement**: When fade duration > 0, compute a series of intermediate HSV keyframes and schedule them via a FreeRTOS timer or LVGL async call. The transition stays perceptually smooth through the HSV hue arc rather than the RGB cube diagonal. Also, for simple PWM divice need add a optional gamma correction curve (e.g., `output = input^2.2`) to linear brightness changes. And also add the configurable low limit of power to PWM devices - e.g. fan can't spin below some duty, so we need to start and end at this limit.

**Implementation**: Add `hsv_to_rgb()` and `rgb_to_hsv()` utilities to `components/devices/` (or a new `components/color_utils/`). In `RgbDevice::apply()`, when fading, divide the fade into 8–16 steps, computing intermediate RGB values from HSV lerp and calling `set_pwm()` for each step at `fade_ms / steps` intervals.

---

### DN-5 — Device rename UI

**Phase**: 4, Slice 3 (device settings sub-screen)

The `IDevice::name` field exists, and `storage::save_devices()` persists it. However there is no UI to change the name after the device is created (name is set at `add_device_screen` time only).

**Implementation**: In `device_detail_screen`, add a name text field (or a tappable label that opens a keyboard dialog) near the top of the screen. Wire changes to `dev->name = new_name` and schedule a save via `schedule_save()`. The keyboard can reuse the pattern from any existing text-input screen in the project.

---

### DN-6 — Confirmation dialogs for all destructive operations

**Phase**: 4 (ongoing, add to any screen with destructive actions)

Currently only the device delete action has a confirmation dialog. Other destructive actions that should have confirmation:
- **Trigger delete**: add a confirmation step matching the device delete pattern
- **Clear override**: prompt "Cancel manual override?" before clearing
- **Factory reset** (Phase 4 Settings → System): "This will erase all devices and triggers. Confirm?"
- **WiFi credentials change** (Settings → Network): "Reconnect to new network?" prompt

Review all `LV_EVENT_CLICKED` handlers that modify or delete persistent data and add a two-button confirm/cancel bottom sheet before the destructive call.

