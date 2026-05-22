// AquaControl — Application entry point
//
// Boot sequence (current as of Phase 2):
//   1. Init shared I2C master bus
//   2. Scan I2C bus, cache result
//   3. Init RGB panel, backlight, LVGL port, GT911 touch
//   4. Init I2C peripheral drivers (PCF8575, PCA9685, DS1307, SHT3x) using
//      the scan result to decide which addresses to bring up
//   5. Show boot console with real scan + driver results
//   6. Start I2C watchdog task; loop idle.
//
// Phase 3 will replace the manual driver smoke-tests below with the device
// abstraction and trigger engine.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>

#include "ac_debug_dump.h"
#include "ac_logger.h"
#include "activity.h"
#include "app_config.h"
#include "backlight.h"
#include "boot_screen.h"
#include "dashboard.h"
#include "device_manager.h"
#include "dim_manager.h"
#include "display_driver.h"
#include "ds1307.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "faults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_log.h"
#include "log_ring.h"
#include "i2c_bus.h"
#include "i2c_scanner.h"
#include "i2c_watchdog.h"
#include "lvgl_port.h"
#include "nvs_store.h"
#include "config_storage.h"
#include "system_config.h"
#include "pca9685.h"
#include "pcf8575.h"
#include "relay_device.h"
#include "schedule_trigger.h"
#include "scheduler.h"
#include "screen_manager.h"
#include "wizard.h"
#include "sensor_sampler.h"
#include "sht3x.h"
#include "solar_calc.h"
#include "solar_trigger.h"
#include "temp_map_trigger.h"
#include "temp_trigger.h"
#include "time_manager.h"
#include "touch_gt911.h"
#include "trigger_manager.h"
#include "ui_context.h"
#include "wifi_manager.h"
#include "web_portal.h"
#include "mqtt_client_aqua.h"
#include "ntp_sync.h"
#include "i18n.h"

static const char* TAG = "main";

// Single global instance of the I2C bus, owned by app_main.
static aqua::i2c::I2CBus      g_i2c_bus;
static aqua::i2c::Watchdog    g_i2c_wdg;
static aqua::drivers::Pcf8575 g_relays;
static aqua::drivers::Pca9685 g_pwm;
static aqua::drivers::Ds1307  g_rtc;
static aqua::drivers::Sht3x   g_sht_water;     // 0x44
static aqua::drivers::Sht3x   g_sht_ambient;   // 0x45

// Phase 3 — device & trigger registries (lifetime = app lifetime).
static aqua::devices::DeviceManager   g_devices;
static aqua::triggers::TriggerManager g_triggers;

// Helper: log "<msg> [ OK|FAIL ]" to both serial and boot screen.
static void boot_line(const char* msg, bool ok) {
    aqua::ui::boot_screen_log(msg, ok ? "OK" : "FAIL");
}

// M-5: file-scope spinlock — guards correlated lat/lon/utc_offset_min reads/writes
// in g_sys_cfg. Declared extern in app_config.h; used by wizard.cpp and pre_eval lambda.
portMUX_TYPE g_sys_cfg_mux = portMUX_INITIALIZER_UNLOCKED;

extern "C" void app_main(void) {
    // H-6: Catch any uncaught C++ exception or pure-virtual call and reboot
    // rather than spinning in an undefined state.
    std::set_terminate([]() {
        ESP_LOGE("main", "std::terminate() called — rebooting");
        esp_restart();
    });

    // Install in-RAM log tee before any other logging happens, so the UI
    // "System Info → Logs" page sees the complete boot trace.
    aqua::log_ring::init();

    AC_LOGI(TAG, "=== %s v%s starting ===", AC_FIRMWARE_NAME, AC_FIRMWARE_VERSION);
    AC_LOGI(TAG, "Free heap: %lu, free PSRAM: %lu",
            (unsigned long)esp_get_free_heap_size(),
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // 0. Persistent storage — required by Phase 3+ config loading.
    esp_err_t nvs_err = aqua::storage::NvsStore::init();
    if (nvs_err != ESP_OK) {
        AC_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_err));
    }

    // 0a. Load SystemConfig (defaults if missing). Declared static so the
    // pointer we hand to ui_context / dim_manager is valid for the whole
    // program lifetime (app_main never returns, but `static` makes the
    // intent explicit and survives any future refactor that does).
    static aqua::storage::SystemConfig g_sys_cfg;
    aqua::storage::load_system_config(&g_sys_cfg);
    // M-5: g_sys_cfg_mux is defined at file scope (above app_main) so that
    // wizard.cpp (via extern in app_config.h) can also acquire it.

    // Restore saved language to the i18n subsystem before any UI text is set.
    aqua::ui::i18n::set_language(
        static_cast<aqua::ui::i18n::Language>(
            static_cast<uint8_t>(g_sys_cfg.language)));

    // 0b. Mount SPIFFS-backed event history + record boot.
    if (aqua::history::init() == ESP_OK) {
        aqua::history::append(aqua::history::EventType::BOOT, 0, 0,
                              AC_FIRMWARE_NAME " " AC_FIRMWARE_VERSION);
        // R7: if NVS had to be erased at init, record it now that SPIFFS is up.
        if (aqua::storage::NvsStore::was_erased()) {
            AC_LOGE(TAG, "NVS was erased — all device/trigger/wifi config lost");
            aqua::history::append(aqua::history::EventType::FAULT_RAISE, 0, 0,
                                  "NVS erased: all config lost (flash version change)");
        }
    }

    // 1. I2C bus (shared by touch + all external modules).
    ESP_ERROR_CHECK(g_i2c_bus.init(AC_I2C_PORT, AC_I2C_SDA_PIN, AC_I2C_SCL_PIN,
                                   AC_I2C_FREQ_HZ));

    // 2. Bus scan — cached for later (boot screen + watchdog).
    // Per-device results are verbose-only; always log the summary line after.
    const aqua::i2c::ScanResult scan = aqua::i2c::scan(g_i2c_bus, /*verbose=*/AC_VERBOSE_BOOT);
    AC_LOGI(TAG, "I2C scan: %u responder(s) on bus", (unsigned)scan.total_responders);

    // 3. Display panel + backlight.
    ESP_ERROR_CHECK(aqua::display::init());
    ESP_ERROR_CHECK(aqua::display::backlight_init());
    ESP_ERROR_CHECK(aqua::display::backlight_set_percent(80));

    // 4. LVGL port + attach RGB display.
    ESP_ERROR_CHECK(aqua::display::lvgl_init());

    // 5. Touch (must run after LVGL port — registers an input device).
    esp_err_t terr = aqua::touch::init(g_i2c_bus);
    if (terr != ESP_OK) {
        AC_LOGW(TAG, "Touch init failed (will continue without touch): %s",
                esp_err_to_name(terr));
    }

    // 6. I2C peripheral drivers — only bring up devices the scan found.
    using aqua::i2c::KnownDevice;

    esp_err_t pcf_err = ESP_ERR_NOT_FOUND;
    if (scan.devices[(size_t)KnownDevice::PCF8575_RELAYS].present) {
        pcf_err = g_relays.init(g_i2c_bus,
                                scan.devices[(size_t)KnownDevice::PCF8575_RELAYS].addr);
    }

    esp_err_t pca_err = ESP_ERR_NOT_FOUND;
    if (scan.devices[(size_t)KnownDevice::PCA9685_PWM].present) {
        pca_err = g_pwm.init(g_i2c_bus,
                             scan.devices[(size_t)KnownDevice::PCA9685_PWM].addr);
    }

    esp_err_t rtc_err = ESP_ERR_NOT_FOUND;
    if (scan.devices[(size_t)KnownDevice::DS1307_RTC].present) {
        rtc_err = g_rtc.init(g_i2c_bus,
                             scan.devices[(size_t)KnownDevice::DS1307_RTC].addr);
    }

    // SHT3x: if both addresses respond, 0x44 = water, 0x45 = ambient (per
    // wiring convention). If only one sensor is present at either address,
    // promote it to the WATER role — water temperature is the safety-critical
    // reading. The user can re-designate via UI settings in Phase 4.
    const bool water_present   = scan.devices[(size_t)KnownDevice::SHT30_WATER].present;
    const bool ambient_present = scan.devices[(size_t)KnownDevice::SHT30_AMBIENT].present;

    esp_err_t shtw_err = ESP_ERR_NOT_FOUND;
    esp_err_t shta_err = ESP_ERR_NOT_FOUND;
    uint8_t   shtw_addr = 0;
    uint8_t   shta_addr = 0;
    bool      shtw_promoted = false;  // single sensor at 0x45 promoted to water

    if (water_present && ambient_present) {
        shtw_addr = AC_ADDR_SHT30_WATER;
        shta_addr = AC_ADDR_SHT30_AMBIENT;
        shtw_err  = g_sht_water.init(g_i2c_bus, shtw_addr);
        shta_err  = g_sht_ambient.init(g_i2c_bus, shta_addr);
    } else if (water_present) {
        shtw_addr = AC_ADDR_SHT30_WATER;
        shtw_err  = g_sht_water.init(g_i2c_bus, shtw_addr);
    } else if (ambient_present) {
        // Only the 0x45 sensor is present — treat it as water for now.
        shtw_addr     = AC_ADDR_SHT30_AMBIENT;
        shtw_promoted = true;
        shtw_err      = g_sht_water.init(g_i2c_bus, shtw_addr);
        AC_LOGW(TAG, "Only SHT3x @ 0x%02X found — promoting to WATER role "
                     "(re-assign in Settings later)", shtw_addr);
    }

    // 7. Boot console — populate with real scan/init results.
    aqua::ui::boot_screen_show();
    {
        // H-2: Use GPIO constants rather than literal numbers so the log stays accurate.
        char i2c_line[64];
        snprintf(i2c_line, sizeof(i2c_line), "I2C bus initialized (SDA=%d, SCL=%d)",
                 (int)AC_I2C_SDA_PIN, (int)AC_I2C_SCL_PIN);
        aqua::ui::boot_screen_log(i2c_line, "OK");
    }
    aqua::ui::boot_screen_log("Display: 800x480 RGB panel", "OK");
    aqua::ui::boot_screen_log("Backlight: LEDC PWM on GPIO 2", "OK");
    aqua::ui::boot_screen_log("LVGL v9 attached", "OK");
    {
        char line[64];
        if (terr == ESP_OK) {
            snprintf(line, sizeof(line), "Touch: GT911 @ 0x%02X",
                     aqua::touch::detected_address());
        } else {
            snprintf(line, sizeof(line), "Touch: GT911 not found");
        }
        boot_line(line, terr == ESP_OK);
    }

    {
        char line[64];
        const auto& info = scan.devices[(size_t)KnownDevice::PCF8575_RELAYS];
        snprintf(line, sizeof(line), "Relays: PCF8575 @ 0x%02X",
                 info.present ? info.addr : AC_ADDR_PCF8575);
        boot_line(line, pcf_err == ESP_OK);
    }
    {
        char line[64];
        const auto& info = scan.devices[(size_t)KnownDevice::PCA9685_PWM];
        snprintf(line, sizeof(line), "PWM: PCA9685 @ 0x%02X (1 kHz)",
                 info.present ? info.addr : AC_ADDR_PCA9685);
        boot_line(line, pca_err == ESP_OK);
    }
    {
        char line[64];
        snprintf(line, sizeof(line), "RTC: DS1307 @ 0x%02X", AC_ADDR_DS1307);
        boot_line(line, rtc_err == ESP_OK);
    }
    {
        char line[64];
        if (shtw_err == ESP_OK) {
            snprintf(line, sizeof(line), "Sensor: SHT3x water @ 0x%02X%s",
                     shtw_addr, shtw_promoted ? " (promoted)" : "");
            boot_line(line, true);
        } else {
            snprintf(line, sizeof(line), "Sensor: SHT3x water @ 0x%02X",
                     AC_ADDR_SHT30_WATER);
            boot_line(line, false);
            // Raise a persistent fault so the dashboard banner reflects the
            // missing sensor even before the sampler task starts raising its own.
            aqua::faults::raise(0x0010, aqua::faults::Source::SENSOR,
                                "Water sensor missing at boot");
        }
    }
    {
        char line[64];
        if (shta_err == ESP_OK) {
            snprintf(line, sizeof(line), "Sensor: SHT3x ambient @ 0x%02X",
                     shta_addr);
            boot_line(line, true);
        } else {
            snprintf(line, sizeof(line), "Sensor: SHT3x ambient @ 0x%02X",
                     AC_ADDR_SHT30_AMBIENT);
            aqua::ui::boot_screen_log(line, "SKIP");  // ambient is optional
        }
    }

    // 8. Smoke-test the just-initialised drivers so logs prove they work.
    //    These are intentionally minimal — full device abstraction is Phase 3.
    if (rtc_err == ESP_OK) {
        struct tm t = {};
        bool valid = false;
        if (g_rtc.get_time(&t, &valid) == ESP_OK) {
#if AC_VERBOSE_BOOT
            AC_LOGI(TAG, "RTC: %04d-%02d-%02d %02d:%02d:%02d  valid=%d",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec, valid);
#endif

            // Build-time fallback: until NTP (Phase 5) sets real time, if the
            // RTC reports invalid OR a clearly wrong year (< 2024), seed it
            // from the firmware's compile timestamp. Better than 2000-01-01
            // for testing time-based triggers in Phase 3.
            if (!valid || (t.tm_year + 1900) < 2024) {
                struct tm bt = {};
                // __DATE__ format: "May 19 2026" (constant 11 chars + NUL)
                // __TIME__ format: "16:47:46"   (constant 8 chars + NUL)
                const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
                const char* bd = __DATE__;
                const char* bt_s = __TIME__;
                for (int i = 0; i < 12; ++i) {
                    if (bd[0] == months[i*3] && bd[1] == months[i*3+1] && bd[2] == months[i*3+2]) {
                        bt.tm_mon = i;
                        break;
                    }
                }
                bt.tm_mday = atoi(bd + 4);
                bt.tm_year = atoi(bd + 7) - 1900;
                bt.tm_hour = atoi(bt_s);
                bt.tm_min  = atoi(bt_s + 3);
                bt.tm_sec  = atoi(bt_s + 6);
                bt.tm_wday = 0;  // ignored by DS1307.set_time

                if (g_rtc.set_time(bt) == ESP_OK) {
                    AC_LOGW(TAG, "RTC seeded from build time: %04d-%02d-%02d %02d:%02d:%02d",
                            bt.tm_year + 1900, bt.tm_mon + 1, bt.tm_mday,
                            bt.tm_hour, bt.tm_min, bt.tm_sec);
                } else {
                    AC_LOGE(TAG, "Failed to seed RTC from build time");
                }
            }
        }
    }
#if AC_VERBOSE_BOOT
    if (shtw_err == ESP_OK) {
        aqua::drivers::Sht3xSample s = {};
        if (g_sht_water.read(&s) == ESP_OK) {
            AC_LOGI(TAG, "SHT3x water:   T=%.2f °C  RH=%.1f %%", s.temp_c, s.humidity);
        }
    }
    if (shta_err == ESP_OK) {
        aqua::drivers::Sht3xSample s = {};
        if (g_sht_ambient.read(&s) == ESP_OK) {
            AC_LOGI(TAG, "SHT3x ambient: T=%.2f °C  RH=%.1f %%", s.temp_c, s.humidity);
        }
    }
#endif

    // 8a-pre. Phase 3.5 — start sensor sampler before scheduler so the cache
    // is warm by the time TempTriggers ask for it.
    if (shtw_err == ESP_OK || shta_err == ESP_OK) {
        aqua::sensors::Config ssc{};
        ssc.water        = (shtw_err == ESP_OK) ? &g_sht_water   : nullptr;
        ssc.ambient      = (shta_err == ESP_OK) ? &g_sht_ambient : nullptr;
        ssc.interval_ms  = 5000;
        ssc.bus          = &g_i2c_bus;
        ssc.water_addr   = shtw_addr;
        ssc.ambient_addr = shta_addr;
        aqua::sensors::start(ssc);
        // Apply saved calibration offsets immediately.
        aqua::sensors::apply_calibration(
            g_sys_cfg.water_cal_offset_c, g_sys_cfg.ambient_cal_offset_c);
    }

    // 8a. Phase 3 — bind RTC to TimeManager so the scheduler has local time.
    bool   time_synced = false;
    if (rtc_err == ESP_OK) {
        aqua::time_mgr::TimeManager::bind_rtc(&g_rtc);
        aqua::time_mgr::TimeManager::set_utc_offset_minutes(g_sys_cfg.utc_offset_min);
        time_synced = aqua::time_mgr::TimeManager::sync_system_from_rtc() == ESP_OK;
        if (time_synced) {
#if AC_VERBOSE_BOOT
            struct tm lt = aqua::time_mgr::TimeManager::now_local();
            AC_LOGI(TAG, "TimeManager: local %04d-%02d-%02d %02d:%02d:%02d",
                    lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                    lt.tm_hour, lt.tm_min, lt.tm_sec);
#endif
        } else {
            AC_LOGW(TAG, "TimeManager: sync from RTC failed");
        }
    }

    // 8b. Phase 3 — config: try to load saved devices/triggers from NVS.
    //     If nothing is saved yet, fall back to a hardcoded test setup so
    //     we can verify the trigger/scheduler pipeline end-to-end on real
    //     hardware. Once Phase 4 wizard runs and saves config, the test
    //     block is bypassed.
    if (pcf_err == ESP_OK) {
        aqua::storage::DriverContext drv_ctx{};
        drv_ctx.pcf = &g_relays;
        drv_ctx.pca = (pca_err == ESP_OK) ? &g_pwm : nullptr;
        aqua::storage::load_devices(g_devices, drv_ctx);
        aqua::storage::load_triggers(g_triggers);

        if (g_devices.size() == 0) {
            AC_LOGI(TAG, "No saved devices — start with empty config (configure via UI)");
        }

        aqua::scheduler::Config scfg{};
        scfg.dm = &g_devices;
        scfg.tm = &g_triggers;
        scfg.pre_eval = [&g_sys_cfg, &g_sys_cfg_mux](aqua::triggers::TriggerManager& tm) {
            // 1. Push the latest cached sensor readings into every TempTrigger
            //    before the scheduler evaluates them. Triggers whose sensor is
            //    missing from the cache get `sensor_present=false`.
            auto water   = aqua::sensors::get(aqua::sensors::Role::WATER);
            auto ambient = aqua::sensors::get(aqua::sensors::Role::AMBIENT);
            // Stale reading guard: if the sampler task has stalled (no new
            // successful read for > 3× the nominal 5 s interval) the cache
            // still holds valid=true with an arbitrarily old timestamp.
            // Treat that as invalid so temperature triggers fail safe.
            static constexpr uint64_t kSensorStaleMs = 15000ULL;  // 3 × 5 s
            if (water.valid  && aqua::sensors::age_ms(aqua::sensors::Role::WATER)   > kSensorStaleMs) water.valid  = false;
            if (ambient.valid && aqua::sensors::age_ms(aqua::sensors::Role::AMBIENT) > kSensorStaleMs) ambient.valid = false;
            tm.for_each([&](aqua::triggers::ITrigger& t) {
                if (t.get_type() == aqua::triggers::TriggerType::TEMP) {
                    auto& tt = static_cast<aqua::triggers::TempTrigger&>(t);
                    const auto& src = (tt.sensor_id == 1) ? ambient : water;
                    tt.update_temperature(src.temp_c, src.valid);
                } else if (t.get_type() == aqua::triggers::TriggerType::TEMP_MAP) {
                    auto& mt = static_cast<aqua::triggers::TempMapTrigger&>(t);
                    const auto& src = (mt.sensor_id == 1) ? ambient : water;
                    mt.update_temperature(src.temp_c, src.valid);
                }
            });

            // 2. Daily solar recompute: if the local date changed since the
            //    last pass, or a recalculation was explicitly requested (e.g.
            //    after NTP sync or lat/lon change), recompute sunrise/sunset
            //    and broadcast to all SolarTriggers.
            static int s_last_yday = -1;
            struct tm now = aqua::time_mgr::TimeManager::now_local();
            if (now.tm_yday != s_last_yday || aqua::solar::consume_recalc()) {
                s_last_yday = now.tm_yday;
                // M-5: read correlated lat/lon/utc_offset_min under spinlock to
                // avoid torn reads if Core 1 (wizard/time_location_screen) writes
                // them concurrently.
                taskENTER_CRITICAL(&g_sys_cfg_mux);
                float  s5_lat = g_sys_cfg.latitude;
                float  s5_lon = g_sys_cfg.longitude;
                int    s5_ofs = g_sys_cfg.utc_offset_min;
                taskEXIT_CRITICAL(&g_sys_cfg_mux);
                auto dr = aqua::solar::compute(now.tm_year + 1900, now.tm_mon + 1,
                                               now.tm_mday,
                                               s5_lat, s5_lon,
                                               s5_ofs);
                tm.for_each([&](aqua::triggers::ITrigger& t) {
                    if (t.get_type() != aqua::triggers::TriggerType::SOLAR) return;
                    auto& st = static_cast<aqua::triggers::SolarTrigger&>(t);
                    st.sunrise_min_today = dr.sunrise_min;
                    st.sunset_min_today  = dr.sunset_min;
                    st.valid             = dr.valid;
                });
                if (dr.valid) {
                    AC_LOGI("solar", "today: sunrise=%02d:%02d sunset=%02d:%02d (lat=%.4f lon=%.4f)",
                            dr.sunrise_min / 60, dr.sunrise_min % 60,
                            dr.sunset_min  / 60, dr.sunset_min  % 60,
                            (double)s5_lat, (double)s5_lon);
                } else {
                    AC_LOGW("solar", "today: no sunrise/sunset (polar) at lat=%.4f",
                            (double)s5_lat);
                }
            }
        };
        // M3: event-driven dashboard refresh — Core 0 notifies Core 1 via lv_async_call.
        scfg.on_device_changed = aqua::ui::dashboard::notify_device_changed;
        // Analog (TEMP_MAP) post-eval pass: apply level to linked PWM/RGB devices.
        scfg.post_eval = [](aqua::triggers::TriggerManager& tm,
                            aqua::devices::DeviceManager& dm) {
            tm.for_each([&](aqua::triggers::ITrigger& t) {
                if (t.get_type() != aqua::triggers::TriggerType::TEMP_MAP || !t.enabled) return;
                auto& mt = static_cast<aqua::triggers::TempMapTrigger&>(t);
                const float level = mt.eval_level();
                for (uint8_t did : mt.linked_device_ids) {
                    auto* dev = dm.find(did);
                    if (!dev) continue;
                    // C-4: relay devices have no analog output; skip to prevent freeze.
                    if (dev->get_type() == aqua::devices::DeviceType::RELAY) {
                        AC_LOGW(TAG, "TEMP_MAP trigger %u linked to relay %u — skipping",
                                (unsigned)mt.id, (unsigned)did);
                        continue;
                    }
                    bool prev = dev->current_active();
                    dev->apply_analog(level);
                    if (dev->current_active() != prev) {
                        aqua::ui::dashboard::notify_device_changed();
                        if (aqua::mqtt::connected())
                            aqua::mqtt::publish_device_state(*dev);
                    }
                }
            });
        };
        // H1: heater safety — propagate settings from SystemConfig.
        scfg.heater_device_id       = g_sys_cfg.heater_device_id;
        scfg.heater_fault_timeout_s = g_sys_cfg.heater_fault_timeout_s;
        scfg.heater_max_temp_c      = g_sys_cfg.heater_max_temp_c;
        if (!aqua::scheduler::start(scfg)) {
            AC_LOGE(TAG, "Scheduler failed to start");
        }
    } else {
        AC_LOGW(TAG, "PCF8575 not present — skipping device/scheduler init");
    }

    // 9. I2C watchdog (low-priority Core 0 task).
    aqua::i2c::WatchdogConfig wcfg = {};
    wcfg.bus         = &g_i2c_bus;
    wcfg.interval_ms = 5000;
    wcfg.port        = AC_I2C_PORT;
    wcfg.sda         = AC_I2C_SDA_PIN;
    wcfg.scl         = AC_I2C_SCL_PIN;
    wcfg.freq_hz     = AC_I2C_FREQ_HZ;
    g_i2c_wdg.start(wcfg, [](const aqua::i2c::WatchdogEvent& ev) {
        AC_LOGW("main", "watchdog: dev=%u addr=0x%02X faulted=%d",
                (unsigned)ev.id, ev.addr, ev.faulted);
        AC_DUMP_BEGIN("i2c-watchdog");
        AC_DUMP("device=%u  addr=0x%02X  state=%s",
                (unsigned)ev.id, ev.addr,
                ev.faulted ? "FAULTED" : "RECOVERED");
        AC_DUMP_END("i2c-watchdog");

        // Bridge into the global fault registry. Code = 0x0100 | KnownDevice
        // index so the UI can display per-device I2C faults.
        char label[40];
        snprintf(label, sizeof(label), "I2C dev %u @ 0x%02X",
                 (unsigned)ev.id, ev.addr);
        aqua::faults::set((uint16_t)(0x0100 | (uint16_t)ev.id),
                          aqua::faults::Source::I2C,
                          label, ev.faulted);
        aqua::history::append(
            ev.faulted ? aqua::history::EventType::FAULT_RAISE
                       : aqua::history::EventType::FAULT_CLEAR,
            0, 0, label);
    });

    AC_LOGI(TAG, "Boot complete. Free heap: %lu, PSRAM: %lu",
            (unsigned long)esp_get_free_heap_size(),
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // 10. Copy-pasteable boot diagnostics block. Gated on AC_DEBUG_DUMP_ENABLED.
    AC_DUMP_BEGIN("boot-summary");
    AC_DUMP("Firmware:      %s v%s", AC_FIRMWARE_NAME, AC_FIRMWARE_VERSION);
    AC_DUMP("Free heap:     %lu", (unsigned long)esp_get_free_heap_size());
    AC_DUMP("Free PSRAM:    %lu",
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    AC_DUMP("I2C scan:      %u responder(s)", (unsigned)scan.total_responders);
    for (size_t i = 0; i < (size_t)KnownDevice::COUNT; ++i) {
        const auto& d = scan.devices[i];
        AC_DUMP("  - %-14s @ 0x%02X  %s%s",
                d.name, d.addr,
                d.present ? "PRESENT" : "MISSING",
                d.critical ? "  [critical]" : "");
    }
    AC_DUMP("Display:       800x480 RGB  backlight=80%%");
    AC_DUMP("Touch:         GT911 %s (init=%s)",
            terr == ESP_OK ? "found" : "MISSING",
            esp_err_to_name(terr));
    if (terr == ESP_OK) {
        AC_DUMP("  address:     0x%02X", aqua::touch::detected_address());
    }
    AC_DUMP("Drivers:");
    AC_DUMP("  PCF8575:     %s", esp_err_to_name(pcf_err));
    AC_DUMP("  PCA9685:     %s", esp_err_to_name(pca_err));
    AC_DUMP("  DS1307:      %s", esp_err_to_name(rtc_err));
    AC_DUMP("  SHT3x water: %s%s",
            esp_err_to_name(shtw_err),
            shtw_promoted ? "  (promoted from 0x45)" : "");
    AC_DUMP("  SHT3x amb.:  %s", esp_err_to_name(shta_err));
    AC_DUMP("Phase 3 status:");
    AC_DUMP("  NVS:         %s", esp_err_to_name(nvs_err));
    AC_DUMP("  Time synced: %s", time_synced ? "yes" : "no");
    AC_DUMP("  Devices:     %u", (unsigned)g_devices.size());
    AC_DUMP("  Triggers:    %u", (unsigned)g_triggers.size());
    AC_DUMP("  Scheduler:   %s",
            aqua::scheduler::is_running() ? "RUNNING" : "stopped");
    AC_DUMP("  Sensors:     %s", aqua::sensors::is_running() ? "sampling" : "off");
    AC_DUMP("  Faults:      %u active", (unsigned)aqua::faults::active_count());
    AC_DUMP_END("boot-summary");

    // 10a. WiFi: start based on saved credentials, or AP fallback (first run).
    //      Respects the wifi_enabled flag from system config.
    //      Must run before the wizard so scanning is available immediately.
    //
    //      Register a failure callback so that if STA exhausts all retries the
    //      wifi_enabled flag is cleared and persisted automatically.
    aqua::wifi::set_sta_failure_callback([](void* arg) {
        auto& cfg = *static_cast<aqua::storage::SystemConfig*>(arg);
        cfg.wifi_enabled = false;
        aqua::storage::save_system_config(cfg);
        aqua::mqtt::stop();  // no WiFi → no point keeping the MQTT client alive
        AC_LOGW("main", "WiFi STA exhausted — wifi_enabled cleared, MQTT stopped");
    }, &g_sys_cfg);

    // Start NTP whenever an IP address is obtained (Issue #1).
    // The callback runs in the esp_event_loop task (Core 0), which is safe
    // for esp_sntp_init().  lv_async_call() is NOT needed here because
    // ntp::start() does not touch LVGL.
    aqua::wifi::set_got_ip_callback([](void* arg) {
        auto& cfg = *static_cast<aqua::storage::SystemConfig*>(arg);
        aqua::ntp::start(cfg.ntp1, cfg.ntp2);
        aqua::history::append(aqua::history::EventType::WIFI_CONNECT, 0, 0,
                              aqua::wifi::ip_string().c_str());
        aqua::web_portal::stop_deferred();  // AP portal no longer needed
        // NOTE: Do NOT call mqtt::start() here. sys_evt stack is ~2.8 kB and
        // esp_mqtt_client_init() will overflow it. MQTT auto-reconnects via
        // its own task if already started. Re-enabling WiFi via the network
        // settings screen handles the MQTT restart for the STA-exhausted case.
    }, &g_sys_cfg);

    if (g_sys_cfg.wifi_enabled) {
        aqua::wifi::StationCfg wsc;
        if (aqua::wifi::load_station_cfg(&wsc) == ESP_OK && !wsc.ssid.empty()) {
            aqua::wifi::start_station(wsc);
        } else {
            aqua::wifi::start_ap_fallback();
            aqua::web_portal::start({&g_devices});
        }
    } else {
        AC_LOGI("main", "WiFi disabled by user setting — skipping WiFi start");
    }

    // 10b. MQTT: auto-start from saved credentials if mqtt_enabled.
    //      Command callback (B3) and HA discovery (B4) are wired here so they
    //      are active even after a broker reconnect.
    if (g_sys_cfg.mqtt_enabled && g_sys_cfg.wifi_enabled) {
        aqua::mqtt::Config mcfg;
        if (aqua::mqtt::load_config(&mcfg) == ESP_OK && !mcfg.uri.empty()) {
            // Derive a stable node_id from the last 2 MAC octets so all HA
            // entities from this device share a unique device identifier.
            {
                uint8_t mac[6] = {};
                esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
                char nid[24];
                snprintf(nid, sizeof(nid), "aquactl_%02X%02X", mac[4], mac[5]);
                mcfg.node_id = nid;
            }

            // B3: MQTT command → device override.
            aqua::mqtt::set_command_callback(
                [](uint8_t dev_id, const std::string& payload) {
                    bool on_off = (payload == "ON");
                    auto* dev = g_devices.find(dev_id);
                    if (dev) {
                        // INDEFINITE so the override persists even when the
                        // schedule disagrees (UNTIL_NEXT is cleared immediately
                        // if a trigger would keep the device in the opposite state).
                        dev->set_override(aqua::devices::OverrideMode::INDEFINITE,
                                          on_off, 0);
                        aqua::scheduler::wake_now();
                    }
                });

            // B4: HA discovery on (re)connect — guarded to avoid a storm.
            // H-4: re-publish only when the device config has changed or on first connect.
            aqua::mqtt::set_connect_callback([]() {
                static bool     s_discovery_done = false;
                static uint32_t s_last_dev_ver   = UINT32_MAX;
                uint32_t cur_ver = g_devices.config_version();
                if (s_discovery_done && cur_ver == s_last_dev_ver) {
                    // Config unchanged since last discovery run — skip the burst.
                    AC_LOGI("main", "MQTT reconnect: HA discovery skipped (config unchanged)");
                } else {
                    g_devices.for_each([](aqua::devices::IDevice& dev) {
                        aqua::mqtt::publish_ha_discovery(dev);
                        // Publish current state immediately so HA doesn't show
                        // "Unknown" until the next scheduler-driven change.
                        aqua::mqtt::publish_device_state(dev);
                    });
                    // Sensor discovery: sensor 0 = water, sensor 1 = ambient.
                    aqua::mqtt::publish_ha_sensor_discovery(0, "AquaCtl Water");
                    aqua::mqtt::publish_ha_sensor_discovery(1, "AquaCtl Ambient");
                    // System status sensors (uptime, heap, WiFi RSSI/IP, faults).
                    aqua::mqtt::publish_ha_system_discovery();
                    s_discovery_done = true;
                    s_last_dev_ver   = cur_ver;
                }
                // Announce (re)connect so HA automations can react.
                aqua::mqtt::publish_event("mqtt_connect", "broker connected");
            });

            aqua::mqtt::start(mcfg);
            boot_line("MQTT client started", true);
        } else {
            AC_LOGI("main", "MQTT: no saved config — skipping MQTT start");
        }
    }

    // 11. Extended boot summary lines before handing off to the dashboard.
    {
        char line[96];
        snprintf(line, sizeof(line), "Devices loaded: %u  Triggers: %u",
                 (unsigned)g_devices.size(), (unsigned)g_triggers.size());
        aqua::ui::boot_screen_log(line, "OK");

        snprintf(line, sizeof(line), "Free heap: %lu B  Min free: %lu B",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
        aqua::ui::boot_screen_log(line);

        if (g_sys_cfg.wifi_enabled) {
            aqua::wifi::StationCfg sta_cfg;
            if (aqua::wifi::load_station_cfg(&sta_cfg) == ESP_OK && !sta_cfg.ssid.empty()) {
                snprintf(line, sizeof(line), "WiFi: STA SSID \"%s\"  hostname AquaControl",
                         sta_cfg.ssid.c_str());
            } else {
                snprintf(line, sizeof(line), "WiFi: AP mode  SSID \"%s\"",
                         aqua::wifi::ap_ssid().c_str());
            }
            aqua::ui::boot_screen_log(line);
        }
        if (g_sys_cfg.mqtt_enabled) {
            aqua::ui::boot_screen_log("MQTT: enabled", "OK");
        }
    }

    // Wait for any tap-to-pause to be released, then hold 2 s.
    aqua::ui::boot_screen_finish(2000);

    // 12. Install UI context, swap boot console for the dashboard
    //     and start the inactivity-dim manager. Touch is already wired into
    //     the LVGL input device, so taps will register as activity.
    aqua::ui::UiContext uictx{};
    uictx.devices  = &g_devices;
    uictx.triggers = &g_triggers;
    uictx.sys_cfg  = &g_sys_cfg;
    uictx.drv_pcf  = (pcf_err == ESP_OK) ? &g_relays : nullptr;
    uictx.drv_pca  = (pca_err == ESP_OK) ? &g_pwm    : nullptr;
    aqua::ui::set_ui_context(uictx);

    if (lv_obj_t* dash = aqua::ui::dashboard::build()) {
        // NONE (no animation) avoids large-area tearing on the
        // single-FB RGB panel; FADE redraws the entire screen many
        // times while the panel is scanning it out -> visible noise.
        aqua::ui::screen_manager::replace(
            dash, aqua::ui::screen_manager::Transition::NONE);
        AC_LOGI(TAG, "Dashboard loaded");

        // Slice 4: first-run wizard — launch if setup not complete.
        if (!g_sys_cfg.first_run_complete) {
            if (lv_obj_t* wiz = aqua::ui::wizard::build()) {
                aqua::ui::screen_manager::push(
                    wiz, aqua::ui::screen_manager::Transition::FADE);
                AC_LOGI(TAG, "First-run wizard launched");
            }
        }
    } else {
        AC_LOGE(TAG, "Dashboard build failed — keeping boot screen");
    }

    aqua::ui::dim::start(&g_sys_cfg);

    // Idle loop — periodically print heap stats + cached sensor readings.
    // B5: periodic MQTT sensor + system-status publish every 60 s (6 × 10 s).
    int mqtt_sensor_ticks = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        const uint64_t idle_raw = aqua::activity::idle_ms();
        if (idle_raw == UINT64_MAX) {
            AC_LOGI(TAG, "alive  heap=%lu  psram=%lu  idle=never  faults=%u",
                    (unsigned long)esp_get_free_heap_size(),
                    (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                    (unsigned)aqua::faults::active_count());
        } else {
            AC_LOGI(TAG, "alive  heap=%lu  psram=%lu  idle=%llums  faults=%u",
                    (unsigned long)esp_get_free_heap_size(),
                    (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                    (unsigned long long)idle_raw,
                    (unsigned)aqua::faults::active_count());
        }

        auto water   = aqua::sensors::get(aqua::sensors::Role::WATER);
        auto ambient = aqua::sensors::get(aqua::sensors::Role::AMBIENT);
        if (water.valid) {
            AC_LOGI(TAG, "cache water:   T=%.2f  RH=%.1f  age=%llums",
                    water.temp_c, water.humidity,
                    (unsigned long long)aqua::sensors::age_ms(aqua::sensors::Role::WATER));
        }
        if (ambient.valid) {
            AC_LOGI(TAG, "cache ambient: T=%.2f  RH=%.1f  age=%llums",
                    ambient.temp_c, ambient.humidity,
                    (unsigned long long)aqua::sensors::age_ms(aqua::sensors::Role::AMBIENT));
        }

        // B5: publish sensor readings + system status to MQTT every 60 s.
        if (++mqtt_sensor_ticks >= 6) {
            mqtt_sensor_ticks = 0;
            if (aqua::mqtt::connected()) {
                if (water.valid)
                    aqua::mqtt::publish_sensor(0, water.temp_c, water.humidity);
                if (ambient.valid)
                    aqua::mqtt::publish_sensor(1, ambient.temp_c, ambient.humidity);

                // System status: uptime, heap, WiFi, fault count.
                uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
                aqua::mqtt::publish_system_status(
                    uptime_s,
                    (uint32_t)esp_get_free_heap_size(),
                    aqua::wifi::is_connected(),
                    aqua::wifi::sta_rssi(),
                    aqua::wifi::ip_string().c_str(),
                    aqua::faults::active_count());
            }
        }
    }
}
