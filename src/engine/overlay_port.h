// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_ENGINE_OVERLAY_PORT_H
#define SCHNELLE_ZEICHEN_ENGINE_OVERLAY_PORT_H

// Narrow interface for the cycling-preview overlay. The engine drives the
// choreography (trigger preview, progress bar, cycling highlight, commit
// flash, profile-name flash) through these primitives; how they are drawn
// (layer-shell daemon, caret candidate window) is the overlay's concern
// (phase 5). A null implementation is valid: the engine works headless.

#include "overlay_protocol.h" // kNoHighlightIndex (cross-process contract)

#include <cstdint>
#include <string>
#include <vector>

namespace schnelle_zeichen {

class OverlayPort {
public:
    virtual ~OverlayPort() = default;

    // Show the variants, highlighting `index` (kNoHighlightIndex = none).
    virtual void show(const std::vector<std::string> &variants, int index) = 0;
    virtual void hide() = 0;

    // Timing bar for the accent window: lead-in (min hold) then the [min,
    // max] window countdown, anchored at the gesture's monotonic start.
    // holdMs > 0 marks the long-press auto-select point on the same
    // timeline (0 = auto-select off, no marker).
    virtual void setProgress(int leadMs, int windowMs, int holdMs,
                             uint64_t startUsec) = 0;
    // Hold the bar once a leader caught the window and cycling begins.
    virtual void freezeProgress() = 0;

    // Brief on-switch feedback with the new profile's name.
    virtual void showProfileName(const std::string &name) = 0;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_ENGINE_OVERLAY_PORT_H
