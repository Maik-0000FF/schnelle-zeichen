// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_MAPPINGS_IO_H
#define SCHNELLE_ZEICHEN_CORE_MAPPINGS_IO_H

// Shared mapping file parser and default mappings.
// Used by the engine (std::string) and the config editor (QString). Keeping
// the format definition in one place prevents the two from diverging.
//
// Output format v2 (deliberate divergence from schnelle-umlaute): variants
// are comma-separated with backslash escapes (see splitOutputs) instead of
// the legacy double-comma escape. The escapes make every string expressible
// (the legacy scheme could not express variants starting with a comma) and
// add multi-line snippet support via \n while the file stays one logical
// mapping per line.

#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace schnelle_zeichen {

// A single input->output mapping as raw strings.
// "output" may contain comma-separated cycling variants (parsed by the engine).
struct RawMapping {
    std::string input;
    std::string output;
};

// Default mappings: German umlauts (ä ö ü) and Eszett (ß), both cases.
inline std::vector<RawMapping> defaultMappings() {
    return {
        {"a", "\xc3\xa4"}, {"o", "\xc3\xb6"}, {"u", "\xc3\xbc"},
        {"s", "\xc3\x9f"}, {"A", "\xc3\x84"}, {"O", "\xc3\x96"},
        {"U", "\xc3\x9c"},
    };
}

// Byte length of the first UTF-8 character based on its leading byte.
// Returns 0 for invalid lead bytes (continuation bytes or 0xFE/0xFF).
inline size_t utf8CharLen(unsigned char lead) {
    if (lead < 0x80)
        return 1;
    if ((lead & 0xE0) == 0xC0)
        return 2;
    if ((lead & 0xF0) == 0xE0)
        return 3;
    if ((lead & 0xF8) == 0xF0)
        return 4;
    return 0;
}

// Byte length of a valid UTF-8 character at the start of [s, s+len).
// Returns 0 if the lead byte is invalid, if the buffer is too short for
// the indicated length, or if any continuation byte is not in 0x80-0xBF.
// A hand-edited mappings.txt with a wrong encoding could otherwise slip
// invalid UTF-8 through the parser, since utf8CharLen inspects only the
// lead byte. No overlong-encoding check: realistic editors don't produce
// them, and the cost outweighs the benefit for a user-owned config file.
inline size_t utf8FirstCharBytes(const char *s, size_t len) {
    if (len == 0)
        return 0;
    size_t n = utf8CharLen(static_cast<unsigned char>(s[0]));
    if (n == 0 || n > len)
        return 0;
    for (size_t i = 1; i < n; ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            return 0;
    }
    return n;
}

// Maximum bytes in one physical mappings line. Multi-line snippets are stored
// escaped on a single line, so the limit is generous; anything beyond it is
// malformed and the whole line is dropped, which also bounds the allocation a
// corrupt file can force.
inline constexpr size_t kMaxMappingLineBytes = size_t{64} * 1024;

// Read one physical line (terminated by '\n' or EOF) byte-accurately into
// `line`, without the terminator. Returns false when the stream is exhausted
// and nothing was read. A line exceeding kMaxMappingLineBytes sets `overlong`
// and is drained to its end, so the caller can drop it without misreading the
// tail as new lines. Byte-accurate on purpose: the fgets-based reader this
// replaces understated the length of lines with an interior NUL, letting a
// NUL-truncated prefix parse as a complete mapping.
inline bool readLimitedLine(FILE *fp, std::string &line, bool &overlong) {
    line.clear();
    overlong = false;
    int c = std::fgetc(fp);
    if (c == EOF)
        return false;
    for (; c != EOF && c != '\n'; c = std::fgetc(fp)) {
        if (line.size() >= kMaxMappingLineBytes) {
            overlong = true;
            while ((c = std::fgetc(fp)) != EOF && c != '\n') {
            }
            return true;
        }
        line.push_back(static_cast<char>(c));
    }
    return true;
}

// Parse mappings from an open FILE*.
// Format: one UTF-8 character + '=' + output, one mapping per line.
// The input character may be ASCII (1 byte) or multi-byte UTF-8
// (e.g. é, ñ on native keyboard layouts). '=' itself is a valid input
// since the delimiter is always the '=' after the first UTF-8 character.
// The output field holds comma-separated cycling variants with backslash
// escapes (see splitOutputs). Lines starting with '#' are comments, empty
// lines are skipped, and malformed lines (overlong, interior NUL) are
// dropped entirely.
inline std::vector<RawMapping> parseMappings(FILE *fp) {
    std::vector<RawMapping> entries;
    std::string line;
    bool overlong = false;
    while (readLimitedLine(fp, line, overlong)) {
        if (overlong)
            continue;
        // Trim trailing carriage returns (CRLF files).
        while (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty())
            continue;
        // An interior NUL cannot occur in a well-formed UTF-8 mappings file;
        // treat the whole line as malformed instead of storing NUL bytes.
        if (line.find('\0') != std::string::npos)
            continue;
        // A leading backslash escapes an input key that the plain parse would
        // otherwise misread: "\#=x" maps '#' (which starts a comment line when
        // unescaped) and "\\=x" maps '\' itself. The escaped byte is always
        // ASCII ('#' or '\') followed by '='. Any other leading backslash falls
        // through to the plain parse, so a bare "\=x" still reads as '\', and
        // comments and normal lines stay untouched.
        if (line.size() >= 3 && line[0] == '\\' &&
            (line[1] == '#' || line[1] == '\\') && line[2] == '=') {
            std::string input(1, line[1]);
            std::string output = line.substr(3);
            if (!output.empty()) {
                entries.push_back({std::move(input), std::move(output)});
            }
            continue;
        }
        if (line[0] == '#') // comment
            continue;
        size_t inputLen = utf8FirstCharBytes(line.data(), line.size());
        if (inputLen == 0 || line.size() <= inputLen || line[inputLen] != '=') {
            continue;
        }
        auto input = line.substr(0, inputLen);
        auto output = line.substr(inputLen + 1);
        if (!output.empty()) {
            entries.push_back({std::move(input), std::move(output)});
        }
    }
    return entries;
}

// Split a raw output string into cycling variants (format v2).
// Comma separates variants: "a,b" -> ["a", "b"]. Backslash escapes make
// every string expressible:
//   \,  literal comma          \n  newline (multi-line snippets)
//   \t  tab                    \\  backslash
// Empty segments are skipped: "a,,b" -> ["a", "b"]. A lone "," (or any
// all-separator string) yields an empty list, which callers treat as "no
// valid outputs". A dangling backslash or an unknown escape marks the whole
// output invalid and also yields the empty list, so the caller skips the
// mapping with a warning. Strict on purpose: unknown escapes stay reserved
// for future format extensions instead of being silently reinterpreted.
// Lives here, next to the parser, so the engine and the editor's validation
// agree on what the output field means.
inline std::vector<std::string> splitOutputs(const std::string &output) {
    std::vector<std::string> outputs;
    std::string current;
    for (size_t i = 0; i < output.length(); ++i) {
        const char c = output[i];
        if (c == '\\') {
            if (i + 1 >= output.length())
                return {};
            ++i;
            switch (output[i]) {
            case ',':
                current += ',';
                break;
            case 'n':
                current += '\n';
                break;
            case 't':
                current += '\t';
                break;
            case '\\':
                current += '\\';
                break;
            default:
                return {};
            }
        } else if (c == ',') {
            if (!current.empty()) {
                outputs.push_back(std::move(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        outputs.push_back(std::move(current));
    }
    return outputs;
}

// Rejoin cycling variants into the stored escaped form, the inverse of
// splitOutputs: comma, newline, tab and backslash inside a variant are
// escaped, the variants are joined with single commas. Empty variants are
// skipped so the result round-trips through splitOutputs unchanged.
inline std::string joinOutputs(const std::vector<std::string> &outputs) {
    std::string result;
    bool first = true;
    for (const auto &out : outputs) {
        if (out.empty())
            continue;
        if (!first)
            result += ',';
        first = false;
        for (char c : out) {
            switch (c) {
            case ',':
                result += "\\,";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\t':
                result += "\\t";
                break;
            case '\\':
                result += "\\\\";
                break;
            default:
                result += c;
            }
        }
    }
    return result;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_MAPPINGS_IO_H
