// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_ENGINE_KEYSYMS_H
#define SCHNELLE_ZEICHEN_ENGINE_KEYSYMS_H

// The few X11/xkbcommon keysym values the engine reasons about (leaders and
// modifier classification). Values are the universal X11 keysym encoding
// that every backend (xkbcommon, XKB) shares, named here once so the
// engine never hardcodes magic numbers. Everything else reaches the engine
// as resolved text, never as a keysym.

#include <cstdint>

namespace schnelle_zeichen {

inline constexpr uint32_t kKeysymSpace = 0x0020;
inline constexpr uint32_t kKeysymLeft = 0xff51;
inline constexpr uint32_t kKeysymUp = 0xff52;
inline constexpr uint32_t kKeysymRight = 0xff53;
inline constexpr uint32_t kKeysymDown = 0xff54;
inline constexpr uint32_t kKeysymAltL = 0xffe9;
inline constexpr uint32_t kKeysymAltR = 0xffea;
inline constexpr uint32_t kKeysymIsoLevel3Shift = 0xfe03;
// Modifier keysym range (Shift_L .. Hyper_R), the legacy pure-modifier gate.
inline constexpr uint32_t kKeysymModifierFirst = 0xffe1; // Shift_L
inline constexpr uint32_t kKeysymModifierLast = 0xffee;  // Hyper_R

inline bool isAltSym(uint32_t sym) { return sym == kKeysymAltL; }
inline bool isAltGrSym(uint32_t sym) {
    return sym == kKeysymAltR || sym == kKeysymIsoLevel3Shift;
}
inline bool isAltLeaderSym(uint32_t sym) {
    return isAltSym(sym) || isAltGrSym(sym);
}
inline bool isModifierSym(uint32_t sym) {
    return (sym >= kKeysymModifierFirst && sym <= kKeysymModifierLast) ||
           sym == kKeysymIsoLevel3Shift;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_ENGINE_KEYSYMS_H
