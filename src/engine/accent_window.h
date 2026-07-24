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

// ASCII-only uppercase check; sufficient because input keys are physical
// keyboard keys which are always single ASCII bytes (legacy invariant).
inline bool isUppercaseInput(const std::string &input) {
    return input.length() == 1 && input[0] >= 'A' && input[0] <= 'Z';
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
