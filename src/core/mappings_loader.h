// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_MAPPINGS_LOADER_H
#define SCHNELLE_ZEICHEN_CORE_MAPPINGS_LOADER_H

// Runtime mappings layer on top of the format-level parser in mappings_io.h.
// parseMappings returns raw input->output strings; the loader also expands
// comma-separated cycling variants (format v2 backslash escapes) and knows
// how to find the files under schnelle-zeichen's config root (config_dir.h).
// It is the one place that resolves config files for the engine, so the
// merge manifest, the usage-counter IO, the engine settings and the profile
// metadata all load here (the editor resolves the same files through its own
// path helper, but both sides share the format headers below).

#include "engine_config.h"     // EngineConfig (shared format)
#include "merge_manifest_io.h" // MergeManifest (shared format)
#include "profiles_io.h"       // ProfilesData (shared format)
#include "usage_io.h"          // UsageCounts (shared format)

#include <string>
#include <unordered_map>
#include <vector>

namespace schnelle_zeichen {

// Runtime mapping table: input UTF-8 character -> cycling output variants.
// Order within the variant list defines the cycling sequence.
using UmlautMap = std::unordered_map<std::string, std::vector<std::string>>;

// Load mappings from a config file relative to the config root, e.g.
// "mappings.txt" for the Standard profile or "profiles/<slug>.txt" for
// another profile. Only the Standard profile falls back to defaultMappings()
// when the file is absent, empty, or every parsed entry splits into zero
// variants (a first-install convenience); any other profile then yields an
// empty map, so a freshly created profile starts blank. Individual malformed
// entries are skipped with a warn() but do not abort the load.
UmlautMap loadMappingsFromFile(const std::string &relPath);

// Convenience overload for the Standard profile (kMappingsFile).
UmlautMap loadMappingsFromFile();

// Load the single global merge manifest (kMergeConf). Returns an empty
// manifest (no base) when the file is absent, which the engine reads as
// "no merge".
MergeManifest loadMergeManifest();

// Load the per-(base, variant) usage counters (kUsageFile). Returns an empty
// table when the file is absent.
UsageCounts loadUsage();

// Load the engine configuration (kSettingsFile). A missing file, missing
// keys or invalid values yield the legacy defaults.
EngineConfig loadEngineConfig();

// Load the profile metadata (kProfilesConf) with the runtime fallbacks
// applied: a missing or empty file yields the seeded Standard profile, an
// unknown active name falls back to the first entry.
ProfilesData loadProfiles();

// Atomically write the usage counters (kUsageFile) via a sibling temp file +
// rename. Returns false on failure.
bool saveUsage(const UsageCounts &counts);

// Atomically write the profile metadata (kProfilesConf), the engine-side
// persistence of an Active switch. Same temp + rename scheme as saveUsage.
bool saveProfiles(const ProfilesData &data);

// Delete usage.conf (the usage counters). No-op if absent. Used by the reset.
void deleteUsage();

// If the usage-reset request marker (kUsageResetMarker) exists, delete it and
// return true. The editor drops the marker and reloads the engine; the engine
// consumes it here to clear the counts and delete usage.conf.
bool takeUsageResetMarker();

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_MAPPINGS_LOADER_H
