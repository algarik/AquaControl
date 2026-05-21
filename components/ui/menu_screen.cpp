// AquaControl - Settings root menu.
//
// Grid of large tappable category cards. Tapping each card pushes the
// corresponding sub-screen. Categories that aren't implemented yet route
// to placeholder_screen so the user can still feel the navigation.
#include "menu_screen.h"

#include "chrome.h"
#include "device_list_screen.h"
#include "display_system_screen.h"
#include "i18n.h"
#include "network_settings_screen.h"
#include "placeholder_screen.h"
#include "sensors_settings_screen.h"
#include "system_config.h"
#include "triggers_screen.h"
#include "screen_manager.h"
#include "system_status_screen.h"
#include "theme.h"
#include "time_location_screen.h"
#include "ui_context.h"

namespace aqua::ui::menu_screen {

// Forward declaration: build() is defined at the bottom of this file but
// language_builder (inside the anonymous namespace) needs to call it.
lv_obj_t* build();

namespace {

constexpr int16_t kCardW   = 184;
constexpr int16_t kCardH   = 168;
constexpr int16_t kCardGap = 14;
constexpr int16_t kCols    = 4;

// Set when language changes in the language sub-screen; consumed by
// LV_EVENT_SCREEN_LOADED on the settings grid to refresh card labels.
static bool      s_lang_changed = false;
static lv_obj_t* s_grid         = nullptr;

struct CategoryEntry {
    i18n::LangKey title_key;
    const char* icon;
    lv_obj_t* (*builder)();  // returns a fresh screen root
    const char* placeholder_msg;  // used if builder is nullptr
};

static lv_obj_t* devices_builder()  { return device_list_screen::build(); }
static lv_obj_t* triggers_builder() { return triggers_screen::build(); }
static lv_obj_t* network_builder()  {
    return network_settings_screen::build();
}
static lv_obj_t* time_builder()     {
    return time_location_screen::build();
}
static lv_obj_t* display_builder()  {
    return display_system_screen::build();
}
static lv_obj_t* sensors_builder()  {
    return sensors_settings_screen::build();
}
static lv_obj_t* system_builder()   {
    return system_status_screen::build();
}
static lv_obj_t* language_builder() {
    // Build a simple language-switcher screen inline.
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    chrome::build(root, i18n::tr(i18n::LangKey::WIZ_LANG), chrome::pop_on_back);

    lv_obj_t* cont = lv_obj_create(root);
    lv_obj_set_size(cont, LV_PCT(100), 480 - chrome::kHeaderH);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, theme::PAD_LG, 0);
    lv_obj_set_style_pad_row(cont, theme::PAD_MD, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    aqua::ui::i18n::Language cur = aqua::ui::i18n::get_language();

    struct LangBtnDesc { const char* lbl; aqua::ui::i18n::Language lang; };
    LangBtnDesc opts[] = {
        { "English (EN)", aqua::ui::i18n::Language::EN },
        { "Русский (RU)", aqua::ui::i18n::Language::RU },
    };
    for (auto& opt : opts) {
        bool active = (opt.lang == cur);
        lv_obj_t* btn = lv_btn_create(cont);
        lv_obj_set_size(btn, LV_PCT(70), 56);
        lv_obj_set_style_bg_color(btn,
            active ? theme::color_accent() : theme::color_surface_alt(), 0);
        lv_obj_set_style_border_color(btn, theme::color_accent(), 0);
        lv_obj_set_style_border_width(btn, active ? 0 : 2, 0);
        lv_obj_set_style_radius(btn, theme::RADIUS_MD, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, opt.lbl);
        lv_obj_set_style_text_font(lbl, theme::font_title(), 0);
        lv_obj_set_style_text_color(lbl,
            active ? theme::color_text_primary() : theme::color_text_secondary(), 0);
        lv_obj_center(lbl);

        // Capture language via heap context (avoid lambda capture issues).
        struct LangCtx { aqua::ui::i18n::Language lang; };
        auto* ctx = new LangCtx{opt.lang};
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* c = static_cast<LangCtx*>(lv_event_get_user_data(e));
            aqua::ui::i18n::set_language(c->lang);
            // Persist to system config.
            auto* cfg = aqua::ui::ui_context().sys_cfg;
            if (cfg) {
                cfg->language = static_cast<aqua::storage::Language>(
                    static_cast<uint8_t>(c->lang));
                aqua::storage::save_system_config(*cfg);
            }
            // Pop back to settings and let LV_EVENT_SCREEN_LOADED refresh labels.
            s_lang_changed = true;
            screen_manager::pop(screen_manager::Transition::SLIDE_RIGHT);
        }, LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            delete static_cast<LangCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, ctx);
    }

    lv_obj_t* note = lv_label_create(cont);
    lv_label_set_text(note, i18n::tr(i18n::LangKey::WIZ_LANG_NOTE));
    lv_obj_set_style_text_font(note, theme::font_caption(), 0);
    lv_obj_set_style_text_color(note, theme::color_text_disabled(), 0);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);

    return root;
}
static const CategoryEntry kCategories[] = {
    { i18n::LangKey::NAV_DEVICES,  LV_SYMBOL_LIST,     devices_builder,  nullptr },
    { i18n::LangKey::NAV_TRIGGERS, LV_SYMBOL_LOOP,     triggers_builder, nullptr },
    { i18n::LangKey::NAV_NETWORK,  LV_SYMBOL_WIFI,     network_builder,  nullptr },
    { i18n::LangKey::NAV_TIME,     LV_SYMBOL_REFRESH,  time_builder,     nullptr },
    { i18n::LangKey::NAV_DISPLAY,  LV_SYMBOL_IMAGE,    display_builder,  nullptr },
    { i18n::LangKey::NAV_SENSORS,  LV_SYMBOL_EYE_OPEN, sensors_builder,  nullptr },
    { i18n::LangKey::NAV_SYSTEM,   LV_SYMBOL_SETTINGS, system_builder,   nullptr },
    { i18n::LangKey::WIZ_LANG,     LV_SYMBOL_KEYBOARD, language_builder, nullptr },
};

static void on_category_clicked(lv_event_t* e) {
    auto* entry = static_cast<const CategoryEntry*>(
        lv_event_get_user_data(e));
    if (!entry || !entry->builder) return;
    if (lv_obj_t* s = entry->builder()) {
        screen_manager::push(s, screen_manager::Transition::SLIDE_LEFT);
    }
}

static lv_obj_t* make_category_card(lv_obj_t* parent,
                                    const CategoryEntry* entry) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_size(c, kCardW, kCardH);
    lv_obj_set_style_bg_color(c, theme::color_surface(), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, theme::color_outline(), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_opa(c, LV_OPA_70, 0);
    lv_obj_set_style_radius(c, theme::RADIUS_LG, 0);
    lv_obj_set_style_pad_all(c, theme::PAD_MD, 0);
    lv_obj_set_style_shadow_width(c, 18, 0);
    lv_obj_set_style_shadow_ofs_y(c, 4, 0);
    lv_obj_set_style_shadow_color(c, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(c, LV_OPA_30, 0);
    lv_obj_set_style_bg_color(c, theme::color_surface_alt(),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_color(c, theme::color_accent(),
                                  LV_STATE_PRESSED);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* icon = lv_label_create(c);
    lv_label_set_text(icon, entry->icon);
    lv_obj_set_style_text_font(icon, theme::font_display(), 0);
    lv_obj_set_style_text_color(icon, theme::color_accent(), 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, theme::PAD_XS);

    lv_obj_t* title = lv_label_create(c);
    lv_label_set_text(title, i18n::tr(entry->title_key));
    lv_obj_set_style_text_font(title, theme::font_title(), 0);
    lv_obj_set_style_text_color(title, theme::color_text_primary(), 0);
    lv_obj_set_width(title, kCardW - 2 * theme::PAD_MD);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_align(title, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_add_event_cb(c, on_category_clicked, LV_EVENT_CLICKED,
                        (void*)entry);
    return c;
}

}  // namespace

lv_obj_t* build() {
    lv_obj_t* root = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root, theme::color_background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    chrome::build(root, i18n::tr(i18n::LangKey::NAV_SETTINGS), chrome::pop_on_back);

    lv_obj_t* grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 800, 480 - chrome::kHeaderH);
    lv_obj_set_pos(grid, 0, chrome::kHeaderH);
    lv_obj_set_style_pad_all(grid, theme::PAD_LG, 0);
    lv_obj_set_style_pad_row(grid, kCardGap, 0);
    lv_obj_set_style_pad_column(grid, kCardGap, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    const size_t n = sizeof(kCategories) / sizeof(kCategories[0]);
    const int16_t grid_w = kCols * kCardW + (kCols - 1) * kCardGap;
    const int16_t x_off = (800 - grid_w) / 2 - theme::PAD_LG;
    for (size_t i = 0; i < n; ++i) {
        lv_obj_t* card = make_category_card(grid, &kCategories[i]);
        int col = (int)(i % kCols);
        int row = (int)(i / kCols);
        lv_obj_set_pos(card,
                       x_off + col * (kCardW + kCardGap),
                       row * (kCardH + kCardGap));
    }

    s_grid = grid;
    // Clear pointer when the screen is deleted (e.g. replaced by dashboard).
    lv_obj_add_event_cb(root, [](lv_event_t*) { s_grid = nullptr; },
                        LV_EVENT_DELETE, nullptr);
    // On returning to settings after a language change, refresh card labels.
    lv_obj_add_event_cb(root, [](lv_event_t*) {
        if (!s_lang_changed || !s_grid) return;
        s_lang_changed = false;
        const size_t nc = sizeof(kCategories) / sizeof(kCategories[0]);
        for (size_t i = 0; i < nc; ++i) {
            lv_obj_t* card = lv_obj_get_child(s_grid, (int32_t)i);
            if (!card) continue;
            uint32_t child_n = lv_obj_get_child_count(card);
            if (child_n >= 2) {
                // Last child is the title label.
                lv_obj_t* title = lv_obj_get_child(card, (int32_t)(child_n - 1));
                lv_label_set_text(title, i18n::tr(kCategories[i].title_key));
            }
        }
    }, LV_EVENT_SCREEN_LOADED, nullptr);

    return root;
}

}  // namespace aqua::ui::menu_screen
