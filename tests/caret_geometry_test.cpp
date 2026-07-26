// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Caret-overlay geometry: the pure placement math and wire format for the
// "show at the text caret" mode. Focus: the "Caret:x,y,w,h;grid" round-trip and
// its malformed-input fallbacks, and caretMargins (below the caret, flip above
// near the bottom, unusable-height fallback, and the on-screen clamps).

#include "check.h"
#include "core/caret_overlay_geometry.h"

#include <string>

using namespace schnelle_zeichen;

namespace {

// ------------------------------------------------------------- wire format

void testWireRoundtrip() {
    const CaretRect r{100, 200, 8, 20};
    const std::string s = caretPositionString(r, "TopCol4");
    CHECK(s == "Caret:100,200,8,20;TopCol4");
    const CaretPositionSpec spec = parseCaretPosition(s);
    CHECK(spec.atCaret);
    CHECK(spec.rect == r);
    CHECK(spec.grid == "TopCol4");
}

void testWireNegativeAndZero() {
    // Negative coordinates (a caret near a left-of-origin output) and the
    // unusable extents AT-SPI reports round-trip verbatim.
    const CaretRect r{-45, 118, -160, -1};
    const CaretPositionSpec spec =
        parseCaretPosition(caretPositionString(r, "BottomCol1"));
    CHECK(spec.atCaret && spec.rect == r && spec.grid == "BottomCol1");
}

void testParseNonCaretPassthrough() {
    // A plain grid or cursor string is not a caret placement; it passes through
    // unchanged so the caller's existing cursor/grid parser handles it.
    for (const std::string in : {"TopCol4", "Cursor:BottomCol4", ""}) {
        const CaretPositionSpec spec = parseCaretPosition(in);
        CHECK(!spec.atCaret);
        CHECK(spec.grid == in);
    }
}

void testParseMalformedFallsBack() {
    // Too few / too many fields, a non-numeric field, or a missing ';' all
    // invalidate the caret rect; the whole string passes through as the grid.
    for (const std::string bad :
         {"Caret:1,2,3;TopCol4", "Caret:1,2,3,4,5;TopCol4",
          "Caret:1,2,x,4;TopCol4", "Caret:1,2,3,4", "Caret:;TopCol4"}) {
        const CaretPositionSpec spec = parseCaretPosition(bad);
        CHECK(!spec.atCaret);
        CHECK(spec.grid == bad);
    }
}

// ------------------------------------------------------------ caretMargins

// A 1920x1080 output at the origin, a 200x64 overlay.
CaretMargins place(const CaretRect &c) {
    return caretMargins(c, 0, 0, 1920, 1080, 200, 64);
}

void testBelowTheCaret() {
    // Caret top-left (100,200), height 20: overlay sits gap below the line.
    const CaretMargins m = place({100, 200, 8, 20});
    CHECK(m.left == 100);
    CHECK(m.top == 200 + 20 + kCaretGap); // 224
}

void testFlipAboveNearBottom() {
    // Near the bottom edge, below would spill off-screen, so flip above:
    // top = y - overlayH - gap.
    const CaretMargins m = place({100, 1050, 8, 20});
    CHECK(m.left == 100);
    CHECK(m.top == 1050 - 64 - kCaretGap); // 982
}

void testUnusableHeightUsesFallback() {
    // AT-SPI's garbage height (<= 0) falls back to the fixed line height.
    const CaretMargins m = place({100, 200, -160, -1});
    CHECK(m.top == 200 + kCaretFallbackLineHeight + kCaretGap); // 228
}

void testClampsOntoOutput() {
    // Caret past the right edge: left clamped so the whole overlay stays on.
    const CaretMargins mr = place({1900, 200, 8, 20});
    CHECK(mr.left == 1920 - 200); // 1720
    // On an output shorter than the overlay, the flip-above goes negative and
    // is floored at the top edge (a degenerate case the clamp guards).
    const CaretMargins mt =
        caretMargins({100, 10, 8, 20}, 0, 0, 800, 50, 200, 64);
    CHECK(mt.top == 0);
}

void testSecondOutputLocalCoords() {
    // Caret on a second output at x-offset 1920: the margin is output-local.
    const CaretMargins m =
        caretMargins({2000, 300, 8, 20}, 1920, 0, 1920, 1080, 200, 64);
    CHECK(m.left == 80); // 2000 - 1920
    CHECK(m.top == 300 + 20 + kCaretGap);
}

} // namespace

int main() {
    testWireRoundtrip();
    testWireNegativeAndZero();
    testParseNonCaretPassthrough();
    testParseMalformedFallsBack();
    testBelowTheCaret();
    testFlipAboveNearBottom();
    testUnusableHeightUsesFallback();
    testClampsOntoOutput();
    testSecondOutputLocalCoords();
    if (failures == 0) {
        std::printf("caret_geometry_test: all checks passed\n");
        return 0;
    }
    std::printf("caret_geometry_test: %d check(s) failed\n", failures);
    return 1;
}
