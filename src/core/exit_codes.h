// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_EXIT_CODES_H
#define SCHNELLE_ZEICHEN_CORE_EXIT_CODES_H

// Exit codes the daemon uses to tell a service manager whether restarting is
// worth anything. This header is the source of truth for the value; the
// systemd units in install.sh and nix/home-module.nix mirror it in their
// RestartPreventExitStatus and name this header in a comment.

namespace schnelle_zeichen {

// The session can never support the daemon, no matter how often it is
// restarted: no compositor protocol to inject text through (GNOME/Mutter,
// native X11). EX_UNAVAILABLE from sysexits.h, which is exactly this case: a
// required service is not available. A plain failure exit (1) stays reserved
// for conditions that may well heal on the next try, such as a keyboard that
// is not plugged in yet.
inline constexpr int kExitSessionUnsupported = 69;

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_EXIT_CODES_H
