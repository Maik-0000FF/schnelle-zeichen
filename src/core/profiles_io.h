// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_PROFILES_IO_H
#define SCHNELLE_ZEICHEN_CORE_PROFILES_IO_H

// Parser for profiles.conf (profile metadata: list, active name, cycle
// hotkeys). Mirrors the legacy editor's ProfileListModel reader, the
// authority on this format: top-level keys Active/CycleNext/CyclePrev,
// [Profiles/N] sections with Name/File/SelectKey/Favorite, values may be
// quoted (e.g. a name with spaces; see ini_io.h). Entry validation: name and
// file non-empty, file passes isSafeProfileFile, no duplicate name or file
// (name dedupe is ASCII-case-insensitive; a deliberate simplification of
// Qt's Unicode toLower, fine for a dedupe heuristic).
//
// Hotkeys stay portable combo strings ("Control+Alt+1"); parsing them into
// key events is the engine's job, not the config layer's.
//
// applyProfileFallbacks mirrors the runtime fallbacks: an empty list seeds
// the protected Standard profile, an Active naming no entry falls back to
// the first. Editor-only concerns (loose-profile adoption, persisting the
// seed, engine reload) stay in the editor.
//
// CONTRACT: the key literals are the on-disk format shared with the future
// editor port; keep both sides in sync when changing any.

#include "ini_io.h"
#include "profile_paths.h" // isSafeProfileFile, kStandardProfile, kMappingsFile

#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace schnelle_zeichen {

struct ProfileEntry {
    std::string name;
    std::string file;
    std::string selectKey;
    bool favorite = false;
};

struct ProfilesData {
    std::vector<ProfileEntry> entries;
    std::string active;
    std::string cycleNext;
    std::string cyclePrev;
};

// Trimmed, ASCII-lowercased form used only for duplicate detection.
inline std::string normalizedProfileName(std::string_view name) {
    const std::string_view t = trimIni(name);
    std::string out;
    out.reserve(t.size());
    for (const char c : t) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

inline ProfilesData profilesFromIni(const IniDocument &doc) {
    ProfilesData data;
    const IniSection *top = findIniSection(doc, "");
    if (top != nullptr) {
        data.active = parseIniString(top, "Active");
        data.cycleNext = parseIniString(top, "CycleNext");
        data.cyclePrev = parseIniString(top, "CyclePrev");
    }
    constexpr std::string_view kSectionPrefix = "Profiles/";
    for (const auto &section : doc) {
        if (section.name.compare(0, kSectionPrefix.size(), kSectionPrefix) !=
            0) {
            continue;
        }
        ProfileEntry e;
        e.name = parseIniString(&section, "Name");
        e.file = parseIniString(&section, "File");
        e.selectKey = parseIniString(&section, "SelectKey");
        e.favorite = parseIniBool(&section, "Favorite", false);
        if (e.name.empty() || e.file.empty() || !isSafeProfileFile(e.file)) {
            continue;
        }
        bool duplicate = false;
        for (const auto &existing : data.entries) {
            if (normalizedProfileName(existing.name) ==
                    normalizedProfileName(e.name) ||
                existing.file == e.file) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            data.entries.push_back(std::move(e));
        }
    }
    return data;
}

inline ProfilesData parseProfiles(FILE *fp) {
    return profilesFromIni(parseIni(fp));
}

// Serialize the profiles data in the exact on-disk form the legacy editor
// writes (values escaped per ini_io.h, [Profiles/N] sections in list order), so
// engine writes (an Active switch) and editor writes round-trip identically.
inline std::string serializeProfiles(const ProfilesData &data) {
    std::string out;
    out += "# Mapping profiles for schnelle-zeichen.\n";
    out += "Active=" + escapeIniValue(data.active) + "\n";
    out += "CycleNext=" + escapeIniValue(data.cycleNext) + "\n";
    out += "CyclePrev=" + escapeIniValue(data.cyclePrev) + "\n";
    for (size_t i = 0; i < data.entries.size(); ++i) {
        const ProfileEntry &e = data.entries[i];
        out += "\n[Profiles/" + std::to_string(i) + "]\n";
        out += "Name=" + escapeIniValue(e.name) + "\n";
        out += "File=" + escapeIniValue(e.file) + "\n";
        out += "SelectKey=" + escapeIniValue(e.selectKey) + "\n";
        out +=
            std::string("Favorite=") + (e.favorite ? "True" : "False") + "\n";
    }
    return out;
}

// Runtime fallbacks: never leave the engine without a valid active profile.
inline void applyProfileFallbacks(ProfilesData &data) {
    if (data.entries.empty()) {
        // Fresh install / corrupt file: materialize the Standard profile
        // (pointing at mappings.txt) and drop any dangling Active.
        data.entries.push_back({kStandardProfile, kMappingsFile, "", false});
        data.active = kStandardProfile;
        return;
    }
    for (const auto &entry : data.entries) {
        if (entry.name == data.active) {
            return;
        }
    }
    data.active = data.entries.front().name;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_PROFILES_IO_H
