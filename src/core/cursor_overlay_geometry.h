// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_CURSOR_OVERLAY_GEOMETRY_H
#define SCHNELLE_ZEICHEN_CORE_CURSOR_OVERLAY_GEOMETRY_H

// Pure geometry + wire-format helpers for the "show at mouse cursor"
// overlay mode. Free of Qt/DBus deps so it is unit-testable and shared by
// the overlay daemon's renderer. 1:1 port of the legacy header.

#include <algorithm>
#include <string>

namespace schnelle_zeichen {

// Marker the engine prepends to the position string when cursor mode is on:
// "Cursor:" + the grid fallback (e.g. "Cursor:TopCol4"). Defined once so
// the writer (engine) and the reader (daemon) can't drift.
inline const std::string &cursorPositionPrefix() {
    static const std::string kPrefix = "Cursor:";
    return kPrefix;
}

struct CursorPositionSpec {
    // True when the daemon should try to place the overlay at the pointer.
    bool atCursor;
    // The plain grid position ("TopCol4", ...): the fallback when atCursor,
    // otherwise the position itself.
    std::string grid;
};

inline CursorPositionSpec parseCursorPosition(const std::string &position) {
    const std::string &prefix = cursorPositionPrefix();
    if (position.rfind(prefix, 0) == 0) {
        return {true, position.substr(prefix.size())};
    }
    return {false, position};
}

struct CursorMargins {
    // Layer-shell margins for a Top|Left-anchored surface, in the anchored
    // output's local pixels.
    int left;
    int top;
};

// Place the overlay's LOWER-LEFT corner at the cursor: the surface extends
// up and to the right of the pointer. Clamped so the whole surface stays on
// the cursor's output (near an edge the corner drifts off the cursor rather
// than spilling off-screen or asking for negative margins).
inline CursorMargins cursorMargins(int cursorX, int cursorY, int screenX,
                                   int screenY, int screenW, int screenH,
                                   int overlayW, int overlayH) {
    const int localX = cursorX - screenX;
    const int localY = cursorY - screenY;
    int left = localX;
    int top = localY - overlayH;
    const int maxLeft = std::max(0, screenW - overlayW);
    const int maxTop = std::max(0, screenH - overlayH);
    left = std::clamp(left, 0, maxLeft);
    top = std::clamp(top, 0, maxTop);
    return {left, top};
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_CURSOR_OVERLAY_GEOMETRY_H
