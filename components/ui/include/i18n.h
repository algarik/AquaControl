// AquaControl — Localization stub (Phase 1)
// Phase 4 will replace this with a full EN/RU translation table.
// Always call tr() — never use raw string literals in UI code.
#pragma once

#include <cstdint>

namespace aqua::ui::i18n {

enum class LangKey : uint16_t {
    BOOT_TITLE,
    BOOT_READY,
    // Phase 4 will add ~200 keys here
};

enum class Language : uint8_t {
    EN = 0,
    RU = 1,
};

void set_language(Language lang);
Language get_language();

const char* tr(LangKey key);

}  // namespace aqua::ui::i18n
