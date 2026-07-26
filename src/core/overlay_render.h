// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_OVERLAY_RENDER_H
#define SCHNELLE_ZEICHEN_CORE_OVERLAY_RENDER_H

// Renderer-level constants and the overlay daemon's show/hide decision
// table. Free of Qt/QML so it stays unit-testable and can be included from
// both processes without dragging in the layer-shell stack. 1:1 port of the
// legacy overlay_render.h.

#include "overlay_protocol.h"

#include <string>

namespace schnelle_zeichen {
namespace render {

// Assumed output width before the surface is bound to one (no screen known
// yet); keeps the placement math on a sane scale until real geometry
// arrives.
constexpr int kFallbackScreenWidth = 1920;

// Assumed overlay size before the QML window has been laid out (width and
// height still report 0). The fractional-column and at-cursor margins
// derive from the overlay's own size, so a plausible stand-in beats a zero.
constexpr int kFallbackOverlayWidth = 200;
constexpr int kFallbackOverlayHeight = 64;

// Distance the anchored overlay keeps from the output's edges, and the floor
// for every grid panel margin. Defined here so the daemon's anchor code and
// the placement tests consume one value instead of each spelling out 24.
constexpr int kEdgeMargin = 24;

enum class RenderAction {
    // Leave the window as it is: content changed (variants, index, theme,
    // progress) and the QML bindings render it on the surface already up.
    // The cycling case; the daemon must not rebuild anything per keystroke.
    None,
    // Hide the window. The engine and its QML stay alive.
    Hide,
    // Anchor for `position` and show. Qt drops the wl_surface on hide and
    // builds a fresh layer surface on show, so new anchors take effect.
    Show,
};

// What the controller is asking for.
struct RenderRequest {
    bool visible;
    bool hasVariants;
    std::string position;
    // Label mode renders one full-width name instead of glyph cells; very
    // different width, must not reuse a grid-mode surface.
    bool label;
};

// What the renderer has already committed to. `active` is true from the
// decision to show until the window is hidden again, INCLUDING while an
// async cursor query is still in flight.
struct RenderState {
    bool active;
    std::string position;
    bool label;
};

// The renderer's one decision. Nothing to show means hide; an active window
// at the same position and mode needs no work (its bindings do it);
// anything else needs a re-anchored surface.
inline RenderAction decideRenderAction(const RenderRequest &req,
                                       const RenderState &state) {
    if (!req.visible || !req.hasVariants) {
        return RenderAction::Hide;
    }
    if (state.active && state.position == req.position &&
        state.label == req.label) {
        return RenderAction::None;
    }
    return RenderAction::Show;
}

// kNoHighlightIndex is the engine saying "a gesture just opened", the only
// signal that can tell a re-triggered key apart from a cycling step (the
// commit flash leaves the same variants visible for a moment).
inline bool opensGesture(int currentIndex) {
    return currentIndex <= kNoHighlightIndex;
}

// Does this Show have to snap the QML transitions, or may it animate? A
// Show starting a NEW gesture must snap (the properties still hold the last
// gesture's values; animating them on a visible surface is the flash); a
// Show moving the highlight within the open gesture is the handover the
// animation exists for.
inline bool showSnapsTransitions(bool wasVisible, bool variantsChanged,
                                 int currentIndex) {
    return !wasVisible || variantsChanged || opensGesture(currentIndex);
}

// Placement epoch: async cursor replies can outlive the gesture that asked.
// Bumped on every hide and show, captured by value in deferred work,
// compared before that work touches the window.
using RenderEpoch = unsigned long long;
constexpr RenderEpoch kFirstEpoch = 1;

inline RenderEpoch nextEpoch(RenderEpoch epoch) { return epoch + 1; }

inline bool isEpochCurrent(RenderEpoch captured, RenderEpoch current) {
    return captured == current;
}

} // namespace render
} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_OVERLAY_RENDER_H
