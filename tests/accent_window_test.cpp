// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Accent-window boundary math: the pure window logic factored out of the legacy
// getEffectiveDelay/getEffectiveMinHold/isTimeoutExpired/isBeforeMinHold.
// Focus: the case selection (lowercase vs uppercase bounds, including
// multi-byte uppercase via xkbcommon), the min >= max degenerate guard,
// unlimited mode, and the exact inclusive/exclusive edges of the min-hold and
// expiry checks.

#include "accent_window.h"

#include <cstdio>
#include <string>

using namespace schnelle_zeichen;

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

namespace {

// Milliseconds -> the microsecond clock the checks take.
uint64_t ms(uint64_t v) { return v * kUsecPerMs; }

DelayConfig makeDelay(int lower, int upper, int lowerMin, int upperMin,
                      bool unlimited) {
    DelayConfig d;
    d.lowercase = lower;
    d.uppercase = upper;
    d.lowercaseMin = lowerMin;
    d.uppercaseMin = upperMin;
    d.unlimited = unlimited;
    return d;
}

// ---------------------------------------------------------- isUppercaseInput

void testIsUppercaseInput() {
    // ASCII case.
    CHECK(!isUppercaseInput("a"));
    CHECK(isUppercaseInput("A"));
    CHECK(!isUppercaseInput("1"));
    CHECK(!isUppercaseInput("="));
    // Multi-byte: parseMappings allows non-ASCII inputs, so case must be
    // decided through xkbcommon, not stop at ASCII.
    CHECK(isUppercaseInput("\xC3\x84"));  // Ä
    CHECK(!isUppercaseInput("\xC3\xA4")); // ä
    CHECK(isUppercaseInput("\xC3\x96"));  // Ö
    CHECK(!isUppercaseInput("\xC3\x9f")); // ß has no distinct lower form
    // Not a single valid character: empty, multi-char, invalid UTF-8.
    CHECK(!isUppercaseInput(""));
    CHECK(!isUppercaseInput("AB"));
    CHECK(!isUppercaseInput("a\xC3\x84")); // trailing bytes past one char
    CHECK(!isUppercaseInput("\xC3"));      // truncated sequence
}

// ---------------------------------------------------------- effectiveWindow

void testEffectiveWindowCaseSelection() {
    const DelayConfig d = makeDelay(400, 700, 0, 0, false);
    const AccentWindow lo = effectiveWindow(d, "a");
    CHECK(lo.maxMs == 400 && lo.minMs == 0 && !lo.unlimited);
    const AccentWindow up = effectiveWindow(d, "A");
    CHECK(up.maxMs == 700 && up.minMs == 0);
    // Multi-byte uppercase takes the uppercase bound too.
    CHECK(effectiveWindow(d, "\xC3\x84").maxMs == 700);
    CHECK(effectiveWindow(d, "\xC3\xA4").maxMs == 400);
}

void testEffectiveWindowMinHoldCarried() {
    const DelayConfig d = makeDelay(400, 700, 150, 250, false);
    CHECK(effectiveWindow(d, "a").minMs == 150);
    CHECK(effectiveWindow(d, "A").minMs == 250);
}

// The min >= max degenerate guard drops the lower bound while the upper bound
// is a real window (otherwise the accent would be unreachable).
void testEffectiveWindowDegenerateMinIgnored() {
    CHECK(effectiveWindow(makeDelay(400, 700, 400, 0, false), "a").minMs == 0);
    CHECK(effectiveWindow(makeDelay(400, 700, 500, 0, false), "a").minMs == 0);
    // Just below max: the lower bound stands.
    CHECK(effectiveWindow(makeDelay(400, 700, 399, 0, false), "a").minMs ==
          399);
}

// Unlimited mode has no max to collide with, so the full dead zone stands even
// above the stored max.
void testEffectiveWindowUnlimitedKeepsMin() {
    const AccentWindow w =
        effectiveWindow(makeDelay(400, 700, 800, 0, true), "a");
    CHECK(w.unlimited);
    CHECK(w.minMs == 800); // not clamped by the (now irrelevant) max
}

// ----------------------------------------------------------- isBeforeMinHold

void testIsBeforeMinHold() {
    AccentWindow w;
    w.minMs = 150;
    CHECK(isBeforeMinHold(w, ms(0)));
    CHECK(isBeforeMinHold(w, ms(149)));
    CHECK(!isBeforeMinHold(w, ms(150))); // edge is inclusive of the window
    CHECK(!isBeforeMinHold(w, ms(200)));
    // A disabled lower bound never blocks.
    w.minMs = 0;
    CHECK(!isBeforeMinHold(w, ms(0)));
    w.minMs = -5;
    CHECK(!isBeforeMinHold(w, ms(0)));
}

// ----------------------------------------------------------- isWindowExpired

void testIsWindowExpired() {
    AccentWindow w;
    w.maxMs = 400;
    w.unlimited = false;
    CHECK(!isWindowExpired(w, ms(0)));
    CHECK(!isWindowExpired(w, ms(400))); // edge still inside the window
    CHECK(isWindowExpired(w, ms(401)));
    // Unlimited never expires, however long the hold.
    w.unlimited = true;
    CHECK(!isWindowExpired(w, ms(10'000)));
}

} // namespace

int main() {
    testIsUppercaseInput();
    testEffectiveWindowCaseSelection();
    testEffectiveWindowMinHoldCarried();
    testEffectiveWindowDegenerateMinIgnored();
    testEffectiveWindowUnlimitedKeepsMin();
    testIsBeforeMinHold();
    testIsWindowExpired();
    if (failures == 0) {
        std::printf("accent_window_test: all checks passed\n");
        return 0;
    }
    std::printf("accent_window_test: %d check(s) failed\n", failures);
    return 1;
}
