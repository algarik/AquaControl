# AquaControl

Embedded firmware for an ESP32-S3 aquarium automation controller. Controls lighting, pumps, and other loads with time-based, solar, and temperature triggers. Managed through a built-in touchscreen UI and optionally integrated with Home Assistant via MQTT.

---

## Hardware

The controller is built around the **[Elecrow CrowPanel 5.0" ESP32 HMI Display](https://www.elecrow.com/esp32-display-5-inch-hmi-display-rgb-tft-lcd-touch-screen-support-lvgl.html)** — an integrated module that combines the MCU, display panel, and touch controller on a single board.

| Component | Part |
|---|---|
| **Main board** | Elecrow CrowPanel 5.0" ESP32 HMI (ESP32-S3-WROOM-1-N4R8) |
| MCU | ESP32-S3, Xtensa dual-core 240 MHz, 8 MB Octal PSRAM, 4 MB flash |
| Display | 800 × 480 RGB parallel panel (ILI6122 + ILI5960), on-board |
| Touch | GT911 capacitive touch controller (I²C), on-board |
| Relay expander | PCF8575 (16-ch I²C GPIO — up to 16 relay outputs) |
| PWM controller | PCA9685 (16-ch I²C PWM — dimmable lights, dosing pumps, RGB) |
| Temperature / RH | SHT30 × 2 — water probe + ambient sensor (I²C) |
| Real-time clock | DS1307 (I²C, battery-backed) |

All peripherals share a single 400 kHz I²C bus (SDA GPIO 19, SCL GPIO 20).

---

## Features

### Device control
- **Relay outputs** — mains-level on/off loads (pumps, heaters, CO₂ solenoids, etc.)
- **PWM outputs** — smooth 0–100 % dimming for LED drivers and dosing pumps
- **RGB outputs** — three-channel PCA9685 groups with perceptual HSV colour interpolation

### Trigger engine
Three trigger types can be assigned to any device (OR logic, multiple triggers per device):

| Type | Description |
|---|---|
| **Schedule** | Daily on/off window with weekday mask |
| **Solar** | Sunrise / sunset offset — adapts automatically to the season based on configured latitude / longitude |
| **Temperature** | Hysteresis band — turns a device on/off based on water or ambient sensor reading |

### Automation scheduler
Runs every second on a dedicated FreeRTOS task. Evaluates all triggers, applies device state changes, logs edge transitions to the event history, and publishes state updates over MQTT.

### Sensors
- Water temperature and humidity (SHT30 waterproof probe, 5 s sample interval)
- Ambient temperature and humidity (SHT30, 5 s sample interval)
- I²C bus health watchdog — detects missing devices and raises faults automatically

### Connectivity
- **WiFi** — station mode, reconnects automatically
- **MQTT** — publishes per-device state as JSON; subscribes to manual on/off command topics; publishes sensor readings and a system status snapshot (uptime, free heap, fault count)
- **Home Assistant** — optional MQTT auto-discovery: all devices and sensors appear in HA automatically without manual configuration

### Touchscreen UI (LVGL v9)
- **Dashboard** — live device states, sensor readings, fault banner
- **Device list / detail** — view and toggle any output, see which trigger is active
- **Add-device wizard** — guided flow to register relays, PWM channels, or RGB groups
- **Trigger screen** — create, edit, and delete schedule / solar / temperature triggers
- **Sensors settings** — assign SHT30 roles, view raw readings
- **Network settings** — WiFi SSID/password, MQTT broker URL and credentials
- **Time & location** — UTC offset, latitude/longitude for solar calculations
- **System status** — uptime, heap, WiFi RSSI, firmware version, fault log

### Storage & reliability
- Device, trigger, and network configuration persisted in NVS (survives power cycles)
- Append-only event history on SPIFFS — records boots, fault raises/clears, and NVS erase events
- Fault subsystem tracks per-device and bus-level faults with raise/clear callbacks
- NVS erase detection: if a flash upgrade wipes configuration, a FAULT_RAISE entry is written to the history log before the next boot screen appears
- FreeRTOS stack-overflow detection via hardware watchpoint (zero runtime cost)
- Heap light-poisoning enabled to catch use-after-free at low overhead

---

## Build

Requires [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html) targeting `esp32s3`.

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

To enable verbose boot diagnostics (sensor readings, RTC time, I²C per-device scan):

```bash
idf.py build -DEXTRA_CXXFLAGS="-DAC_VERBOSE_BOOT=1"
```

---

## License

Private / not yet licensed.
