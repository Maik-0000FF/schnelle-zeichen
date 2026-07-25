// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_MERGE_MANIFEST_IO_H
#define SCHNELLE_ZEICHEN_CORE_MERGE_MANIFEST_IO_H

// Parse and serialize the single global merge manifest (merge.conf): the
// chosen base profile, the ordered appended source profiles (click order), and
// the per-base manual order overrides. Shared by the engine and the editor so
// both read the same format.
//
// Format (one directive per line):
//   base=<file>                          the base profile File ("" = no merge)
//   source=<file>                        one appended source File, in order
//   ~\t<base>\t<sourceRef>\t<value>      one order-override instance
//
// Every value field (the base=/source= file names, and the order line's
// base char, sourceRef and variant value) is stored with the shared TSV
// field escaping (escapeTsvField: \t \n \r \\ \#). The format is robust on
// its own: snippet variants containing tabs or newlines, a '#' base and
// even a file name with control characters survive the line- and
// tab-oriented format without leaning on caller-side validation (the
// isSafeProfileFile guard bars such names anyway, as its own defense). The
// order of override lines for a given base defines the arrangement. Lines
// starting with '#' are comments; empty and unrecognized lines skipped.

#include "mappings_io.h"     // readConfigLine + TSV field escaping
#include "profile_compose.h" // OrderOverride, Variant

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace schnelle_zeichen {

struct MergeManifest {
    std::string base;                 // base profile File; empty = no merge
    std::vector<std::string> sources; // appended source Files, click order
    OrderOverride order;              // per-base manual arrangement
};

namespace merge_manifest_detail {

// Split a line into fields on '\t'. Empty trailing fields are preserved so a
// caller can detect a malformed (too-short) line.
inline std::vector<std::string> splitTabs(const std::string &line) {
    std::vector<std::string> fields;
    std::string cur;
    for (char c : line) {
        if (c == '\t') {
            fields.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    fields.push_back(std::move(cur));
    return fields;
}

inline bool startsWith(const std::string &s, const char *prefix) {
    const std::string p(prefix);
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

} // namespace merge_manifest_detail

inline MergeManifest parseMergeManifest(FILE *fp) {
    using namespace merge_manifest_detail;
    MergeManifest manifest;
    std::string line;
    while (readConfigLine(fp, line)) {
        if (startsWith(line, "base=")) {
            std::string file;
            if (unescapeTsvField(line.substr(5), file))
                manifest.base = std::move(file);
        } else if (startsWith(line, "source=")) {
            std::string file;
            if (unescapeTsvField(line.substr(7), file) && !file.empty())
                manifest.sources.push_back(std::move(file));
        } else if (line[0] == '~') {
            // ~\t<base>\t<sourceRef>\t<value>
            auto fields = splitTabs(line);
            if (fields.size() < 4 || fields[0] != "~")
                continue; // not a well-formed order line
            // Re-join fields[3..] with '\t' before unescaping, so a raw-tab
            // value from a file written before the field escaping existed
            // still parses instead of being silently dropped.
            std::string rawValue = fields[3];
            for (size_t i = 4; i < fields.size(); ++i) {
                rawValue += '\t';
                rawValue += fields[i];
            }
            std::string base;
            std::string ref;
            std::string value;
            if (!unescapeTsvField(fields[1], base) ||
                !unescapeTsvField(fields[2], ref) ||
                !unescapeTsvField(rawValue, value))
                continue; // malformed escape: drop the line
            if (base.empty() || ref.empty())
                continue;
            manifest.order[base].push_back({std::move(value), std::move(ref)});
        }
    }
    return manifest;
}

inline std::string serializeMergeManifest(const MergeManifest &manifest) {
    std::string out;
    out += "base=";
    out += escapeTsvField(manifest.base);
    out += '\n';
    for (const auto &src : manifest.sources) {
        out += "source=";
        out += escapeTsvField(src);
        out += '\n';
    }
    // Sort the bases so an unchanged manifest re-serializes byte-identically
    // instead of churning line order (the map is unordered). Within a base the
    // instance order is the arrangement and is kept as-is.
    std::vector<std::string> bases;
    bases.reserve(manifest.order.size());
    for (const auto &kv : manifest.order)
        bases.push_back(kv.first);
    std::sort(bases.begin(), bases.end());
    for (const auto &base : bases) {
        for (const auto &inst : manifest.order.at(base)) {
            out += "~\t";
            out += escapeTsvField(base);
            out += '\t';
            out += escapeTsvField(inst.sourceRef);
            out += '\t';
            out += escapeTsvField(inst.value);
            out += '\n';
        }
    }
    return out;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_MERGE_MANIFEST_IO_H
