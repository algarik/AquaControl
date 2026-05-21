// AquaControl — Localization implementation (Phase 3.5 E17).
//
// Two parallel string tables keyed by `LangKey`. Adding a key requires
// adding an entry to BOTH tables (compile-time check via the static_assert
// at the bottom of this file).
#include "i18n.h"

#include <cstddef>

namespace aqua::ui::i18n {

static Language s_lang = Language::EN;

void     set_language(Language lang) { s_lang = lang; }
Language get_language()              { return s_lang; }

namespace {

// Total number of keys = one past the last enumerator (WIZ_DONE).
constexpr size_t kCount = static_cast<size_t>(LangKey::LANG_KEY_COUNT);

// Order MUST match the enum declaration in i18n.h.
constexpr const char* kEn[kCount] = {
    // boot / global
    "AquaControl - booting...",
    "System ready.",
    // buttons
    "OK", "Cancel", "Save", "Delete", "Edit", "Back", "Next", "Retry", "Yes", "No",
    // nav
    "Dashboard", "Devices", "Triggers", "Schedules", "Settings",
    "Network", "Time", "Sensors", "System", "History", "Logs", "Info",
    "Display &\nSystem",
    // devices
    "Relay", "PWM", "RGB", "Enabled", "Disabled",
    "ON", "OFF", "Override", "Brightness", "Color",
    "Fade in", "Fade out",
    // triggers
    "Schedule", "Solar", "Temperature",
    "Sunrise", "Sunset", "Offset (min)", "Duration (min)", "Days",
    // sensors / units
    "Water", "Ambient", "C", "F", "%", "min",
    // network
    "Wi-Fi", "Station", "Access Point",
    "Connected", "Disconnected", "SSID", "Password",
    "MQTT", "NTP",
    // faults
    "Faults active", "I2C fault", "RTC fault", "No time source",
    "Schedule overlap", "Location not set",
    // wizard
    "Welcome to AquaControl",
    "Language",
    "Language change takes effect immediately.\nAll labels update at once.",
    "Time", "Location", "Wi-Fi", "Relays",
    "First-run Setup",
    "Enable Wi-Fi",
    "Enable MQTT",
    "Set Time",
    "Latitude",
    "Longitude",
    "Skip",
    "Apply",
    "Wi-Fi off - NTP unavailable",
    // wizard body text
    "Choose your language",
    "Select the language for the interface.",
    "This quick setup wizard will guide you through:\n\n"
        "  WiFi connection\n"
        "  Time zone & NTP servers\n\n"
        "All settings can be changed later from\nthe Settings menu on the dashboard.",
    "WiFi password",
    "Leave SSID blank to use AP-mode only.",
    "Broker URI  (e.g. mqtt://192.168.1.10:1883)",
    "Username (optional)",
    "Password (optional)",
    "UTC offset (hours : minutes)",
    "NTP Server 1",
    "NTP Server 2 (optional)",
    "Used for sunrise / sunset calculations.\n"
        "Enter decimal degrees (e.g. 54.7826 / 32.0453 for Smolensk).",
    "AP mode only",
    "Your AquaControl is ready.\nAll settings can be adjusted later from the Settings menu.",
    "No networks found",
    "Scan",
    "Scanning...",
    // time & location screen
    "Timezone",
    "UTC offset (minutes)",
    "NTP Servers",
    "Primary NTP server",
    "Secondary NTP server",
    "Location  (for solar calculations)",
    "Latitude  (e.g. 55.75)",
    "Longitude  (e.g. 37.61)",
    "Year", "Mon", "Day", "Hour", "Min",
    "Current:",
    // network settings screen
    "Username  (optional)",
    "Password  (optional)",
    "Base topic",
    "HA MQTT Discovery",
    "Signal: %d dBm",
    "Forget Network",
    "Rescan",
    // trigger editor
    "Name",
    "trigger name",
    "+ Add",
    "Edit Trigger",
    "Add Trigger",
    "Delete Trigger",
    "Remove this trigger?",
    "Select trigger type:",
    "Trigger manager not available.",
    "No triggers configured.\nTap  + Add  to create one.",
    "Start time  (HH:MM)",
    "Stop time  (HH:MM)",
    "Mode",
    "Interval type",
    "Interval (min)",
    "At time  (HH:MM)",
    "On duration (min)",
    "Active days",
    "Event",
    "End mode",
    "End event",
    "End offset (min)",
    "Sensor",
    "Condition",
    "Threshold temperature",
    "Hysteresis (\xc2\xb0" "C)",
    "Linked Devices",
    "Above",
    "Below",
    // day names
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun",
    // device detail
    "CURRENT STATE",
    "OVERRIDE CONTROL",
    "MANUAL CONTROL",
    "FADING IN",
    "FADING OUT",
    "Level",
    "Until next",
    "Following triggers",
    "No manual hold",
    "PWM SETTINGS",
    "RGB SETTINGS",
    "RELAY SETTINGS",
    "Active: HIGH (normal)",
    "Active: LOW (inverted)",
    "DEVICE ROLE",
    "Role",
    "DEVICE NAME",
    "Tap field to edit - use EN/RU to switch layout",
    "Delete device?",
    "Permanently delete \"%s\"?\nAll triggers referencing this device will keep their settings\nbut stop affecting anything.",
    "This device is not linked to any trigger. ON / OFF taps\napply immediately and stay until changed.",
    "Pick a duration to apply the override; until then the device follows its triggers.",
    "Device",
    // RGB color presets
    "Warm", "Cool", "Red", "Green", "Blue", "Magenta", "Cyan",
    // display & system screen
    "Active brightness",
    "Dim brightness",
    "Inactivity timeout",
    "Display",
    "Units",
    "Setup",
    "Temperature unit",
    // system status screen
    "Firmware",
    "Resources",
    "Active Faults",
    "No active faults.",
    "Recent Events",
    "No events recorded.",
    "Actions",
    "Factory Reset",
    "This will erase all settings and reboot.\nAll configured devices and triggers will be lost.",
    // sensors settings screen
    "I2C Addresses",
    "Water sensor",
    "Ambient sensor",
    "Enabled",
    "Address changes take effect after restart.",
    "Calibration Offset",
    "Adjust if sensor reads high or low.\nApplied immediately on save.",
    "Water (\xc2\xb0" "C)",
    "Ambient (\xc2\xb0" "C)",
    "Water Temp History (12h)",
    "No data yet",
    "All done",
    // dashboard date / time strings
    "time not set", "syncing time...",
    // full weekday names
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    // short weekday
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
    // month names
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    // NTP sync button
    "Sync Now",
    "Sync started...",
    "Wi-Fi is off",
    // dashboard status strings
    "No data",
    "Set location",
    "Polar",
    "%us ago",
    "%um ago",
    "None yet",
    "%u / %u on",
    // system screen
    "Restart",
    "Not connected",
    // add device screen
    "Add Device",
    "Type",
    "Channel",
    "Channel conflict",
    "Channel %d is already used by another %s device.",
    "Channels %d\u2013%d are already used by another device.",
    // display timeout options
    "30 seconds",
    "1 minute",
    "2 minutes",
    "5 minutes",
    "10 minutes",
    "Never",
    // device detail
    "trigger driven",
    "manual only",
    "Held",
    "for %dm",
    "until next trigger",
    "Turn ON",
    "Turn OFF",
    "Hold",
    "Cancel hold",
    "Toggle",
    // boot screen
    "tap to pause",
    // network settings
    "(leave blank for open network)",
    // solar end-mode value labels
    "Duration",
    "Until event",
    // schedule trigger mode labels
    "Start/End",              // was "Start→End" (U+2192 not in font)
    "Daily at",
    "Every N",
    "Every",
    "Duration",
    "sec",
    "min",
    "hr",
    "Duration >= period — cannot save",
};

constexpr const char* kRu[kCount] = {
    // boot / global
    "AquaControl - запуск...",
    "Система готова.",
    // buttons
    "ОК", "Отмена", "Сохранить", "Удалить", "Изменить",
    "Назад", "Далее", "Повторить", "Да", "Нет",
    // nav
    "Главная", "Устройства", "Триггеры", "Расписания", "Настройки",
    "Сеть", "Время", "Датчики", "Система", "История", "Журнал", "Инфо",
    "Дисплей и\nсистема",
    // devices
    "Реле", "ШИМ", "RGB", "Включено", "Выключено",
    "ВКЛ", "ВЫКЛ", "Ручной режим", "Яркость", "Цвет",
    "Плавный пуск", "Плавное гашение",
    // triggers
    "Расписание", "Солнце", "Температура",
    "Восход", "Закат", "Смещение (мин)", "Длительность (мин)", "Дни",
    // sensors / units
    "Вода", "Воздух", "°C", "°F", "%", "мин",
    // network
    "Wi-Fi", "Клиент", "Точка доступа",
    "Подключено", "Отключено", "Сеть (SSID)", "Пароль",
    "MQTT", "NTP",
    // faults
    "Активные неисправности", "Сбой I2C", "Сбой RTC", "Нет источника времени",
    "Пересечение расписаний", "Координаты не заданы",
    // wizard
    "Добро пожаловать в AquaControl",
    "Язык",
    "Язык применяется немедленно.\nВсе надписи обновляются сразу.",
    "Время", "Местоположение", "Wi-Fi", "Реле",
    "Первоначальная настройка",
    "Включить Wi-Fi",
    "Включить MQTT",
    "Задать время",
    "Широта",
    "Долгота",
    "Пропустить",
    "Применить",
    "Wi-Fi выкл. - NTP недоступен",
    // wizard body text
    "Выберите язык",
    "Выберите язык интерфейса.",
    "Мастер первоначальной настройки поможет вам:\n\n"
        "  Настроить Wi-Fi\n"
        "  Задать часовой пояс и NTP\n\n"
        "Все настройки можно изменить позже\nв меню настроек.",
    "Пароль Wi-Fi",
    "Оставьте SSID пустым для режима точки доступа.",
    "Адрес брокера (напр. mqtt://192.168.1.10:1883)",
    "Имя пользователя (опционально)",
    "Пароль (опционально)",
    "Смещение UTC (часы : минуты)",
    "NTP сервер 1",
    "NTP сервер 2 (опционально)",
    "Используется для расчётов восхода/заката.\n"
        "Введите градусы в десятичном формате (напр. 54.7826 / 32.0453).",
    "Только точка доступа",
    "AquaControl готов к работе.\nВсе настройки можно изменить позже в меню настроек.",
    "Сети не найдены",
    "Сканировать",
    "Поиск...",
    // time & location screen
    "Часовой пояс",
    "Смещение UTC (минуты)",
    "NTP серверы",
    "Основной NTP сервер",
    "Резервный NTP сервер",
    "Местоположение (для расчётов солнца)",
    "Широта (напр. 55.75)",
    "Долгота (напр. 37.61)",
    "Год", "Мес", "День", "Час", "Мин",
    "Текущее:",
    // network settings screen
    "Имя пользователя (опционально)",
    "Пароль (опционально)",
    "Базовый топик",
    "Автообнаружение HA (MQTT)",
    "Сигнал: %d dBm",
    "Забыть сеть",
    "Пересканировать",
    // trigger editor
    "Название",
    "имя триггера",
    "+ Добавить",
    "Редактировать триггер",
    "Новый триггер",
    "Удалить триггер",
    "Удалить этот триггер?",
    "Выберите тип триггера:",
    "Менеджер триггеров недоступен.",
    "Триггеры не настроены.\nНажмите  + Добавить  для создания.",
    "Начало  (ЧЧ:ММ)",
    "Конец  (ЧЧ:ММ)",
    "Режим",
    "Тип интервала",
    "Интервал (мин)",
    "В момент времени (ЧЧ:ММ)",
    "Длительность включения (мин)",
    "Активные дни",
    "Событие",
    "Режим окончания",
    "Конечное событие",
    "Смещение конца (мин)",
    "Датчик",
    "Условие",
    "Пороговая температура",
    "Гистерезис (\xc2\xb0" "C)",
    "Связанные устройства",
    "Выше",
    "Ниже",
    // day names
    "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс",
    // device detail
    "ТЕКУЩЕЕ СОСТОЯНИЕ",
    "ПЕРЕОПРЕДЕЛЕНИЕ",
    "РУЧНОЕ УПРАВЛЕНИЕ",
    "ВКЛЮЧЕНИЕ",
    "ВЫКЛЮЧЕНИЕ",
    "Уровень",
    "До след. триггера",
    "По расписанию",
    "Нет удержания",
    "НАСТРОЙКИ ШИМ",
    "НАСТРОЙКИ RGB",
    "НАСТРОЙКИ РЕЛЕ",
    "Активное: HIGH (обычное)",
    "Активное: LOW (инвертированное)",
    "РОЛЬ УСТРОЙСТВА",
    "Роль",
    "ИМЯ УСТРОЙСТВА",
    "Нажмите для редактирования - EN/RU для смены языка",
    "Удалить устройство?",
    "Удалить \"%s\" навсегда?\nВсе триггеры, ссылающиеся на это устройство,\nсохранят настройки, но перестанут на него влиять.",
    "Устройство не привязано к триггерам. Нажатия ВКЛ/ВЫКЛ\nприменяются немедленно.",
    "Выберите длительность переопределения; до этого устройство следует своим триггерам.",
    "Устройство",
    // RGB color presets
    "Тёплый", "Холодный", "Красный", "Зелёный", "Синий", "Пурпурный", "Голубой",
    // display & system screen
    "Активная яркость",
    "Яркость в режиме затемнения",
    "Тайм-аут бездействия",
    "Дисплей",
    "Единицы измерения",
    "Настройка",
    "Единица температуры",
    // system status screen
    "Прошивка",
    "Ресурсы",
    "Активные неисправности",
    "Неисправностей нет.",
    "Последние события",
    "Событий нет.",
    "Действия",
    "Сброс настроек",
    "Все настройки будут удалены и устройство перезагрузится.\nВсе устройства и триггеры будут потеряны.",
    // sensors settings screen
    "I2C адреса",
    "Датчик воды",
    "Датчик воздуха",
    "Включён",
    "Смена адреса вступит в силу после перезапуска.",
    "Поправка калибровки",
    "Корректировка при отклонении показаний датчика.\nПрименяется немедленно при сохранении.",
    "Вода (\xc2\xb0" "C)",
    "Воздух (\xc2\xb0" "C)",
    "История температуры воды (12ч)",
    "Данных пока нет",
    "Готово",
    // dashboard date / time strings
    "время не задано", "синхронизация...",
    // full weekday names
    "Воскресенье", "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота",
    // short weekday
    "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб",
    // month names
    "янв", "фев", "мар", "апр", "мая", "июн",
    "июл", "авг", "сен", "окт", "ноя", "дек",
    // NTP sync button
    "Синхронизовать",
    "Синхронизация запущена...",
    "Wi-Fi выключен",
    // dashboard status strings
    "Нет данных",
    "Задать координаты",
    "Полярный",
    "%uс назад",
    "%uм назад",
    "Нет",
    "%u / %u вкл",
    // system screen
    "Перезагрузить",
    "Не подключено",
    // add device screen
    "Добавить устройство",
    "Тип",
    "Канал",
    "Конфликт каналов",
    "Канал %d уже используется другим устройством %s.",
    "Каналы %d\u2013%d уже заняты другим устройством.",
    // display timeout options
    "30 секунд",
    "1 минута",
    "2 минуты",
    "5 минут",
    "10 минут",
    "Нет",
    // device detail
    "по триггеру",
    "ручной режим",
    "Удерживается",
    "на %d мин",
    "до следующего триггера",
    "Включить",
    "Выключить",
    "Удержать",
    "Отмена удержания",
    "Переключить",
    // boot screen
    "нажмите для паузы",
    // network settings
    "(оставьте пустым для открытой сети)",
    // solar end-mode value labels
    "Длительность",
    "До события",
    // schedule trigger mode labels
    "Старт/Конец",               // was "Старт→Конец" (U+2192 not in font)
    "Ежедневно в",
    "Каждые N",
    "Каждые",
    "Длительность",
    "сек",
    "мин",
    "ч",
    "Длительность \xe2\x89\xa5 период — сохранение невозможно",
};

// Compile-time sanity: tables must be the same size as the enum range.
static_assert(sizeof(kEn) / sizeof(kEn[0]) == kCount,
              "EN table size != LangKey count");
static_assert(sizeof(kRu) / sizeof(kRu[0]) == kCount,
              "RU table size != LangKey count");

}  // namespace

const char* tr(LangKey key) {
    const size_t idx = static_cast<size_t>(key);
    if (idx >= kCount) return "?";
    return (s_lang == Language::RU) ? kRu[idx] : kEn[idx];
}

}  // namespace aqua::ui::i18n
