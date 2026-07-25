// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_PROGRESS_OVERLAY_GEOMETRY_H
#define SCHNELLE_ZEICHEN_CORE_PROGRESS_OVERLAY_GEOMETRY_H

// Pure geometry for the overlay timing progress bar: the bar's pixel sizing
// and the grid placement that centres the PANEL (not the whole panel+bar
// surface) on a column. Free of Qt/QML, unit-testable, shared by the
// daemon's renderer and QML (via controller invokables). 1:1 port.

#include "overlay_render.h"

#include <algorithm>
#include <cmath>

namespace schnelle_zeichen {
namespace progress {

// Pixels per millisecond the bar length encodes, the screen fraction it is
// clamped to, and the floor length.
constexpr double kPxPerMs = 0.22;
constexpr double kScreenFraction = 0.6;
constexpr int kMinWidth = 80;

// Bar pixel length: total gesture time (lead + window) scaled by kPxPerMs,
// clamped to [kMinWidth, screenWidth * kScreenFraction]. screenWidth <= 0
// (surface not yet on an output) falls back to the shared assumption.
inline int barLength(int totalMs, int screenWidth) {
    const int sw = screenWidth > 0 ? screenWidth : render::kFallbackScreenWidth;
    const int maxWidth = static_cast<int>(std::lround(sw * kScreenFraction));
    const int raw = static_cast<int>(std::lround(totalMs * kPxPerMs));
    return std::clamp(raw, kMinWidth, std::max(kMinWidth, maxWidth));
}

// Lead-segment length, proportional to lead : total within the bar.
inline int leadLength(int barLen, int leadMs, int totalMs) {
    if (totalMs <= 0) {
        return 0;
    }
    return static_cast<int>(
        std::lround(static_cast<double>(barLen) * leadMs / totalMs));
}

// Pixel centre of grid column `col` (0..6): the 7 columns sit at uniform
// 12.5 % steps, so column c centres at (c+1)/8 of the screen width. The one
// column-centre formula, shared by the cycle overlay's anchor math
// (overlay/main.cpp) and the panel placement below.
inline int gridColumnCenter(int col, int screenWidth) {
    return screenWidth * (col + 1) / 8;
}

// Left margin (from the output's left edge) that centres a frameWidth-wide
// panel on grid column `col` (0..6) while keeping the whole windowWidth-wide
// surface (panel + bar overhang) on screen by `edgeMargin`.
inline int gridPanelLeftMargin(int col, int screenWidth, int frameWidth,
                               int windowWidth, int edgeMargin) {
    const int center = gridColumnCenter(col, screenWidth);
    const int left = center - frameWidth / 2;
    const int maxLeft =
        std::max(edgeMargin, screenWidth - windowWidth - edgeMargin);
    return std::clamp(left, edgeMargin, maxLeft);
}

} // namespace progress
} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_PROGRESS_OVERLAY_GEOMETRY_H
