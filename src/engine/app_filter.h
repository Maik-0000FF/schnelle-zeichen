// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_ENGINE_APP_FILTER_H
#define SCHNELLE_ZEICHEN_ENGINE_APP_FILTER_H

// Per-application enable/disable filter, ported from the legacy AppFilter
// as a pure function over the focused app id (the InputContext dependency
// dropped; FocusSource supplies the id). Substring matching and the
// empty-entry skip are byte-for-byte legacy semantics.

#include "engine_config.h"

#include <string>

namespace schnelle_zeichen {

// Whether processing should be skipped for this app. Disabled mode never
// filters. An empty app id is "unknown": blacklisted nothing (false) or not
// whitelisted (true) depending on mode. Empty list entries are skipped so a
// stray blank line cannot disable the engine entirely (blacklist) or bypass
// the filter entirely (whitelist).
inline bool isFilteredApp(const AppFilterConfig &filter,
                          const std::string &program) {
    if (filter.mode == AppFilterMode::Disabled) {
        return false;
    }
    if (program.empty()) {
        return filter.mode == AppFilterMode::Whitelist;
    }
    if (filter.mode == AppFilterMode::Blacklist) {
        for (const auto &app : filter.blacklist) {
            if (app.empty()) {
                continue;
            }
            if (program.find(app) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    for (const auto &app : filter.whitelist) {
        if (app.empty()) {
            continue;
        }
        if (program.find(app) != std::string::npos) {
            return false;
        }
    }
    return true;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_ENGINE_APP_FILTER_H
