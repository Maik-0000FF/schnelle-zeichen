// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mappings_loader.h"

#include "config_dir.h"
#include "log.h"
#include "mappings_io.h"
#include "profile_paths.h"

#include <unistd.h>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace schnelle_zeichen {

namespace {

struct FileCloser {
    void operator()(FILE *fp) const { std::fclose(fp); }
};
using FilePtr = std::unique_ptr<FILE, FileCloser>;

// Open a config-root-relative file for reading; null when the config root or
// the file is absent.
FilePtr openConfigFile(const std::string &relPath) {
    const std::string path = configFilePath(relPath);
    if (path.empty()) {
        return nullptr;
    }
    return FilePtr(std::fopen(path.c_str(), "r"));
}

// Open a config-root-relative file and run `parse` on it, returning a
// default-constructed result when the file is absent. Shared by the manifest
// and usage loaders.
template <typename Parse>
auto openAndParse(const std::string &relPath, Parse parse)
    -> decltype(parse(std::declval<FILE *>())) {
    using Result = decltype(parse(std::declval<FILE *>()));
    if (auto fp = openConfigFile(relPath)) {
        return parse(fp.get());
    }
    return Result{};
}

} // namespace

UmlautMap loadMappingsFromFile(const std::string &relPath) {
    UmlautMap map;
    if (auto fp = openConfigFile(relPath)) {
        for (const auto &m : parseMappings(fp.get())) {
            auto outputs = splitOutputs(m.output);
            if (outputs.empty()) {
                warn("mapping '" + m.input + "' has no valid outputs, skipped");
                continue;
            }
            map[m.input] = std::move(outputs);
        }
    }
    // The built-in German defaults are a first-install convenience for the
    // Standard profile only. Other profiles stay genuinely empty when their
    // file is missing/empty, so a freshly created profile starts blank instead
    // of inheriting the umlaut set.
    if (map.empty() && isStandardProfile(relPath)) {
        for (const auto &m : defaultMappings()) {
            map[m.input] = splitOutputs(m.output);
        }
    }
    return map;
}

UmlautMap loadMappingsFromFile() { return loadMappingsFromFile(kMappingsFile); }

MergeManifest loadMergeManifest() {
    return openAndParse(kMergeConf, parseMergeManifest);
}

UsageCounts loadUsage() { return openAndParse(kUsageFile, parseUsage); }

EngineConfig loadEngineConfig() {
    return openAndParse(kSettingsFile, parseEngineConfig);
}

ProfilesData loadProfiles() {
    ProfilesData data = openAndParse(kProfilesConf, parseProfiles);
    applyProfileFallbacks(data);
    return data;
}

bool saveUsage(const UsageCounts &counts) {
    const std::string path = configFilePath(kUsageFile);
    if (path.empty()) {
        return false;
    }
    // First save on a fresh system: the config root may not exist yet.
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    if (ec) {
        return false;
    }
    // Write everything to a sibling temp file, flush it to disk, then rename
    // into place. rename() within one directory is atomic on POSIX, so a
    // concurrent editor read never sees a half-written file (mirrors the
    // fcitx StandardPaths safeSave behaviour). The engine is the sole writer
    // of usage.conf, so the fixed temp name cannot race another writer.
    const std::string tmpPath = path + ".tmp";
    FILE *fp = std::fopen(tmpPath.c_str(), "w");
    if (fp == nullptr) {
        return false;
    }
    const std::string data = serializeUsage(counts);
    bool ok = std::fwrite(data.data(), 1, data.size(), fp) == data.size();
    ok = ok && std::fflush(fp) == 0;
    ok = ok && ::fsync(fileno(fp)) == 0;
    if (std::fclose(fp) != 0) {
        ok = false;
    }
    ok = ok && std::rename(tmpPath.c_str(), path.c_str()) == 0;
    if (!ok) {
        std::error_code removeEc;
        std::filesystem::remove(tmpPath, removeEc);
    }
    return ok;
}

void deleteUsage() {
    const std::string path = configFilePath(kUsageFile);
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

bool takeUsageResetMarker() {
    const std::string path = configFilePath(kUsageResetMarker);
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return false;
    }
    std::filesystem::remove(path, ec);
    return true;
}

} // namespace schnelle_zeichen
