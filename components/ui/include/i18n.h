// AquaControl — Localization keys (Phase 1 stub extended in Phase 3.5 E17).
//
// Always call `tr(key)` in UI code — never embed raw user-visible strings.
// The full Phase 4 wizard / settings table is expected to add ~150 more
// keys; the values added here are the common-vocabulary set used across
// every screen and the dashboard, so we can author the rest of Phase 4
// without trickling churn back into this header.
#pragma once

#include <cstdint>

namespace aqua::ui::i18n {

enum class LangKey : uint16_t {
    // --- Boot / global ----------------------------------------------------
    BOOT_TITLE,
    BOOT_READY,

    // --- Common verbs / dialog buttons ------------------------------------
    BTN_OK,
    BTN_CANCEL,
    BTN_SAVE,
    BTN_DELETE,
    BTN_EDIT,
    BTN_BACK,
    BTN_NEXT,
    BTN_RETRY,
    BTN_YES,
    BTN_NO,

    // --- Navigation / screen titles ---------------------------------------
    NAV_DASHBOARD,
    NAV_DEVICES,
    NAV_TRIGGERS,
    NAV_SCHEDULES,
    NAV_SETTINGS,
    NAV_NETWORK,
    NAV_TIME,
    NAV_SENSORS,
    NAV_SYSTEM,
    NAV_HISTORY,
    NAV_LOGS,
    NAV_INFO,
    NAV_DISPLAY,

    // --- Device kinds & state --------------------------------------------
    DEV_RELAY,
    DEV_PWM,
    DEV_RGB,
    DEV_ENABLED,
    DEV_DISABLED,
    DEV_ON,
    DEV_OFF,
    DEV_OVERRIDE,
    DEV_BRIGHTNESS,
    DEV_COLOR,
    DEV_FADE_IN,
    DEV_FADE_OUT,

    // --- Trigger kinds ---------------------------------------------------
    TRG_SCHEDULE,
    TRG_SOLAR,
    TRG_TEMPERATURE,
    TRG_SUNRISE,
    TRG_SUNSET,
    TRG_OFFSET_MIN,
    TRG_DURATION_MIN,
    TRG_DAYS,

    // --- Sensors / units --------------------------------------------------
    SENSE_WATER,
    SENSE_AMBIENT,
    UNIT_C,
    UNIT_F,
    UNIT_PCT,
    UNIT_MIN,

    // --- Network / status -------------------------------------------------
    NET_WIFI,
    NET_STATION,
    NET_AP,
    NET_CONNECTED,
    NET_DISCONNECTED,
    NET_SSID,
    NET_PASSWORD,
    NET_MQTT,
    NET_NTP,

    // --- Faults / warnings -----------------------------------------------
    FAULT_BANNER,
    FAULT_I2C,
    FAULT_RTC,
    FAULT_NO_TIME,
    WARN_OVERLAP,
    WARN_NO_LOCATION,

    // --- First-run wizard -------------------------------------------------
    WIZ_WELCOME,
    WIZ_LANG,
    WIZ_LANG_NOTE,
    WIZ_TIME,
    WIZ_LOCATION,
    WIZ_WIFI,
    WIZ_RELAYS,
    WIZ_FIRST_RUN,
    WIZ_WIFI_ENABLE,
    WIZ_MQTT_ENABLE,
    WIZ_SET_TIME,
    WIZ_LAT,
    WIZ_LON,
    WIZ_SKIP,
    WIZ_APPLY,
    NET_WIFI_DISABLED_NOTE,

    // --- Wizard body text -----------------------------------------------
    WIZ_CHOOSE_LANG,     // "Choose your language"
    WIZ_LANG_BODY,       // "Select the language for the interface."
    WIZ_WELCOME_BODY,    // intro paragraph on welcome step
    WIZ_WIFI_PW_PH,      // placeholder text "WiFi password"
    WIZ_AP_NOTE,         // "Leave SSID blank to use AP-mode only."
    WIZ_MQTT_URI_LBL,    // "Broker URI  (e.g. mqtt://host:1883)"
    WIZ_OPTIONAL_USER,   // "Username (optional)"
    WIZ_OPTIONAL_PW,     // "Password (optional)"
    WIZ_UTC_LABEL,       // "UTC offset (hours : minutes)"
    WIZ_NTP1_LBL,        // "NTP Server 1"
    WIZ_NTP2_LBL,        // "NTP Server 2 (optional)"
    WIZ_LOC_NOTE,        // solar calc note with example coords
    WIZ_AP_MODE_ONLY,    // "AP mode only" (summary card)
    WIZ_READY_NOTE,      // post-wizard note
    WIZ_NO_NETWORKS,     // scan result: no nets
    WIZ_SCAN,            // scan button label "Scan"
    WIZ_SCANNING,        // scan in-progress label

    // --- Time & Location screen -----------------------------------------
    TZ_TITLE,            // section label "Timezone"
    TZ_UTC_OFFSET,       // field label "UTC offset (minutes)"
    TZ_NTP_TITLE,        // section label "NTP Servers"
    TZ_NTP1,             // "Primary NTP server"
    TZ_NTP2,             // "Secondary NTP server"
    TZ_LOC_TITLE,        // section label "Location  (for solar calculations)"
    TZ_LAT,              // "Latitude  (e.g. 55.75)"
    TZ_LON,              // "Longitude  (e.g. 37.61)"
    TIME_YEAR,
    TIME_MON,
    TIME_DAY,
    TIME_HOUR,
    TIME_MIN,
    TIME_CURRENT,        // "Current:"

    // --- Network settings screen ----------------------------------------
    NET_MQTT_USER,       // "Username  (optional)"
    NET_MQTT_PW,         // "Password  (optional)"
    NET_BASE_TOPIC,      // "Base topic"
    NET_HA_DISC,         // "HA MQTT Discovery"
    NET_SIGNAL,          // "Signal: %d dBm"
    NET_FORGET,          // "Forget Network"
    NET_RESCAN,          // "Rescan"

    // --- Trigger editor --------------------------------------------------
    TRG_NAME,            // field label "Name"
    TRG_NAME_PH,         // placeholder "trigger name"
    TRG_ADD,             // header button  "+ Add"
    TRG_EDIT_TITLE,      // screen title "Edit Trigger"
    TRG_ADD_TITLE,       // screen title "Add Trigger"
    TRG_DELETE_TITLE,    // msgbox title "Delete Trigger"
    TRG_DELETE_CONFIRM,  // msgbox body "Remove this trigger?"
    TRG_SELECT_TYPE,     // type chooser prompt
    TRG_NO_MGR,          // error label when TriggerManager unavailable
    TRG_NONE_YET,        // empty-state label
    TRG_START_TIME,      // "Start time  (HH:MM)"
    TRG_STOP_TIME,       // "Stop time  (HH:MM)"
    TRG_MODE,            // field label "Mode"
    TRG_INTERVAL_TYPE,   // "Interval type"
    TRG_INTERVAL_MIN,    // "Interval (min)"
    TRG_AT_TIME,         // "At time  (HH:MM)"
    TRG_ON_DURATION,     // "On duration (min)"
    TRG_ACTIVE_DAYS,     // section "Active days"
    TRG_EVENT,           // field "Event"
    TRG_END_MODE,        // field "End mode"
    TRG_END_EVENT,       // field "End event"
    TRG_END_OFFSET_MIN,  // "End offset (min)"
    TRG_SENSOR,          // field "Sensor"
    TRG_CONDITION,       // field "Condition"
    TRG_THRESHOLD,       // "Threshold temperature"
    TRG_HYSTERESIS,      // "Hysteresis (°C)"
    TRG_LINKED_DEVICES,  // section "Linked Devices"
    TRG_ABOVE,           // condition option "Above"
    TRG_BELOW,           // condition option "Below"

    // --- Day-of-week names (short) ----------------------------------------
    DAY_MON, DAY_TUE, DAY_WED, DAY_THU, DAY_FRI, DAY_SAT, DAY_SUN,

    // --- Device detail screen --------------------------------------------
    DEV_CURRENT_STATE,   // card title "CURRENT STATE"
    DEV_OVERRIDE_CTRL,   // card title "OVERRIDE CONTROL"
    DEV_MANUAL_CTRL,     // card title "MANUAL CONTROL"
    DEV_FADING_IN,       // state label "FADING IN"
    DEV_FADING_OUT,      // state label "FADING OUT"
    DEV_LEVEL,           // "Level"
    DEV_UNTIL_NEXT,      // override pill "Until next"
    DEV_FOLLOWS_TRG,     // hint "Following triggers"
    DEV_NO_HOLD,         // hint "No manual hold"
    DEV_PWM_SETTINGS,    // card title "PWM SETTINGS"
    DEV_RGB_SETTINGS,    // card title "RGB SETTINGS"
    DEV_RELAY_SETTINGS,  // card title "RELAY SETTINGS"
    DEV_ACTIVE_HIGH,     // toggle label "Active: HIGH (normal)"
    DEV_ACTIVE_LOW,      // toggle label "Active: LOW (inverted)"
    DEV_ROLE_SETTINGS,   // card title "DEVICE ROLE"
    DEV_ROLE_LABEL,      // row label "Role"
    DEV_NAME_SETTINGS,   // card title "DEVICE NAME"
    DEV_NAME_HINT,       // hint under textarea "Tap to edit, use EN/RU to switch layout"
    DEV_DELETE_TITLE,    // msgbox "Delete device?"
    DEV_DELETE_CONFIRM,  // msgbox body (uses %s printf format)
    DEV_NOT_LINKED,      // "This device is not linked to any trigger..."
    DEV_OVERRIDE_HINT,   // "Pick a duration to apply the override..."
    DEV_DEVICE,          // generic type word "Device"

    // --- RGB color preset names ------------------------------------------
    RGB_WARM, RGB_COOL, RGB_RED, RGB_GREEN, RGB_BLUE, RGB_MAGENTA, RGB_CYAN,

    // --- Display & System settings screen --------------------------------
    DISP_ACTIVE_BRT,     // "Active brightness"
    DISP_DIM_BRT,        // "Dim brightness"
    DISP_INACTIVITY,     // "Inactivity timeout"
    DISP_SECTION_DISP,   // section label "Display"
    DISP_SECTION_UNITS,  // section label "Units"
    DISP_SECTION_SETUP,  // section label "Setup"
    DISP_TEMP_UNIT,      // "Temperature unit"

    // --- System status screen --------------------------------------------
    SYS_FIRMWARE,        // section "Firmware"
    SYS_RESOURCES,       // section "Resources"
    SYS_ACTIVE_FAULTS,   // section "Active Faults"
    SYS_NO_FAULTS,       // "No active faults."
    SYS_RECENT_EVENTS,   // section "Recent Events"
    SYS_NO_EVENTS,       // "No events recorded."
    SYS_ACTIONS,         // section "Actions"
    SYS_FACTORY_RESET,   // button / msgbox title "Factory Reset"
    SYS_FACTORY_CONFIRM, // msgbox body

    // --- Sensors settings screen -----------------------------------------
    SENS_I2C_SECTION,    // section "I2C Addresses"
    SENS_WATER_SENSOR,   // "Water sensor"
    SENS_AMBIENT_SENSOR, // "Ambient sensor"
    SENS_SENSOR_ENABLED, // "Enabled"
    SENS_ADDR_NOTE,      // note about address change needing restart
    SENS_CAL_SECTION,    // section "Calibration Offset"
    SENS_CAL_NOTE,       // note about calibration
    SENS_WATER_CAL,      // "Water (°C)"
    SENS_AMBIENT_CAL,    // "Ambient (°C)"
    SENS_HISTORY_TITLE,  // "Water Temp History (12h)"
    SENS_HISTORY_NO_DATA, // "No data yet"

    WIZ_DONE,

    // --- Dashboard date / time strings ----------------------------------------
    DASH_TIME_NOT_SET,   // "time not set"
    DASH_SYNCING,        // "syncing time..."
    // Full weekday names (Sunday first, 0-indexed)
    DASH_WDAY_SUN, DASH_WDAY_MON, DASH_WDAY_TUE, DASH_WDAY_WED,
    DASH_WDAY_THU, DASH_WDAY_FRI, DASH_WDAY_SAT,
    // Short weekday (for date line)
    DASH_WDAY_SHORT_SUN, DASH_WDAY_SHORT_MON, DASH_WDAY_SHORT_TUE,
    DASH_WDAY_SHORT_WED, DASH_WDAY_SHORT_THU, DASH_WDAY_SHORT_FRI,
    DASH_WDAY_SHORT_SAT,
    // Month names (January first, 1-indexed — key 0 = Jan = index 0)
    DASH_MON_JAN, DASH_MON_FEB, DASH_MON_MAR, DASH_MON_APR,
    DASH_MON_MAY, DASH_MON_JUN, DASH_MON_JUL, DASH_MON_AUG,
    DASH_MON_SEP, DASH_MON_OCT, DASH_MON_NOV, DASH_MON_DEC,

    TZ_SYNC_NOW,         // button label "Sync Now"
    TZ_SYNC_STARTED,     // feedback label "Sync started..."
    TZ_SYNC_WIFI_OFF,    // feedback label "Wi-Fi is off"

    // --- Dashboard status strings (added Phase 4 fix) --------------------
    DASH_NO_DATA,        // "No data"
    DASH_SET_LOCATION,   // "Set location"
    DASH_POLAR,          // "Polar"
    DASH_SEC_AGO,        // "%us ago"   (%u = seconds)
    DASH_MIN_AGO,        // "%um ago"   (%u = minutes)
    DASH_NONE_YET,       // "None yet"
    DASH_DEV_COUNT,      // "%u / %u on"  (on / total devices)

    // --- System screen additions -----------------------------------------
    SYS_RESTART,         // "Restart"
    SYS_NOT_CONNECTED,   // "Not connected"

    // --- Add Device screen -----------------------------------------------
    ADD_DEV_TITLE,            // screen title "Add Device"
    ADD_DEV_TYPE,             // label "Type"
    ADD_DEV_CHANNEL,          // label "Channel"
    ADD_DEV_CONFLICT_TITLE,   // msgbox title "Channel conflict"
    ADD_DEV_CONFLICT_1,       // "Channel %d is already used by another %s device."
    ADD_DEV_CONFLICT_RANGE,   // "Channels %d-%d are already used by another device."

    // --- Display settings timeout options --------------------------------
    DISP_TIMEOUT_30S,    // "30 seconds"
    DISP_TIMEOUT_1M,     // "1 minute"
    DISP_TIMEOUT_2M,     // "2 minutes"
    DISP_TIMEOUT_5M,     // "5 minutes"
    DISP_TIMEOUT_10M,    // "10 minutes"
    DISP_TIMEOUT_NEVER,  // "Never"

    // --- Device detail extra strings -------------------------------------
    DEV_TRIGGER_DRIVEN,  // "trigger driven"
    DEV_MANUAL_ONLY,     // "manual only"
    DEV_HELD,            // "Held"
    DEV_FOR_MINUTES,     // "for %dm"
    DEV_UNTIL_NEXT_TRG,  // "until next trigger"
    DEV_TURN_ON,         // "Turn ON"
    DEV_TURN_OFF,        // "Turn OFF"
    DEV_HOLD,            // "Hold"
    DEV_CANCEL_HOLD,     // "Cancel hold"
    DEV_TOGGLE,          // "Toggle"

    // --- Boot screen -----------------------------------------------------
    BOOT_TAP_PAUSE,      // "tap to pause"

    // --- Network settings ------------------------------------------------
    NET_OPEN_HINT,       // "(leave blank for open network)"

    // --- Solar trigger end-mode value labels (beside sw_use_end_event) ---
    TRG_END_MODE_DUR,    // "Duration"    — switch OFF (duration mode)
    TRG_END_MODE_EVT,    // "Until event" — switch ON  (end-event mode)

    // --- Schedule trigger mode labels ------------------------------------
    TRG_SCHED_MODE_WINDOW,   // "Start→End"
    TRG_SCHED_MODE_ONCE,     // "Daily at"
    TRG_SCHED_MODE_EVERY,    // "Every N"
    TRG_EVERY,               // field label "Every"
    TRG_DURATION,            // field label "Duration"
    TRG_UNIT_SEC,            // "sec"
    TRG_UNIT_MIN,            // "min"
    TRG_UNIT_HR,             // "hr"
    TRG_ERR_DUR_OVERLAP,     // overlap error label

    // --- Temp-Map trigger specific ----------------------------------------
    TRG_TMAP_HYSTERESIS,     // "Hysteresis (°C)"
    TRG_TMAP_TEMP_LO,        // "Temp Lo (°C)"
    TRG_TMAP_TEMP_HI,        // "Temp Hi (°C)"
    TRG_TMAP_INFO,           // info text explaining linear mapping
    TRG_TMAP_RELAY_SKIP,     // note: relay devices excluded from device list
    TRG_TMAP_REVERSE,        // "Reverse (Hi→Lo)"

    // --- Device detail — RGB / PWM labels ---------------------------------
    DEV_COLOR_HI,            // "Color Hi  (ON)"
    DEV_COLOR_LO,            // "Color Lo  (analog min)"
    DEV_HUE,                 // "H°"
    DEV_SAT,                 // "Sat"
    DEV_VAL,                 // "Val"
    DEV_LEVEL_LO,            // "Level Lo"
    DEV_LEVEL_LO_HINT,       // hint text below Level Lo in PWM card

    // --- New keys (audit item 21) -----------------------------------------
    UNIT_RH_PCT,             // humidity sub-label format string (M-2)
    VAL_TEMPMAP_RELAY_ERROR, // validator error: relay linked to TEMP_MAP (C-4)
    MSG_SAVED,               // save toast message (UI-4)
    NTP_FAULT_MSG,           // NTP staleness fault label (M-1)

    DISP_SCREENSAVER,        // "Screensaver clock" toggle label

    LANG_KEY_COUNT,  // sentinel — keep LAST
};

enum class Language : uint8_t {
    EN = 0,
    RU = 1,
};

void set_language(Language lang);
Language get_language();

const char* tr(LangKey key);

}  // namespace aqua::ui::i18n
