// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Core format-layer tests: the shared parsers/serializers the engine and the
// editor both consume. Focus: escaping round-trips (usage.conf, merge.conf,
// mapping outputs), the integer range guards, and the malformed-line rules
// (interior NUL, overlong, unknown escape) that must drop a line instead of
// misreading a prefix.

#include "check.h"
#include "core/ini_io.h"
#include "core/mappings_io.h"
#include "core/merge_manifest_io.h"
#include "core/profile_paths.h"
#include "core/profiles_io.h"
#include "core/usage_io.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace schnelle_zeichen;

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

void testMergeManifestHostileFilenames() {
    // File names with tabs/newlines are barred by isSafeProfileFile, but the
    // format must survive them on its own: all fields are escaped, so such a
    // name round-trips instead of shifting the tab split or tearing lines.
    MergeManifest manifest;
    manifest.base = "profiles/we\tird.txt";
    manifest.sources = {"profiles/multi\nline.txt"};
    manifest.order["a"].push_back({"value", "profiles/we\tird.txt"});
    const MergeManifest back =
        parseString(serializeMergeManifest(manifest),
                    [](FILE *fp) { return parseMergeManifest(fp); });
    CHECK(back.base == manifest.base);
    CHECK(back.sources == manifest.sources);
    CHECK(back.order.at("a").size() == 1);
    CHECK(back.order.at("a")[0].sourceRef == "profiles/we\tird.txt");
}

// ---------------------------------------------------------- profile paths

void testIsSafeProfileFile() {
    CHECK(isSafeProfileFile("mappings.txt"));
    CHECK(isSafeProfileFile("profiles/emoji.txt"));
    // Traversal, nesting, absolute paths, wrong prefix.
    CHECK(!isSafeProfileFile("profiles/../settings.conf"));
    CHECK(!isSafeProfileFile("profiles/a/b.txt"));
    CHECK(!isSafeProfileFile("/etc/passwd"));
    CHECK(!isSafeProfileFile("other/x.txt"));
    CHECK(!isSafeProfileFile("profiles/"));
    CHECK(!isSafeProfileFile(""));
    // Control characters: legal on Linux, but they would interfere with the
    // line- and tab-oriented config formats embedding the File field.
    CHECK(!isSafeProfileFile("profiles/we\tird.txt"));
    CHECK(!isSafeProfileFile("profiles/multi\nline.txt"));
    CHECK(!isSafeProfileFile("profiles/cr\r.txt"));
    CHECK(!isSafeProfileFile("profiles/esc\x1b.txt"));
    CHECK(!isSafeProfileFile("profiles/del\x7f.txt"));
    CHECK(!isSafeProfileFile("mappings.txt\n"));
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

// ---------------------------------------------------------- readLimitedLine

void testReadLimitedLine() {
    // Two lines, no trailing newline on the last: both read without the
    // terminator, then the stream is exhausted.
    const std::string text = "first\nsecond";
    FILE *fp = fmemopen(const_cast<char *>(text.data()), text.size(), "r");
    std::string line;
    bool overlong = true;
    CHECK(readLimitedLine(fp, line, overlong) && line == "first" && !overlong);
    CHECK(readLimitedLine(fp, line, overlong) && line == "second" && !overlong);
    CHECK(!readLimitedLine(fp, line, overlong)); // EOF, nothing read
    std::fclose(fp);
}

void testReadLimitedLineInteriorNul() {
    // Byte-accurate: an interior NUL stays in the line (the fgets reader this
    // replaced understated the length here, truncating at the NUL).
    const std::string text("a\0b\nnext\n", 9); // counted ctor keeps the NUL
    FILE *fp = fmemopen(const_cast<char *>(text.data()), text.size(), "r");
    std::string line;
    bool overlong = false;
    CHECK(readLimitedLine(fp, line, overlong));
    CHECK(line.size() == 3 && line[1] == '\0');
    CHECK(readLimitedLine(fp, line, overlong) && line == "next");
    std::fclose(fp);
}

void testReadLimitedLineOverlong() {
    // A line past the cap sets `overlong`, is drained to its newline, and the
    // following line still reads cleanly (no tail misread as new lines).
    std::string text(kMaxMappingLineBytes + 10, 'x');
    text += "\nok\n";
    FILE *fp = fmemopen(const_cast<char *>(text.data()), text.size(), "r");
    std::string line;
    bool overlong = false;
    CHECK(readLimitedLine(fp, line, overlong));
    CHECK(overlong);
    CHECK(line.size() == kMaxMappingLineBytes);
    CHECK(readLimitedLine(fp, line, overlong) && line == "ok" && !overlong);
    std::fclose(fp);
}

// ------------------------------------------------------------- parseMappings

void testParseMappingsPlain() {
    const std::string text = "# a header comment\n"
                             "a=\xc3\xa4\n" // ascii input -> umlaut
                             "\xc3\xa4=x\n" // multi-byte input (ä)
                             "==eq\n"       // '=' is a valid input char
                             "\r\n"         // blank (CR only) skipped
                             "b=one,two\n"  // raw variant list kept verbatim
                             "c=\r\n";      // empty output: dropped
    const auto m =
        parseString(text, [](FILE *fp) { return parseMappings(fp); });
    CHECK(m.size() == 4);
    CHECK(m[0].input == "a" && m[0].output == "\xc3\xa4");
    CHECK(m[1].input == "\xc3\xa4" && m[1].output == "x");
    CHECK(m[2].input == "=" && m[2].output == "eq");
    CHECK(m[3].input == "b" && m[3].output == "one,two");
}

void testParseMappingsEscapedInput() {
    // A leading backslash escapes an input the plain parse would misread:
    // "\#=x" maps '#', "\\=y" maps '\'. A bare "\=z" still reads as '\'.
    const std::string text = "\\#=x\n"
                             "\\\\=y\n"
                             "\\=z\n";
    const auto m =
        parseString(text, [](FILE *fp) { return parseMappings(fp); });
    CHECK(m.size() == 3);
    CHECK(m[0].input == "#" && m[0].output == "x");
    CHECK(m[1].input == "\\" && m[1].output == "y");
    CHECK(m[2].input == "\\" && m[2].output == "z");
}

void testParseMappingsMalformed() {
    // Overlong, interior NUL and invalid UTF-8 inputs are all dropped without
    // poisoning the surrounding lines.
    std::string text = "a=\xc3\xa4\n";
    text += std::string(kMaxMappingLineBytes + 5, 'q') + "=big\n"; // overlong
    text += "b=nul\n";
    // Interior NUL in the output: the line keeps a valid '=', so it is dropped
    // purely by the NUL rule, not for a missing separator.
    text[text.size() - 4] = '\0'; // -> "b=\0ul"
    text += "\xff=bad\n";         // invalid lead byte
    text += "z=ok\n";
    const auto m =
        parseString(text, [](FILE *fp) { return parseMappings(fp); });
    CHECK(m.size() == 2);
    CHECK(m[0].input == "a" && m[0].output == "\xc3\xa4");
    CHECK(m[1].input == "z" && m[1].output == "ok");
}

// --------------------------------------------------------------- profiles.conf

void testProfilesRoundtrip() {
    ProfilesData data;
    data.active = "Emoji";
    data.cycleNext = "Control+Alt+Right";
    data.cyclePrev = "Control+Alt+Left";
    data.entries.push_back({"Standard", "mappings.txt", "", false});
    data.entries.push_back(
        {"My Emoji", "profiles/emoji.txt", "Control+Alt+2", true}); // spaces
    const ProfilesData back = parseString(
        serializeProfiles(data), [](FILE *fp) { return parseProfiles(fp); });
    CHECK(back.active == data.active);
    CHECK(back.cycleNext == data.cycleNext);
    CHECK(back.cyclePrev == data.cyclePrev);
    CHECK(back.entries.size() == 2);
    CHECK(back.entries[0].name == "Standard" &&
          back.entries[0].file == "mappings.txt");
    CHECK(back.entries[1].name == "My Emoji" &&
          back.entries[1].file == "profiles/emoji.txt" &&
          back.entries[1].selectKey == "Control+Alt+2" &&
          back.entries[1].favorite);
}

void testProfilesDedupAndValidation() {
    // Duplicate name (ASCII-case-insensitive) and duplicate file drop the
    // later entry; an unsafe or empty File drops the entry entirely.
    const std::string text = "Active=Std\n"
                             "[Profiles/0]\n"
                             "Name=Std\nFile=mappings.txt\n"
                             "[Profiles/1]\n"
                             "Name=STD\nFile=profiles/a.txt\n" // dup name
                             "[Profiles/2]\n"
                             "Name=Other\nFile=mappings.txt\n" // dup file
                             "[Profiles/3]\n"
                             "Name=Bad\nFile=profiles/../x.txt\n" // unsafe
                             "[Profiles/4]\n"
                             "Name=\nFile=profiles/b.txt\n" // empty name
                             "[Profiles/5]\n"
                             "Name=Keep\nFile=profiles/keep.txt\n";
    const ProfilesData d =
        parseString(text, [](FILE *fp) { return parseProfiles(fp); });
    CHECK(d.entries.size() == 2);
    CHECK(d.entries[0].name == "Std");
    CHECK(d.entries[1].name == "Keep");
}

void testProfileFallbacks() {
    // Empty list: seed the protected Standard profile pointing at mappings.txt.
    ProfilesData empty;
    empty.active = "ghost";
    applyProfileFallbacks(empty);
    CHECK(empty.entries.size() == 1);
    CHECK(empty.entries[0].name == kStandardProfile &&
          empty.entries[0].file == kMappingsFile);
    CHECK(empty.active == kStandardProfile);
    // Active naming no entry falls back to the first entry.
    ProfilesData dangling;
    dangling.entries.push_back({"First", "mappings.txt", "", false});
    dangling.entries.push_back({"Second", "profiles/s.txt", "", false});
    dangling.active = "nope";
    applyProfileFallbacks(dangling);
    CHECK(dangling.active == "First");
    // A valid Active is left untouched.
    dangling.active = "Second";
    applyProfileFallbacks(dangling);
    CHECK(dangling.active == "Second");
}

void testIsStandardProfile() {
    CHECK(isStandardProfile("mappings.txt"));
    CHECK(!isStandardProfile("profiles/emoji.txt"));
    CHECK(!isStandardProfile(""));
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
    testMergeManifestHostileFilenames();
    testIsSafeProfileFile();
    testOutputsRoundtrip();
    testReadLimitedLine();
    testReadLimitedLineInteriorNul();
    testReadLimitedLineOverlong();
    testParseMappingsPlain();
    testParseMappingsEscapedInput();
    testParseMappingsMalformed();
    testProfilesRoundtrip();
    testProfilesDedupAndValidation();
    testProfileFallbacks();
    testIsStandardProfile();
    if (failures == 0) {
        std::printf("format_io_test: all checks passed\n");
        return 0;
    }
    std::printf("format_io_test: %d check(s) failed\n", failures);
    return 1;
}
