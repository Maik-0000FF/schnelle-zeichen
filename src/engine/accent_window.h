// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_ENGINE_ACCENT_WINDOW_H
#define SCHNELLE_ZEICHEN_ENGINE_ACCENT_WINDOW_H

// Pure accent-window math, factored out of the legacy engine's
// getEffectiveDelay/getEffectiveMinHold/isTimeoutExpired/isBeforeMinHold.
// The window [min, max] in ms: a leader before min yields the plain
// character (rollover dead zone), after max it no longer triggers.
// Uppercase inputs get their own bounds. A degenerate hand-edited config
// with min >= max would make the accent unreachable, so such a lower bound
// is ignored (falls back to [0, max]).

#include "engine_config.h"
#include "mappings_io.h" // utf8DecodeFirst (shared byte handling)

#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <string>

namespace schnelle_zeichen {

inline constexpr uint64_t kUsecPerMs = 1'000;

struct AccentWindow {
    int minMs = 0;
    int maxMs = 0;
    // No upper bound: the window stays open while the key is held (the
    // macOS/Quick-Accent popup feel, [Delay]/Unlimited). The min-hold dead
    // zone still applies.
    bool unlimited = false;
};

// Uppercase check for the single UTF-8 input character. parseMappings
// deliberately allows multi-byte inputs (é, Ä on native layouts), so this
// must not stop at ASCII: a mapping on Ä would get the short lowercase
// window otherwise. Non-ASCII case is decided through xkbcommon (a keysym
// with a distinct lowercase form is uppercase), the same tables the
// resolver uses.
inline bool isUppercaseInput(const std::string &input) {
    uint32_t cp = 0;
    const size_t n = utf8DecodeFirst(input.data(), input.size(), cp);
    if (n == 0 || n != input.size()) {
        return false; // not a single valid character
    }
    if (cp < 0x80) {
        return cp >= 'A' && cp <= 'Z';
    }
    const xkb_keysym_t sym = xkb_utf32_to_keysym(cp);
    if (sym == XKB_KEY_NoSymbol) {
        return false;
    }
    return xkb_keysym_to_lower(sym) != sym;
}

inline AccentWindow effectiveWindow(const DelayConfig &delay,
                                    const std::string &input) {
    const bool upper = isUppercaseInput(input);
    AccentWindow w;
    w.maxMs = upper ? delay.uppercase : delay.lowercase;
    const int minHold = upper ? delay.uppercaseMin : delay.lowercaseMin;
    w.unlimited = delay.unlimited;
    // The min >= max degenerate guard only applies while the upper bound is
    // real: in unlimited mode there is no max to collide with, so the full
    // configured dead zone stands.
    w.minMs = (!w.unlimited && minHold >= w.maxMs) ? 0 : minHold;
    return w;
}

// True while the hold is still inside the dead zone (leader too early).
// min <= 0 disables the lower bound, the historic default.
inline bool isBeforeMinHold(const AccentWindow &w, uint64_t elapsedUsec) {
    if (w.minMs <= 0) {
        return false;
    }
    return elapsedUsec / kUsecPerMs < static_cast<uint64_t>(w.minMs);
}

// True once the hold has outlived the window (leader no longer triggers).
// Never expires in unlimited mode.
inline bool isWindowExpired(const AccentWindow &w, uint64_t elapsedUsec) {
    if (w.unlimited) {
        return false;
    }
    return elapsedUsec / kUsecPerMs > static_cast<uint64_t>(w.maxMs);
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_ENGINE_ACCENT_WINDOW_H
