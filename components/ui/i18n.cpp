#include "i18n.h"

namespace aqua::ui::i18n {

static Language s_lang = Language::EN;

void set_language(Language lang) { s_lang = lang; }
Language get_language() { return s_lang; }

// Phase 1 stub — EN only. Phase 4 replaces with full table.
static const char* tr_en(LangKey key) {
    switch (key) {
        case LangKey::BOOT_TITLE: return "AquaControl v" "0.1.0" " - booting...";
        case LangKey::BOOT_READY: return "System ready.";
    }
    return "?";
}

static const char* tr_ru(LangKey key) {
    switch (key) {
        case LangKey::BOOT_TITLE: return "AquaControl v" "0.1.0" " - \xD0\xB7\xD0\xB0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA...";
        case LangKey::BOOT_READY: return "\xD0\xA1\xD0\xB8\xD1\x81\xD1\x82\xD0\xB5\xD0\xBC\xD0\xB0 \xD0\xB3\xD0\xBE\xD1\x82\xD0\xBE\xD0\xB2\xD0\xB0.";
    }
    return "?";
}

const char* tr(LangKey key) {
    return s_lang == Language::RU ? tr_ru(key) : tr_en(key);
}

}  // namespace aqua::ui::i18n
