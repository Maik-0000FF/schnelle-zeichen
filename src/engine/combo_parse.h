// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_ENGINE_COMBO_PARSE_H
#define SCHNELLE_ZEICHEN_ENGINE_COMBO_PARSE_H

// Parse the portable shortcut combo strings from profiles.conf (e.g.
// "Control+Alt+Super+J") into a modifier mask plus keysym, replacing the
// legacy fcitx::Key plumbing. Keysym names resolve through libxkbcommon
// (the same spellings fcitx parses); letters match case-insensitively via
// xkb_keysym_to_lower on both sides, mirroring Key::normalize. A combo
// without a real (non-Shift) modifier is invalid, so a bare "1" can never
// swallow every plain press of it.

#include "KeySource.h" // KeyModifier mask

#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <string>

namespace schnelle_zeichen {

struct ShortcutCombo {
    uint32_t modifiers = 0; // KeyModifier mask
    uint32_t keysym = XKB_KEY_NoSymbol;

    bool valid() const { return keysym != XKB_KEY_NoSymbol; }

    // Shift is compared too: a "Control+Shift+1" combo must not fire on a
    // plain Ctrl+1. CapsLock is ignored on the event side (a lock, not a
    // held modifier).
    bool matches(uint32_t eventModifiers, uint32_t eventKeysym) const {
        if (!valid()) {
            return false;
        }
        const uint32_t relevant = KeyModifier::Shift | KeyModifier::Ctrl |
                                  KeyModifier::Alt | KeyModifier::AltGr |
                                  KeyModifier::Super;
        if ((eventModifiers & relevant) != (modifiers & relevant)) {
            return false;
        }
        return xkb_keysym_to_lower(eventKeysym) == xkb_keysym_to_lower(keysym);
    }
};

inline ShortcutCombo parseShortcutCombo(const std::string &combo) {
    ShortcutCombo result;
    if (combo.empty()) {
        return result;
    }
    uint32_t mods = 0;
    size_t pos = 0;
    std::string keyName;
    while (pos <= combo.size()) {
        const size_t plus = combo.find('+', pos);
        const std::string token = combo.substr(
            pos, plus == std::string::npos ? std::string::npos : plus - pos);
        const bool last = plus == std::string::npos;
        if (last) {
            keyName = token;
        } else if (token == "Control" || token == "Ctrl") {
            mods = mods | static_cast<uint32_t>(KeyModifier::Ctrl);
        } else if (token == "Alt") {
            mods = mods | static_cast<uint32_t>(KeyModifier::Alt);
        } else if (token == "Shift") {
            mods = mods | static_cast<uint32_t>(KeyModifier::Shift);
        } else if (token == "Super") {
            mods = mods | static_cast<uint32_t>(KeyModifier::Super);
        } else {
            return result; // unknown modifier name: invalid combo
        }
        if (last) {
            break;
        }
        pos = plus + 1;
    }
    // The modifier requirement: at least one real (non-Shift) modifier.
    const uint32_t nonShift = KeyModifier::Ctrl | KeyModifier::Alt |
                              KeyModifier::AltGr | KeyModifier::Super;
    if ((mods & nonShift) == 0 || keyName.empty()) {
        return result;
    }
    const xkb_keysym_t sym =
        xkb_keysym_from_name(keyName.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
    if (sym == XKB_KEY_NoSymbol) {
        return result;
    }
    result.modifiers = mods;
    result.keysym = sym;
    return result;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_ENGINE_COMBO_PARSE_H
