// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_CARET_OVERLAY_GEOMETRY_H
#define SCHNELLE_ZEICHEN_CORE_CARET_OVERLAY_GEOMETRY_H

// Pure geometry + wire-format helpers for the "show at the text caret" overlay
// mode (OverlayPlacement::TextCaret). The caret rectangle is supplied by a
// FocusSource (AT-SPI on Linux); its top-left position (screen px) is reliable,
// but its width/height are often unusable, since several toolkits report zero
// or negative character extents over AT-SPI. So the vertical size falls back to
// a fixed line height when the reported height is not positive. Free of Qt/DBus
// so it is unit-testable and shared by the engine (which composes the wire
// string) and the overlay daemon (which parses it and places the surface).
//
// Wire format: the caret rect travels inside the SAME position string the Show
// call already carries, so no protocol bump is needed:
//   "Caret:<x>,<y>,<w>,<h>;<gridFallback>"
// The trailing grid position is the fallback the daemon uses when the rect is
// missing or lands off-screen; it mirrors the "Cursor:" prefix already used for
// pointer placement (cursor_overlay_geometry.h).

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

namespace schnelle_zeichen {

// Fallback height of one text line when the caret rect's own height is unusable
// (<= 0), and the gap between the caret line and the overlay's near edge.
inline constexpr int kCaretFallbackLineHeight = 24;
inline constexpr int kCaretGap = 4;

// Marker the engine prepends to the position string in TextCaret placement.
inline constexpr const char *kCaretPositionPrefix = "Caret:";

struct CaretRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0; // screen pixels; w/h may be unusable (see header note)
    bool operator==(const CaretRect &o) const {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
};

// The largest caret coordinate magnitude treated as plausible. Some toolkits
// report a wild sentinel (seen in the probe: x near -1.5e9) for a non-text
// focus; a bound well past any real multi-monitor layout rejects those without
// clipping a genuine caret.
inline constexpr int kCaretMaxCoord = 100000;

// Whether a raw AT-SPI character-extents rect is a usable caret anchor. AT-SPI
// emits an all-zero rect as noise for non-caret widgets (and for carets in
// toolkits that expose no per-character extents, e.g. the probe's browser
// combo box), so that is rejected; an out-of-range top-left (the sentinel
// above) is rejected too. Width/height may still be unusable, which the
// placement (caretMargins) already tolerates by deriving the line height.
inline bool isUsableCaretRect(const CaretRect &r) {
    if (r.x == 0 && r.y == 0 && r.w == 0 && r.h == 0) {
        return false;
    }
    if (r.x < -kCaretMaxCoord || r.x > kCaretMaxCoord ||
        r.y < -kCaretMaxCoord || r.y > kCaretMaxCoord) {
        return false;
    }
    return true;
}

struct CaretPositionSpec {
    // True when the daemon should place the overlay at `rect`, falling back to
    // `grid` if the rect is off-screen.
    bool atCaret = false;
    CaretRect rect;
    // The "<Row><Col>" fallback when atCaret, otherwise the position string
    // itself (so a non-Caret input passes through unchanged to the cursor/grid
    // parser).
    std::string grid;
};

// Compose the wire string from a caret rect and the grid fallback.
inline std::string caretPositionString(const CaretRect &r,
                                       const std::string &grid) {
    return std::string(kCaretPositionPrefix) + std::to_string(r.x) + "," +
           std::to_string(r.y) + "," + std::to_string(r.w) + "," +
           std::to_string(r.h) + ";" + grid;
}

namespace detail {

// Full-match signed-int parse: returns false on empty input, a non-numeric
// tail, or a value outside int range, so a malformed field invalidates the
// whole caret rect instead of being read as a truncated prefix.
inline bool parseInt(const std::string &s, int &out) {
    if (s.empty()) {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size()) {
        return false;
    }
    if (v < INT_MIN || v > INT_MAX) {
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

} // namespace detail

// Parse the position string. On the "Caret:" prefix, extract the four
// comma-separated ints and the grid fallback; any malformation (wrong prefix,
// missing ';', not exactly four numeric fields) yields atCaret=false with
// `grid` set to the whole input, so the caller degrades to its cursor/grid
// handling.
inline CaretPositionSpec parseCaretPosition(const std::string &position) {
    CaretPositionSpec spec;
    const std::string prefix = kCaretPositionPrefix;
    if (position.rfind(prefix, 0) != 0) {
        spec.grid = position;
        return spec;
    }
    const size_t semi = position.find(';', prefix.size());
    if (semi == std::string::npos) {
        spec.grid = position; // no fallback field: treat as opaque
        return spec;
    }
    const std::string nums =
        position.substr(prefix.size(), semi - prefix.size());
    const std::string grid = position.substr(semi + 1);

    int vals[4];
    size_t field = 0;
    size_t start = 0;
    bool ok = true;
    for (size_t i = 0; i <= nums.size() && ok; ++i) {
        if (i == nums.size() || nums[i] == ',') {
            if (field >= 4) {
                ok = false;
                break;
            }
            ok = detail::parseInt(nums.substr(start, i - start), vals[field]);
            ++field;
            start = i + 1;
        }
    }
    if (!ok || field != 4) {
        spec.grid = position; // malformed: pass the whole string through
        return spec;
    }
    spec.atCaret = true;
    spec.rect = {vals[0], vals[1], vals[2], vals[3]};
    spec.grid = grid;
    return spec;
}

struct CaretMargins {
    // Layer-shell margins for a Top|Left-anchored surface, in the anchored
    // output's local pixels.
    int left;
    int top;
};

// Place the overlay just below the caret line, its left edge aligned to the
// caret; flip it above the line when it would spill past the output's bottom.
// The caret rect is in screen coordinates; the result is clamped onto the
// caret's output (screenX/Y/W/H) so the whole overlay stays visible. The line
// height uses the caret's own height when positive, else the fallback.
inline CaretMargins
caretMargins(const CaretRect &caret, int screenX, int screenY, int screenW,
             int screenH, int overlayW, int overlayH,
             int fallbackLineHeight = kCaretFallbackLineHeight,
             int gap = kCaretGap) {
    const int lineHeight = caret.h > 0 ? caret.h : fallbackLineHeight;
    const int localX = caret.x - screenX;
    const int localY = caret.y - screenY;
    int left = localX;
    int top = localY + lineHeight + gap; // below the caret line
    if (top + overlayH > screenH) {
        top =
            localY - overlayH - gap; // would spill past the bottom: flip above
    }
    const int maxLeft = std::max(0, screenW - overlayW);
    const int maxTop = std::max(0, screenH - overlayH);
    left = std::clamp(left, 0, maxLeft);
    top = std::clamp(top, 0, maxTop);
    return {left, top};
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_CARET_OVERLAY_GEOMETRY_H
