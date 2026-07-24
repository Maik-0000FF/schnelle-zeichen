// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_INPUT_FOCUSSOURCE_H
#define SCHNELLE_ZEICHEN_INPUT_FOCUSSOURCE_H

#include <string>

namespace schnelle_zeichen {

// Read-only context about the focused application, gathered from side channels
// because a raw input backend has no framework to ask: X11 _NET_ACTIVE_WINDOW,
// AT-SPI for the caret rect, the macOS Accessibility API. Drives per-app
// enable/disable and the caret-following overlay.
//
// Every field is best-effort. A backend that cannot determine one leaves it
// empty / unset rather than guessing, so consumers must treat missing data as
// "unknown", never as a definite value.
struct FocusInfo {
    std::string appId; // executable name or bundle id; empty when unknown

    bool hasCaretRect = false; // false => the x/y/w/h below are meaningless
    int caretX = 0;
    int caretY = 0;
    int caretW = 0;
    int caretH = 0; // screen pixels
};

class FocusSource {
public:
    virtual ~FocusSource() = default;

    // Snapshot of the currently focused app. Cheap to call; a backend caches
    // and refreshes on focus-change signals rather than probing every call.
    virtual FocusInfo current() = 0;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_INPUT_FOCUSSOURCE_H
