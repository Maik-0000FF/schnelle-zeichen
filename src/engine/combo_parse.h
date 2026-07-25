// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_ENGINE_COMBO_PARSE_H
#define SCHNELLE_ZEICHEN_ENGINE_COMBO_PARSE_H

// Parse the portable shortcut combo strings from profiles.conf (e.g.
// "Control+Alt+Super+J") into a modifier mask plus keysym, replacing the
// legacy framework Key plumbing. Modifier tokens match case-insensitively
// ("ctrl+j" from a hand-edited file must not silently arm nothing), and
// keysym names resolve through libxkbcommon (the classic X keysym
// spellings); letters match case-insensitively via xkb_keysym_to_lower on
// both sides, mirroring the legacy normalize. A combo without a real
// (non-Shift) modifier is invalid, so a bare "1" can never swallow every
// plain press of it.

#include "KeySource.h" // KeyModifier mask

#include <xkbcommon/xkbcommon.h>

#include <cctype>
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

namespace combo_detail {
// ASCII lowercase for the case-insensitive modifier-token match.
inline std::string lowered(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}
} // namespace combo_detail

inline ShortcutCombo parseShortcutCombo(const std::string &combo) {
    ShortcutCombo result;
    if (combo.empty()) {
        return result;
    }
    uint32_t mods = 0;
    size_t pos = 0;
    std::string keyName;
    while (pos <= combo.size()) {
        const std::size_t plus = combo.find('+', pos);
        const std::string token = combo.substr(
            pos, plus == std::string::npos ? std::string::npos : plus - pos);
        const bool last = plus == std::string::npos;
        const std::string lower = combo_detail::lowered(token);
        if (last) {
            keyName = token;
        } else if (lower == "control" || lower == "ctrl") {
            mods = mods | static_cast<uint32_t>(KeyModifier::Ctrl);
        } else if (lower == "alt") {
            mods = mods | static_cast<uint32_t>(KeyModifier::Alt);
        } else if (lower == "altgr") {
            // Completes the mask: AltGr participates in matches(), so it
            // must be spellable too instead of being an unreachable bit.
            mods = mods | static_cast<uint32_t>(KeyModifier::AltGr);
        } else if (lower == "shift") {
            mods = mods | static_cast<uint32_t>(KeyModifier::Shift);
        } else if (lower == "super") {
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
