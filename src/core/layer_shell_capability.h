// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_LAYER_SHELL_CAPABILITY_H
#define SCHNELLE_ZEICHEN_CORE_LAYER_SHELL_CAPABILITY_H

// Pure detection for wlr-layer-shell support in the current session. Free
// of Qt/D-Bus deps so it is unit-testable and reusable by the engine-side
// overlay client and the future editor. 1:1 port (including the blocklist
// approach: a Wayland session is assumed capable unless it is a known
// holdout, which is what keeps compositors like Mango working without an
// allowlist entry).

#include <cstdlib>
#include <cstring>
#include <string>

namespace schnelle_zeichen {

struct LayerShellCapability {
    bool supported;
    // Human-readable session descriptor, e.g. "GNOME (Wayland)".
    std::string session;
    // Empty when supported; otherwise one sentence explaining the limit.
    std::string reason;
};

namespace detail {

// Case-insensitive substring match over the colon-separated
// XDG_CURRENT_DESKTOP value.
inline bool containsCI(const char *haystack, const char *needle) {
    if (haystack == nullptr || needle == nullptr) {
        return false;
    }
    const auto hlen = std::strlen(haystack);
    const auto nlen = std::strlen(needle);
    if (nlen == 0 || nlen > hlen) {
        return false;
    }
    for (std::size_t i = 0; i + nlen <= hlen; ++i) {
        std::size_t j = 0;
        for (; j < nlen; ++j) {
            const char a = haystack[i + j];
            const char b = needle[j];
            const char la = (a >= 'A' && a <= 'Z') ? char(a - 'A' + 'a') : a;
            const char lb = (b >= 'A' && b <= 'Z') ? char(b - 'A' + 'a') : b;
            if (la != lb) {
                break;
            }
        }
        if (j == nlen) {
            return true;
        }
    }
    return false;
}

} // namespace detail

// sessionType = XDG_SESSION_TYPE, currentDesktop = XDG_CURRENT_DESKTOP;
// both may be null/empty.
inline LayerShellCapability
checkLayerShellCapability(const char *sessionType, const char *currentDesktop) {
    using detail::containsCI;

    const std::string desktop = (currentDesktop != nullptr && *currentDesktop)
                                    ? currentDesktop
                                    : "unknown";
    const bool isWayland =
        sessionType != nullptr && std::strcmp(sessionType, "wayland") == 0;
    const bool isX11 =
        sessionType != nullptr && std::strcmp(sessionType, "x11") == 0;

    LayerShellCapability cap{false, "", ""};
    cap.session = desktop;
    if (isWayland) {
        cap.session += " (Wayland)";
    } else if (isX11) {
        cap.session += " (X11)";
    }

    if (!isWayland) {
        cap.reason = "wlr-layer-shell is a Wayland-only protocol. "
                     "X11 sessions cannot host the overlay.";
        return cap;
    }
    // GNOME's Mutter refuses to implement wlr-layer-shell; Unity rides on
    // the same stack. Fail fast so the daemon is never started there.
    if (containsCI(currentDesktop, "GNOME") ||
        containsCI(currentDesktop, "Unity")) {
        cap.reason = "GNOME/Mutter does not implement wlr-layer-shell. "
                     "The overlay will not cycle correctly.";
        return cap;
    }
    cap.supported = true;
    return cap;
}

inline LayerShellCapability detectLayerShellCapability() {
    const char *sessionType = std::getenv("XDG_SESSION_TYPE");
    // Deviation from legacy (named): privileged contexts (sudo) strip
    // XDG_SESSION_TYPE while the Wayland socket env survives; a present
    // WAYLAND_DISPLAY is just as conclusive, so fall back to it instead of
    // misreading the session as X11.
    if ((sessionType == nullptr || *sessionType == '\0') &&
        std::getenv("WAYLAND_DISPLAY") != nullptr) {
        sessionType = "wayland";
    }
    return checkLayerShellCapability(sessionType,
                                     std::getenv("XDG_CURRENT_DESKTOP"));
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_LAYER_SHELL_CAPABILITY_H
