// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Theme-id sync guard: themes.h kThemeIds (the C++ validator's accepted list)
// and Palettes.qml (the shared palette module) must define the exact same set
// of theme ids. They live in different languages with no shared source, so a
// theme added on one side only would drift silently. This parses the QML and
// compares kThemeIds against all three id lists it carries: the `all` palette
// definitions, the `ids` picker array, and the `labels` display-name map.

#include "check.h"
#include "core/themes.h"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace schnelle_zeichen;

namespace {

std::string readFile(const char *path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::set<std::string> expectedIds() {
    std::set<std::string> s;
    for (const auto id : kThemeIds) {
        s.emplace(id);
    }
    return s;
}

std::set<std::string> matchAll(const std::string &text, const std::regex &re) {
    std::set<std::string> s;
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end;
         ++it) {
        s.insert((*it)[1].str());
    }
    return s;
}

// The substring from `open` up to the next `close`, so an id-extracting regex
// only sees the intended block instead of the whole file.
std::string section(const std::string &qml, const std::string &open,
                    const std::string &close) {
    const size_t a = qml.find(open);
    if (a == std::string::npos) {
        return "";
    }
    const size_t b = qml.find(close, a + open.size());
    return b == std::string::npos ? "" : qml.substr(a, b - a);
}

// Top-level palette definitions in `all`: a quoted key followed by ": {".
// Scoped to the `all` block (which alone closes with "})", nested palettes
// close with "},"), symmetric with idsArray/labelKeys. Nested palette
// properties use unquoted keys anyway, so only the 14 palettes match.
std::set<std::string> paletteKeys(const std::string &qml) {
    return matchAll(section(qml, "property var all: ({", "})"),
                    std::regex(R"rx("([a-z0-9-]+)"\s*:\s*\{)rx"));
}

// The flat `ids: [ "a", "b", ... ]` picker array.
std::set<std::string> idsArray(const std::string &qml) {
    return matchAll(section(qml, "property var ids: [", "]"),
                    std::regex(R"rx("([a-z0-9-]+)")rx"));
}

// The `labels: ({ "id": "Name", ... })` map keys (values are Title-cased with
// spaces, so the lowercase-id key regex never matches a display name).
std::set<std::string> labelKeys(const std::string &qml) {
    return matchAll(section(qml, "property var labels: ({", "})"),
                    std::regex(R"rx("([a-z0-9-]+)"\s*:)rx"));
}

} // namespace

int main() {
    // The regex build and file read can throw; keep every exception inside
    // main (nothing must escape it) and report it as a failure.
    try {
        const std::string qml = readFile(PALETTES_QML_PATH);
        if (qml.empty()) {
            std::printf("theme_id_sync_test: cannot read %s\n",
                        PALETTES_QML_PATH);
            return 1;
        }
        const std::set<std::string> expected = expectedIds();
        // No duplicate slipped into the C++ list (set collapses dupes).
        CHECK(expected.size() == kThemeIds.size());
        // The QML palette definitions, picker array and label map each match
        // the C++ list exactly.
        CHECK(paletteKeys(qml) == expected);
        CHECK(idsArray(qml) == expected);
        CHECK(labelKeys(qml) == expected);
    } catch (...) {
        std::printf("theme_id_sync_test: unexpected exception\n");
        return 1;
    }

    if (failures == 0) {
        std::printf("theme_id_sync_test: all checks passed\n");
        return 0;
    }
    std::printf("theme_id_sync_test: %d check(s) failed\n", failures);
    return 1;
}
