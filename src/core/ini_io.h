#ifndef SCHNELLE_ZEICHEN_CORE_INI_IO_H
#define SCHNELLE_ZEICHEN_CORE_INI_IO_H

// Framework-free reader for the fcitx-style INI files inherited from
// schnelle-umlaute (settings.conf, profiles.conf): [Section] and
// [Section/Sub] headers, key=value pairs, '#' comments, and fcitx's value
// quoting (a value containing whitespace, quotes or backslashes is stored
// quoted with backslash escapes). The escape/unescape pair mirrors fcitx
// stringutils escapeForValue/unescapeForValue exactly as the legacy editor
// reimplemented them, so files written by either side round-trip.
// Cold path only (config load); nothing here runs per keystroke.

#include "mappings_io.h" // readLimitedLine (shared byte-accurate reader)

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace schnelle_zeichen {

struct IniEntry {
    std::string key;
    std::string value;
};

// One section in file order; the unnamed section ("") holds the keys before
// the first [header].
struct IniSection {
    std::string name;
    std::vector<IniEntry> entries;
};

using IniDocument = std::vector<IniSection>;

// Trim the whitespace the legacy readers trimmed: spaces, tabs and stray
// carriage returns from CRLF files.
inline std::string_view trimIni(std::string_view s) {
    while (!s.empty() &&
           (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// Mirror of fcitx stringutils::escapeForValue: quote the value when it
// contains whitespace, a quote or a backslash, and backslash-escape the
// special characters.
inline std::string escapeIniValue(const std::string &s) {
    bool needQuote = false;
    for (const char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
            c == '\v' || c == '"' || c == '\\') {
            needQuote = true;
            break;
        }
    }
    std::string out;
    if (needQuote) {
        out += '"';
    }
    for (const char c : s) {
        char esc = 0;
        switch (c) {
        case '\\':
            esc = '\\';
            break;
        case '"':
            esc = '"';
            break;
        case '\n':
            esc = 'n';
            break;
        case '\f':
            esc = 'f';
            break;
        case '\r':
            esc = 'r';
            break;
        case '\t':
            esc = 't';
            break;
        case '\v':
            esc = 'v';
            break;
        default:
            break;
        }
        if (esc != 0) {
            out += '\\';
            out += esc;
        } else {
            out += c;
        }
    }
    if (needQuote) {
        out += '"';
    }
    return out;
}

// Mirror of fcitx stringutils::unescapeForValue: a value wrapped in quotes
// is unescaped; anything else is returned verbatim. A malformed quoted value
// (not closed exactly at the end) is returned raw, best-effort.
inline std::string unescapeIniValue(const std::string &s) {
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') {
        return s;
    }
    std::string result;
    bool escape = false;
    bool closed = false;
    size_t i = 1;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (!escape) {
            if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                ++i;
                closed = true;
                break;
            } else {
                result += c;
            }
        } else {
            switch (c) {
            case '\\':
                result += '\\';
                break;
            case '"':
                result += '"';
                break;
            case 'n':
                result += '\n';
                break;
            case 'f':
                result += '\f';
                break;
            case 'r':
                result += '\r';
                break;
            case 't':
                result += '\t';
                break;
            case 'v':
                result += '\v';
                break;
            default:
                result += c; // unknown escape: keep literal
                break;
            }
            escape = false;
        }
    }
    if (closed && i == s.size()) {
        return result;
    }
    return s;
}

// Parse an INI stream into sections in file order. Empty lines and '#'
// comments are skipped, values are unescaped, malformed lines (no '=',
// overlong, interior NUL) are dropped.
inline IniDocument parseIni(FILE *fp) {
    IniDocument doc;
    doc.push_back({"", {}});
    std::string line;
    bool overlong = false;
    while (readLimitedLine(fp, line, overlong)) {
        if (overlong) {
            continue;
        }
        if (line.find('\0') != std::string::npos) {
            continue;
        }
        const std::string_view t = trimIni(line);
        if (t.empty() || t.front() == '#') {
            continue;
        }
        if (t.front() == '[' && t.back() == ']') {
            doc.push_back({std::string(t.substr(1, t.size() - 2)), {}});
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        std::string key(trimIni(t.substr(0, eq)));
        std::string value =
            unescapeIniValue(std::string(trimIni(t.substr(eq + 1))));
        doc.back().entries.push_back({std::move(key), std::move(value)});
    }
    return doc;
}

inline const IniSection *findIniSection(const IniDocument &doc,
                                        std::string_view name) {
    for (const auto &section : doc) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

// Last occurrence wins, matching the later-assignment-overwrites reading of
// the legacy loaders.
inline const std::string *findIniValue(const IniSection &section,
                                       std::string_view key) {
    const std::string *found = nullptr;
    for (const auto &entry : section.entries) {
        if (entry.key == key) {
            found = &entry.value;
        }
    }
    return found;
}

inline std::string parseIniString(const IniSection *section,
                                  std::string_view key,
                                  const std::string &def = "") {
    if (section == nullptr) {
        return def;
    }
    const std::string *v = findIniValue(*section, key);
    return v != nullptr ? *v : def;
}

// "True"/"1" -> true, "False"/"0" -> false (case-insensitive); anything
// else keeps the default (the fcitx unmarshal-keeps-default semantics).
// For valid values this matches both legacy readers; their union is
// accepted (SettingsModel read only "True", ProfileListModel "True"/"1").
inline bool parseIniBool(const IniSection *section, std::string_view key,
                         bool def) {
    if (section == nullptr) {
        return def;
    }
    const std::string *v = findIniValue(*section, key);
    if (v == nullptr) {
        return def;
    }
    std::string lower;
    lower.reserve(v->size());
    for (const char c : *v) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "true" || lower == "1") {
        return true;
    }
    if (lower == "false" || lower == "0") {
        return false;
    }
    return def;
}

// Full-match decimal integer; anything else keeps the default.
inline int parseIniInt(const IniSection *section, std::string_view key,
                       int def) {
    if (section == nullptr) {
        return def;
    }
    const std::string *v = findIniValue(*section, key);
    if (v == nullptr || v->empty()) {
        return def;
    }
    char *end = nullptr;
    const long n = std::strtol(v->c_str(), &end, 10);
    if (end == v->c_str() || *end != '\0') {
        return def;
    }
    return static_cast<int>(n);
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_INI_IO_H
