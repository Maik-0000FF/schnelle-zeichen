// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Progress-overlay grid geometry: the pure placement math the daemon uses to
// centre the PANEL (not the wider/taller panel+bar surface) on a grid cell.
// Focus: the column-centre formula, the horizontal panel margin, and the
// vertical panel margin that fixes the Center-row offset (the bar overhang
// above the panel would otherwise drop it by half the overhang), plus the
// edge-margin clamps on both axes.

#include "core/progress_overlay_geometry.h"

#include <cstdio>

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

constexpr int kEdge = 24; // matches kEdgeMargin in overlay/main.cpp

// ---------------------------------------------------------- gridColumnCenter

// The 7 columns sit at uniform 12.5 % steps: column c centres at (c+1)/8.
void testGridColumnCenter() {
    const int sw = 1920;
    CHECK(progress::gridColumnCenter(0, sw) == sw * 1 / 8); // 240
    CHECK(progress::gridColumnCenter(3, sw) == sw / 2); // 960, screen centre
    CHECK(progress::gridColumnCenter(6, sw) == sw * 7 / 8); // 1680
}

// --------------------------------------------------------- gridPanelLeftMargin

void testGridPanelLeftMargin() {
    // Centre column on a 1920 output: panel centred on 960 -> 960 - 100 = 860.
    CHECK(progress::gridPanelLeftMargin(3, 1920, 200, 260, kEdge) == 860);
    // A non-extreme column is the plain centred value (no clamp): col 0 centres
    // at 240 -> 240 - 100 = 140.
    CHECK(progress::gridPanelLeftMargin(0, 1920, 200, 260, kEdge) == 140);
    // Low clamp: a wide panel on a narrow output would want a negative left,
    // floored to the edge margin. col 0 centres at 50 -> 50 - 100 = -50.
    CHECK(progress::gridPanelLeftMargin(0, 400, 200, 260, kEdge) == kEdge);
    // High clamp: a right column with a wide bar overhang would push the
    // surface off the right edge; capped so it stays on by the edge margin.
    // col 6 centres at 875 -> left 825, but maxLeft = 1000 - 400 - 24 = 576.
    CHECK(progress::gridPanelLeftMargin(6, 1000, 100, 400, kEdge) == 576);
}

// ---------------------------------------------------------- gridPanelTopMargin

// The core of the N19 fix: centre the panel, not the surface. With a 14 px
// overhang (bar 6 + gap 8) above a 64 px panel, a surface-centred placement
// would drop the panel by 7 px; the panel-centred top margin cancels exactly
// that.
void testGridPanelTopMarginCentresPanel() {
    const int sh = 1080, frameH = 64;
    const int overhang = 14; // progressBarHeight(6) + progressBarGap(8)
    const int windowH = frameH + overhang; // 78
    const int top = progress::gridPanelTopMargin(sh, frameH, windowH, kEdge);
    // Panel top sits `overhang` below the surface top; its centre must land on
    // the screen centre.
    const int panelCentre = top + overhang + frameH / 2;
    CHECK(panelCentre == sh / 2);
    // Concretely: (1080-64)/2 - 14 = 494.
    CHECK(top == 494);
}

// With no bar (windowH == frameH) the margin is the plain centred panel top,
// so the non-progress path is unchanged.
void testGridPanelTopMarginNoOverhang() {
    const int sh = 1080, frameH = 64;
    CHECK(progress::gridPanelTopMargin(sh, frameH, frameH, kEdge) ==
          (sh - frameH) / 2);
}

// Low clamp: a surface taller than the output would want a negative top, which
// is floored to the edge margin. overhang 136 on a 100 px output:
// (100 - 64) / 2 - 136 < 0 -> edge. (The symmetric max-top clamp mirrors
// gridPanelLeftMargin but never binds here: a larger surface means a larger
// overhang, which only lowers the top, so it can't push the bottom off-screen.)
void testGridPanelTopMarginClamps() {
    CHECK(progress::gridPanelTopMargin(100, 64, 200, kEdge) == kEdge);
}

} // namespace

int main() {
    testGridColumnCenter();
    testGridPanelLeftMargin();
    testGridPanelTopMarginCentresPanel();
    testGridPanelTopMarginNoOverhang();
    testGridPanelTopMarginClamps();
    if (failures == 0) {
        std::printf("progress_geometry_test: all checks passed\n");
        return 0;
    }
    std::printf("progress_geometry_test: %d check(s) failed\n", failures);
    return 1;
}
