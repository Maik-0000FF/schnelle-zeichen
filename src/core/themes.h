// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_THEMES_H
#define SCHNELLE_ZEICHEN_CORE_THEMES_H

// The C++ side's list of accepted theme names, used by the overlay
// controller's SetTheme guard (and later the editor). Must stay in sync
// with the palettes in src/overlay/palette/Palettes.qml: a name here
// without a palette entry renders with the fallback, and vice versa.
// Qt-free port of the legacy themes.h; the default theme id is renamed to
// the product ("schnelle-zeichen", was "schnelle-umlaute").

#include <array>
#include <string_view>

namespace schnelle_zeichen {

inline constexpr const char *kDefaultTheme = "schnelle-zeichen";

inline constexpr std::array<std::string_view, 14> kThemeIds = {
    "schnelle-zeichen",
    "dark",
    "light",
    "contrast",
    "catppuccin-mocha",
    "catppuccin-latte",
    "nord",
    "gruvbox-dark",
    "dracula",
    "tokyo-night",
    "rose-pine",
    "solarized-light",
    "eldritch",
    "kanagawa"};

inline bool isValidTheme(std::string_view name) {
    for (const auto id : kThemeIds) {
        if (id == name) {
            return true;
        }
    }
    return false;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_THEMES_H
