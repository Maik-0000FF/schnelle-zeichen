// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_OVERLAY_PROTOCOL_H
#define SCHNELLE_ZEICHEN_CORE_OVERLAY_PROTOCOL_H

// Wire protocol for the de.schnelle_zeichen.Overlay1 D-Bus interface, the
// single source both the engine-side client and the overlay daemon consume.
// Improvement over legacy (named): the service identifiers live here too;
// they were duplicated string literals on each side before.
//
// Bump kOverlayProtocolVersion on EVERY change to a method signature on the
// interface (a new, removed, reordered or retyped argument). After an
// in-place upgrade an old daemon can keep owning the bus name; the version
// handshake lets the client detect and restart it (see overlay_lifecycle.h).

namespace schnelle_zeichen {

// Fresh product, fresh protocol lineage.
constexpr int kOverlayProtocolVersion = 1;

// The currentIndex a Show carries when NO cell is highlighted: the engine
// sends it while a gesture's accent window is still open (preview before a
// leader) and for a standalone profile-name pill. The daemon also reads it
// as "a gesture just opened" (see overlay_render.h, opensGesture), so it is
// a cross-process contract.
constexpr int kNoHighlightIndex = -1;

inline constexpr const char *kOverlayService = "de.schnelle_zeichen.Overlay";
inline constexpr const char *kOverlayPath = "/de/schnelle_zeichen/Overlay";
inline constexpr const char *kOverlayInterface =
    "de.schnelle_zeichen.Overlay1";

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_OVERLAY_PROTOCOL_H
