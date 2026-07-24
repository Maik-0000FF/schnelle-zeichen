// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_PROFILE_PATHS_H
#define SCHNELLE_ZEICHEN_CORE_PROFILE_PATHS_H

// Single source of truth for the file names that cross the editor<->engine
// boundary. The editor and the engine both read/write the same files, so
// these literals must not be duplicated in each: if one side drifted, the two
// would silently disagree on where the mappings and profiles live. Plain
// C-string constants so both QString and std::string can consume them.
//
// Deviation from schnelle-umlaute (deliberate): no kConfigSubdir.
// schnelle-zeichen owns its config root ($XDG_CONFIG_HOME/schnelle-zeichen,
// resolved in config_dir.h) instead of nesting under another framework's
// config dir, so every path below is relative to that root and
// isStandardProfile only needs
// the bare file name.

#include <string>
#include <string_view>

namespace schnelle_zeichen {

// The Standard profile's mappings file (relative to the config root). This is
// the pre-profiles file, kept as-is so existing mappings are never lost.
inline constexpr const char *kMappingsFile = "mappings.txt";
// Subdirectory holding every non-Standard profile's mappings file.
inline constexpr const char *kProfilesSubdir = "profiles";
// Profile metadata (list, active name, cycle hotkeys), relative to the config
// root. Owned by the editor's profile model; read by the engine.
inline constexpr const char *kProfilesConf = "profiles.conf";
// The single global merge manifest (chosen base + ordered appended source
// profiles + per-base order overrides), relative to the config root. Owned by
// the editor; read by the engine, which composes when the base is active.
inline constexpr const char *kMergeConf = "merge.conf";
// Per-(base char, committed variant) usage counters, relative to the config
// root. Written by the engine (the only editor<->engine file in that
// direction), read by the editor to sort variants by usage when the toggle is
// on.
inline constexpr const char *kUsageFile = "usage.conf";
// Engine settings (delays, leaders, app filter, overlay, behavior), relative
// to the config root. Successor of the legacy conf/schnelle-umlaute.conf;
// the content-describing name matches the sibling files in the own root.
inline constexpr const char *kSettingsFile = "settings.conf";
// One-shot sidecar marker requesting a usage-counter reset. The editor writes
// this file and reloads the engine; the engine (sole owner of the in-memory
// counts) consumes it, clearing the counts and deleting usage.conf, then
// removes the marker. Kept out of the main config file on purpose: it is a
// command, not a setting, so no second writer touches the shared config file.
inline constexpr const char *kUsageResetMarker = "usage-reset.request";
// Display name of the protected Standard profile.
inline constexpr const char *kStandardProfile = "Standard";

// A profile's File field must be either the Standard mappings file or a plain
// file directly under the profiles/ subdir. Rejects path traversal / absolute
// paths / nested dirs from a hand-edited or migrated profiles.conf, so neither
// the engine loader nor the editor's delete ever reaches outside the config
// root. Shared by both sides so the rule lives in one place.
inline bool isSafeProfileFile(std::string_view file) {
    if (file == kMappingsFile) {
        return true;
    }
    const std::string prefix = std::string(kProfilesSubdir) + "/";
    if (file.size() <= prefix.size() ||
        file.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const std::string_view rest = file.substr(prefix.size());
    return !rest.empty() && rest.find('/') == std::string_view::npos &&
           rest.find("..") == std::string_view::npos;
}

// True when File refers to the protected Standard profile. With every path
// relative to schnelle-zeichen's own config root, the bare file name is the one
// canonical form on both sides (the legacy dual-form check collapsed with
// kConfigSubdir).
inline bool isStandardProfile(std::string_view file) {
    return file == kMappingsFile;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_PROFILE_PATHS_H
