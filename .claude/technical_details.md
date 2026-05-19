> **Note:** This document is the original requirements/spec. Where requirements have been refined during planning (e.g., relay count is now configurable 1–16 with default 5; two SHT30 sensors at 0x44 + 0x45; PWM frequency 1 kHz; etc.), the authoritative source is `.claude/implementation_plan.md` §11 "Decisions Made".

## Goals
- Create an app to control the aquarium hardware (AquaControl). 

## Hardware
- main unit ElecRow 5 inch ESP32 based display. spec: 
resulution: 800*480 (Capacitive Touch)
CPU: ESP32-S3-WROOM-1-N4R8, 240 MHz, 4MB, 512KB, 384KB, PSRAM 8MB
display driver: ILI6122 & ILI5960, TFT
Interface to external hardware: I2C, shared with TFT touch

## External hardware controllers (connected thru I2C to main unit):
- PCF8575 I2C to control simle relay outputs (5 relays)
- PCA9685 I2C to control PWM outputs: 1. 5 sinlge channels PWM shoud have the options to on/off, set level and fade-in, fade-out function where we can specify duration of fade in minutes (0 - immideately switched on/off - to 360 - slowly on or off during this interval); 2. 2 combined PWM outputs to control RGB Led strip also over the same PCA9685 I2C interface. Each of two this outputs simple combine together 3 PCA9685 outputs to control R, G, B components of the Led strip.
- DS1307 clock module to maintain realtime clock in absence of NTP sync
- SHT30/31 I2C temperature sensors (ambient and water temp measurements). Device should be able to identify other I2C temp/hum sensor connect on the fly and provide settings for found ones.

## Programming stack
- official ESP-IDF >= 5.3 as framework
- LVGL v9.2 or better latest 9.5 for interface

## What user expects from this device:
- main idea is to have two layers - devices and triggers. Devices are those listed above. Triggers are: a) configurable stop/start time b) solar time with ability to specify offset. c) Temperature trigger. 
- Each device is connected by user to one or more triggers.
- All settings must be stored in the ERAM and restored on reboot. 
- Each device can be active/disabled
- Each trigger can be active/disabled
- Wifi connection is optional. When connected it should allow to a) communicante to HomeAssistent over MQTT discovery. b) Sync time to prespecified NTPs. When Wifi is disable the device must function locally (time is configured by user and stored to DS1307, Solar coordinates are configured by user, and sunrise/sunset is calculated on device)

## UI/UX expected screens:
1. On boot - like linux system boot console with on-the-fly checks of all the systems/devices/conections etc.
2. Main screen (dashboard) which is always visible by default and provides general info about current status. After timeout of inactivity (configured) the screen should be dimmed to the specified level. In case of any faults in the system it should became bright and display error.
3. Settigns (accessible from main screen):
  a) general system settings: Wifi, NTP, Lattitude/Longitude, Dimming timeout and everything else you think user can setup
  b) Devices configurations (well structured per device/output)
  c) Triggers configs (all required settigns per each trigger type plus assignment to devices)
  d) System status (logs/errors etc.)

You must think about best possible, modern visually correct LVGL interface that is fast and fit into the screen size of the device. Do not be simple - use styling, colors, icons that can be downloaded. Use no more that 4 sizes of the fonts and no more than 2 font faces. 
Interface must allow to switch language between English and Russian - you must consider this for LVGL setup and during programming. 


## Additional investigation you should make:
- Check internet for any skills you can use for this project
- Check current environment and what we need to install/setup to complete our tasks
- We do not have any hardware to debug code inside esp. You need to embed a logger that can be swtched off finally to be able to debug over serial USB connection.
- do a careful investigation on how to program such displays. For UI/UX you need to consider that given display has limited memory, and you should be carefull of LVGL memory management!
- for I2C communication you should consider the writing the watchdogs and checks that all devices are responding and working! I2C is critical part here and any fail in devices communication could make fishes dies!
- Check the https://github.com/mccahan/esp32-display-claude-base repo and see if this would be helpful for you to develop quality UI/UX. You will need to adopt the repo for our display!
- for reference the following config was used for testing:
```
#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
// #include <driver/i2c.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Bus_RGB _bus_instance;
  lgfx::Panel_RGB _panel_instance;
  lgfx::Light_PWM _light_instance;
  //    lgfx::Touch_GT911 _touch_instance;

public:
  LGFX(void)
  {

    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      cfg.pin_d0 = GPIO_NUM_8;  // B0
      cfg.pin_d1 = GPIO_NUM_3;  // B1
      cfg.pin_d2 = GPIO_NUM_46; // B2
      cfg.pin_d3 = GPIO_NUM_9;  // B3
      cfg.pin_d4 = GPIO_NUM_1;  // B4

      cfg.pin_d5 = GPIO_NUM_5;  // G0
      cfg.pin_d6 = GPIO_NUM_6;  // G1
      cfg.pin_d7 = GPIO_NUM_7;  // G2
      cfg.pin_d8 = GPIO_NUM_15; // G3
      cfg.pin_d9 = GPIO_NUM_16; // G4
      cfg.pin_d10 = GPIO_NUM_4; // G5

      cfg.pin_d11 = GPIO_NUM_45; // R0
      cfg.pin_d12 = GPIO_NUM_48; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_21; // R3
      cfg.pin_d15 = GPIO_NUM_14; // R4

      cfg.pin_henable = GPIO_NUM_40;
      cfg.pin_vsync = GPIO_NUM_41;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_0;
      cfg.freq_write = 15000000;

      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch = 43;

      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 8;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch = 12;

      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;

      _bus_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = GPIO_NUM_2;
      _light_instance.config(cfg);
      _panel_instance.light(&_light_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.memory_width = 800;
      cfg.memory_height = 480;
      cfg.panel_width = 800;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    /*
    {
            auto cfg = _touch_instance.config();
            cfg.x_min      = 0;
            cfg.x_max      = 799;
            cfg.y_min      = 0;
            cfg.y_max      = 479;
            cfg.pin_int    = -1;
            cfg.pin_rst    = -1;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.i2c_port   = I2C_NUM_1;
            cfg.pin_sda    = GPIO_NUM_19;
            cfg.pin_scl    = GPIO_NUM_20;
            cfg.freq       = 400000;
            cfg.i2c_addr   = 0x14;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
*/
    setPanel(&_panel_instance);
  }
};

#endif
```
```
/* uncomment for GT911 */
 #define TOUCH_GT911
 #define TOUCH_GT911_SCL 20//20
 #define TOUCH_GT911_SDA 19//19
 #define TOUCH_GT911_INT -1//-1
 #define TOUCH_GT911_RST -1//38
 #define TOUCH_GT911_ROTATION ROTATION_NORMAL
 #define TOUCH_MAP_X1 800//480
 #define TOUCH_MAP_X2 0
 #define TOUCH_MAP_Y1 480//272
 #define TOUCH_MAP_Y2 0
```


