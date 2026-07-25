// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Core format-layer tests: the shared parsers/serializers the engine and the
// editor both consume. Focus: escaping round-trips (usage.conf, merge.conf,
// mapping outputs), the integer range guards, and the malformed-line rules
// (interior NUL, overlong, unknown escape) that must drop a line instead of
// misreading a prefix.

#include "core/ini_io.h"
#include "core/mappings_io.h"
#include "core/merge_manifest_io.h"
#include "core/usage_io.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace schnelle_zeichen;

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

namespace {

// Run a parser over an in-memory file. fmemopen is POSIX and the tests are
// Linux-only, like the product.
template <typename Parse>
auto parseString(const std::string &text, Parse p) {
    FILE *fp = fmemopen(const_cast<char *>(text.data()), text.size(), "r");
    auto result = p(fp);
    std::fclose(fp);
    return result;
}

// ------------------------------------------------------------ parseIniInt

// parseIniInt over a one-entry section, so each case reads as value -> int.
int iniIntFor(const std::string &value, int def) {
    IniSection s;
    s.name = "S";
    s.entries.push_back({"K", value});
    return parseIniInt(&s, "K", def);
}

void testParseIniIntRange() {
    const int def = -7;
    CHECK(iniIntFor("44", def) == 44);
    CHECK(iniIntFor("-44", def) == -44);
    CHECK(iniIntFor("2147483647", def) == 2147483647);
    CHECK(iniIntFor("-2147483648", def) == -2147483648);
    // One past the int range must keep the default, not wrap.
    CHECK(iniIntFor("2147483648", def) == def);
    CHECK(iniIntFor("-2147483649", def) == def);
    // 2^32 + 44 wrapped to 44 before the range guard (a fabricated keycode).
    CHECK(iniIntFor("4294967340", def) == def);
    // 2^32 wrapped to 0 and passed range checks.
    CHECK(iniIntFor("4294967296", def) == def);
    // Past even the long range (strtol clamps + ERANGE).
    CHECK(iniIntFor("99999999999999999999", def) == def);
    CHECK(iniIntFor("-99999999999999999999", def) == def);
    // Non-numeric tails and garbage keep the default (full-match rule).
    CHECK(iniIntFor("12abc", def) == def);
    CHECK(iniIntFor("abc", def) == def);
    CHECK(iniIntFor("", def) == def);
}

// ------------------------------------------------------- TSV field escaping

void testTsvFieldEscaping() {
    const std::vector<std::string> values = {
        "plain",  "with\ttab", "with\nnewline", "back\\slash",
        "#lead",  "trail\r",   "mix\t\n\\#\r",  "ä",
        "a,b,c ", "~",
    };
    for (const auto &v : values) {
        std::string back;
        CHECK(unescapeTsvField(escapeTsvField(v), back));
        CHECK(back == v);
    }
    // Escaped forms carry no raw separators and cannot open a comment line.
    CHECK(escapeTsvField("a\tb").find('\t') == std::string::npos);
    CHECK(escapeTsvField("a\nb").find('\n') == std::string::npos);
    CHECK(escapeTsvField("#x")[0] != '#');
    // Strictness: dangling backslash and unknown escapes are malformed.
    std::string out;
    CHECK(!unescapeTsvField("dangling\\", out));
    CHECK(!unescapeTsvField("bad\\q", out));
    // Pre-escaping plain values pass through unchanged.
    CHECK(unescapeTsvField("plain value", out) && out == "plain value");
}

// ------------------------------------------------------------ usage.conf

void testUsageRoundtrip() {
    UsageCounts counts;
    counts["a"]["\xc3\xa4"] = 3;
    counts["a"]["a\nb\tc"] = 7;        // snippet variant with newline + tab
    counts["#"]["x"] = 1;              // '#' base must not become a comment
    counts["\\"]["va\\lue"] = 2;       // backslashes both sides
    counts["s"]["long \r tail\r"] = 5; // CRs survive the CRLF trim
    const UsageCounts back = parseString(
        serializeUsage(counts), [](FILE *fp) { return parseUsage(fp); });
    CHECK(back == counts);
}

void testUsageMalformedLines() {
    // Old-format plain lines still parse; malformed ones drop without
    // poisoning their neighbors.
    const std::string text = "# comment\n"
                             "a\t\xc3\xa4\t3\n" // plain pre-escaping line
                             "b\tx\n"           // too few fields
                             "c\ty\t12junk\n"   // non-numeric count
                             "d\tz\t-1\n"       // negative count
                             "e\tw\t99999999999999999999\n" // ERANGE count
                             "f\tbad\\q\t1\n"               // unknown escape
                             "g\tok\t2\n";
    const UsageCounts counts =
        parseString(text, [](FILE *fp) { return parseUsage(fp); });
    CHECK(counts.size() == 2);
    CHECK(counts.at("a").at("\xc3\xa4") == 3);
    CHECK(counts.at("g").at("ok") == 2);
}

void testUsageInteriorNul() {
    // An interior NUL marks the whole line malformed; the fgets reader this
    // replaced would have parsed the truncated prefix as a valid counter.
    std::string text = "a\tvariant\t3\n";
    text[3] = '\0';
    text += "b\tok\t1\n";
    const UsageCounts counts =
        parseString(text, [](FILE *fp) { return parseUsage(fp); });
    CHECK(counts.size() == 1);
    CHECK(counts.at("b").at("ok") == 1);
}

// ------------------------------------------------------------- merge.conf

void testMergeManifestRoundtrip() {
    MergeManifest manifest;
    manifest.base = "mappings.txt";
    manifest.sources = {"profiles/emoji.txt", "profiles/math.txt"};
    manifest.order["a"].push_back({"\xc3\xa4", "profiles/emoji.txt"});
    manifest.order["a"].push_back({"line1\nline2", "profiles/math.txt"});
    manifest.order["#"].push_back({"tab\there", "profiles/emoji.txt"});
    const MergeManifest back =
        parseString(serializeMergeManifest(manifest),
                    [](FILE *fp) { return parseMergeManifest(fp); });
    CHECK(back.base == manifest.base);
    CHECK(back.sources == manifest.sources);
    CHECK(back.order.size() == 2);
    const auto &a = back.order.at("a");
    CHECK(a.size() == 2);
    CHECK(a[0].value == "\xc3\xa4" && a[0].sourceRef == "profiles/emoji.txt");
    CHECK(a[1].value == "line1\nline2" &&
          a[1].sourceRef == "profiles/math.txt");
    const auto &hash = back.order.at("#");
    CHECK(hash.size() == 1 && hash[0].value == "tab\there");
}

void testMergeManifestLegacyRawTab() {
    // A raw-tab value from a file written before the field escaping existed
    // still parses via the rejoin path.
    const std::string text = "base=mappings.txt\n"
                             "source=profiles/emoji.txt\n"
                             "~\ta\tprofiles/emoji.txt\traw\ttab\n";
    const MergeManifest m =
        parseString(text, [](FILE *fp) { return parseMergeManifest(fp); });
    CHECK(m.order.at("a").size() == 1);
    CHECK(m.order.at("a")[0].value == "raw\ttab");
}

void testMergeManifestMalformed() {
    const std::string text = "base=mappings.txt\n"
                             "source=\n"           // empty source: skipped
                             "~\ta\n"              // too few fields
                             "~\t\tref\tvalue\n"   // empty base
                             "~\ta\t\tvalue\n"     // empty ref
                             "~\ta\tref\tbad\\q\n" // unknown escape
                             "~\tb\tref\tok\n";
    const MergeManifest m =
        parseString(text, [](FILE *fp) { return parseMergeManifest(fp); });
    CHECK(m.sources.empty());
    CHECK(m.order.size() == 1);
    CHECK(m.order.at("b")[0].value == "ok");
}

// -------------------------------------------------------- mapping outputs

void testOutputsRoundtrip() {
    const std::vector<std::vector<std::string>> lists = {
        {"\xc3\xa4"},
        {"a", "b", "c"},
        {"with,comma", "with\nnewline", "with\ttab", "back\\slash"},
        {"multi\nline\nsnippet, with comma"},
    };
    for (const auto &variants : lists) {
        CHECK(splitOutputs(joinOutputs(variants)) == variants);
    }
    // Invalid escapes mark the whole output invalid (empty list).
    CHECK(splitOutputs("a\\q").empty());
    CHECK(splitOutputs("dangling\\").empty());
    // Empty segments are skipped, all-separator input yields no outputs.
    CHECK(splitOutputs("a,,b") == (std::vector<std::string>{"a", "b"}));
    CHECK(splitOutputs(",").empty());
}

} // namespace

int main() {
    testParseIniIntRange();
    testTsvFieldEscaping();
    testUsageRoundtrip();
    testUsageMalformedLines();
    testUsageInteriorNul();
    testMergeManifestRoundtrip();
    testMergeManifestLegacyRawTab();
    testMergeManifestMalformed();
    testOutputsRoundtrip();
    if (failures == 0) {
        std::printf("format_io_test: all checks passed\n");
        return 0;
    }
    std::printf("format_io_test: %d check(s) failed\n", failures);
    return 1;
}
