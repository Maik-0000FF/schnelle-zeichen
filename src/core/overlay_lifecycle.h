// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_OVERLAY_LIFECYCLE_H
#define SCHNELLE_ZEICHEN_CORE_OVERLAY_LIFECYCLE_H

// Pure decision logic for the overlay daemon lifecycle. Free of D-Bus/Qt
// dependencies so it is unit-testable with nothing but the standard
// library. 1:1 port.

#include <optional>

namespace schnelle_zeichen {

enum class OverlayLifecycleAction { None, Start, Quit };

// Given the overlay's Enabled flag in the previous and current config
// states, decide what the daemon lifecycle should do:
//   nullopt -> true : Start (eager, so the daemon is ready for the first
//                     cycling event instead of racing activation latency)
//   nullopt -> false: None
//   false   -> true : Start
//   true    -> false: Quit
//   same    -> same : None
inline OverlayLifecycleAction
decideOverlayLifecycleAction(std::optional<bool> previous, bool current) {
    if (!previous.has_value()) {
        return current ? OverlayLifecycleAction::Start
                       : OverlayLifecycleAction::None;
    }
    if (!*previous && current) {
        return OverlayLifecycleAction::Start;
    }
    if (*previous && !current) {
        return OverlayLifecycleAction::Quit;
    }
    return OverlayLifecycleAction::None;
}

// Whether an already-running daemon must be quit because its wire protocol
// no longer matches (a stale in-place-upgrade leftover). Only meaningful
// when a daemon owns the bus name; a version-query failure reads as a
// daemon predating the handshake, i.e. stale.
inline bool overlayDaemonIsStale(bool hasOwner, bool gotVersion,
                                 int reportedVersion, int expectedVersion) {
    if (!hasOwner) {
        return false;
    }
    if (!gotVersion) {
        return true;
    }
    return reportedVersion != expectedVersion;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_OVERLAY_LIFECYCLE_H
