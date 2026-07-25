// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_USAGE_IO_H
#define SCHNELLE_ZEICHEN_CORE_USAGE_IO_H

// Parse and serialize the per-(base char, committed variant) usage counters
// (usage.conf). Written by the engine, read by the editor. Kept in one shared
// header so both sides agree on the format (Single Source of Truth), the same
// rationale as mappings_io.h.
//
// Format: one counter per line, tab-separated:
//   <base>\t<variant>\t<count>
// base and variant are opaque UTF-8 fields stored with the shared TSV field
// escaping (escapeTsvField: \t \n \r \\ \#), so snippet variants containing
// tabs or newlines and a '#' base survive the line-oriented format; count is
// a non-negative decimal integer. Lines starting with '#' are comments;
// empty and malformed lines are skipped.

#include "mappings_io.h" // readConfigLine + TSV field escaping (shared rules)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace schnelle_zeichen {

// base char -> (variant value -> commit count).
using UsageCounts =
    std::unordered_map<std::string, std::unordered_map<std::string, long long>>;

inline UsageCounts parseUsage(FILE *fp) {
    UsageCounts counts;
    std::string line;
    while (readConfigLine(fp, line)) {
        // Split into exactly three fields: base, variant, count. The fields
        // are TSV-escaped, so a raw '\t' is always a separator and the
        // two-tab split is unambiguous.
        const size_t t1 = line.find('\t');
        if (t1 == std::string::npos)
            continue;
        const size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos)
            continue;
        std::string base;
        std::string variant;
        if (!unescapeTsvField(line.substr(0, t1), base) ||
            !unescapeTsvField(line.substr(t1 + 1, t2 - t1 - 1), variant))
            continue;
        const std::string countStr = line.substr(t2 + 1);
        if (base.empty() || variant.empty() || countStr.empty())
            continue;
        char *end = nullptr;
        errno = 0;
        const long long n = std::strtoll(countStr.c_str(), &end, 10);
        // errno == ERANGE catches an out-of-range count that strtoll clamps to
        // LLONG_MAX; without it a garbage counter would be stored as ~9.2e18
        // instead of being skipped like any other malformed line.
        if (end == countStr.c_str() || *end != '\0' || n < 0 || errno == ERANGE)
            continue;
        counts[base][variant] = n;
    }
    return counts;
}

inline std::string serializeUsage(const UsageCounts &counts) {
    // Sort bases and variants so an unchanged table re-serializes
    // byte-identically instead of churning line order.
    std::vector<std::string> bases;
    bases.reserve(counts.size());
    for (const auto &kv : counts)
        bases.push_back(kv.first);
    std::sort(bases.begin(), bases.end());

    std::string out;
    for (const auto &base : bases) {
        const auto &variants = counts.at(base);
        std::vector<std::string> keys;
        keys.reserve(variants.size());
        for (const auto &kv : variants)
            keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        for (const auto &variant : keys) {
            out += escapeTsvField(base);
            out += '\t';
            out += escapeTsvField(variant);
            out += '\t';
            out += std::to_string(variants.at(variant));
            out += '\n';
        }
    }
    return out;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_USAGE_IO_H
