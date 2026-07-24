// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_CONTROL_PROTOCOL_H
#define SCHNELLE_ZEICHEN_CORE_CONTROL_PROTOCOL_H

// Wire protocol for the de.schnelle_zeichen.Engine1 D-Bus control interface,
// the single source for the engine-side service and its clients (tray, and
// optionally an OS-level shortcut invoking a one-shot call). Session bus.
//
// Methods:
//   Pause()            pause: every key event is forwarded untouched; only
//                      the configured pause-toggle shortcut is still matched
//   Resume()           leave the paused state
//   Toggle() -> b      flip and return the new paused state
//   GetPaused() -> b   current state
//   Quit()             stop the engine daemon (releases the grab)
// Signal:
//   PausedChanged(b)   emitted on every state change, for the tray checkbox
//
// The pause state is a runtime flag on purpose: it touches no config file,
// so resuming restores exactly the prior state and toggling never churns
// profiles.conf or settings.conf.

namespace schnelle_zeichen {

inline constexpr const char *kEngineService = "de.schnelle_zeichen.Engine";
inline constexpr const char *kEnginePath = "/de/schnelle_zeichen/Engine";
inline constexpr const char *kEngineInterface = "de.schnelle_zeichen.Engine1";

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_CONTROL_PROTOCOL_H
