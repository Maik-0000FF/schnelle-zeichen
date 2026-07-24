// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_CORE_ENGINE_CONFIG_H
#define SCHNELLE_ZEICHEN_CORE_ENGINE_CONFIG_H

// Engine configuration as a plain struct, decoupled from fcitx-config: the
// same groups, key names, value spellings and defaults as the legacy
// SchnelleUmlauteConfig (the on-disk contract with the editor), parsed from
// the fcitx-style INI via ini_io.h.
//
// CONTRACT: the section and key literals below are the on-disk format the
// future editor port writes; keep both sides in sync when changing any.
//
// Deliberate deviations from the legacy config.h, by name:
// - fcitx-configtool plumbing (IntConstrainWithStep dumpDescription,
//   PlaceholderAnnotation, ExternalOption) has no equivalent; the numeric
//   bounds live on as load-time validation (out-of-range keeps the
//   default, the fcitx constraint semantics).
// - Editor-only keys ([Theme], Overlay/CaretTheme, top-level Mappings) and
//   pre-1.2 migration keys (Position=, AtCursor=) are ignored:
//   schnelle-zeichen starts with a fresh config root (no-migration
//   decision).
// - Hardening adopted from the editor's reader: custom-leader keycodes are
//   validated with isUsableKeyCode and fall back to kNoKeyCode, never
//   fabricating a leader from a malformed value.
// - kDeferredCommitDelayMs (the XIM commit-ordering wait) is NOT ported;
//   the raw backend serializes commits itself, so the constant belongs to
//   the fcitx backend where the ordering problem exists.

#include "hand_classifier.h" // kNoKeyCode, isUsableKeyCode
#include "ini_io.h"

#include <cstdio>
#include <string>
#include <vector>

namespace schnelle_zeichen {

// Delay slider bounds (ms), from the legacy config.
inline constexpr int kDelayMin = 50;
inline constexpr int kDelayMax = 2000;
inline constexpr int kDelayStep = 10;
// Minimum-hold lower bound (ms). 0 = no dead window, the historic default
// and the schnelle-umlaute hallmark: the leader commits instantly.
inline constexpr int kMinHoldMin = 0;
// Upper bound for numeric indices in the app-filter list sections, so a
// corrupt file cannot force an unbounded allocation.
inline constexpr long kMaxFilterListIndex = 4096;

enum class AppFilterMode { Disabled, Blacklist, Whitelist };
enum class OverlayPlacement { Grid, MouseCursor, TextCaret };
enum class OverlayRow { Top, Center, Bottom };
enum class OverlayColumn { Col1, Col2, Col3, Col4, Col5, Col6, Col7 };

// Each case defines an accent window [min, max] in milliseconds. lowercase/
// uppercase are the upper bound (latest moment a leader still triggers) and
// keep their historic key names; the Min values are the lower bound (dead
// window), default 0 = none. min is meant to stay below max; a hand-edited
// config with min >= max is degenerate and the engine ignores such a lower
// bound (mirrored from legacy getEffectiveMinHold, engine-side).
struct DelayConfig {
    int lowercase = 400;
    int uppercase = 700;
    int lowercaseMin = 0;
    int uppercaseMin = 0;
    // Opt-in extension (owner-approved), default off = exact legacy window:
    // no upper bound, the window stays open while the key is held (the
    // macOS/Quick-Accent popup feel).
    bool unlimited = false;
};

// A custom leader is a physical key: the keycode is what the engine matches
// and hand-classifies; the character is only shown in the UI and checked
// against the mappings. A leader whose keycode is kNoKeyCode is inactive.
struct CustomLeaderConfig {
    bool enabled = false;
    std::string key;
    int keyCode = kNoKeyCode;
    bool reverse = false;
    bool key2Enabled = false;
    std::string key2;
    int key2Code = kNoKeyCode;
    bool key2Reverse = false;
};

// Per-leader cycle direction: enable decides whether the key is a leader at
// all, reverse decides whether it steps backward. Default: Space is the one
// active leader, everything steps forward.
struct LeaderConfig {
    bool space = true;
    bool spaceReverse = false;
    bool left = false;
    bool leftReverse = false;
    bool right = false;
    bool rightReverse = false;
    bool up = false;
    bool upReverse = false;
    bool down = false;
    bool downReverse = false;
    bool alt = false;
    bool altReverse = false;
    bool altGr = false;
    bool altGrReverse = false;
    CustomLeaderConfig custom;
};

struct AppFilterConfig {
    AppFilterMode mode = AppFilterMode::Disabled;
    std::vector<std::string> blacklist;
    std::vector<std::string> whitelist;
};

struct OverlayConfig {
    bool enabled = false;
    bool showOnTrigger = false;
    OverlayPlacement placement = OverlayPlacement::Grid;
    bool progressBar = false;
    OverlayRow row = OverlayRow::Top;
    OverlayColumn column = OverlayColumn::Col4;
};

struct BehaviorConfig {
    bool sortByFrequency = false;
    // Opt-in extension (owner-approved, spike-validated), default off =
    // exact legacy behavior: holding a mapped key past autoSelectMs
    // pre-selects the first variant without a leader; the release commits.
    bool autoSelect = false;
    int autoSelectMs = 500;
    // Named deviation from legacy, owner-decided: a HELD leader no longer
    // steps per auto-repeat by default (each deliberate press steps once,
    // no accidental overshoot). True restores the legacy hold-to-cycle.
    bool leaderAutoRepeat = false;
    // Portable combo string toggling the runtime pause (e.g. for games where
    // held keys would trigger gestures); empty = no shortcut. Parsed by the
    // daemon via parseShortcutCombo; while paused only this combo is matched
    // and everything else passes through untouched.
    std::string pauseToggle;
};

struct EngineConfig {
    DelayConfig delay;
    LeaderConfig leader;
    AppFilterConfig appFilter;
    OverlayConfig overlay;
    BehaviorConfig behavior;
};

namespace detail {

// Int with the legacy slider constraint: out-of-range keeps the default.
inline int constrainedInt(const IniSection *s, std::string_view key, int def,
                          int min, int max) {
    const int v = parseIniInt(s, key, def);
    return (v >= min && v <= max) ? v : def;
}

// Never fabricate a leader from a malformed value: anything that cannot
// name a pressable key reads as kNoKeyCode (unassigned).
inline int keyCodeValue(const IniSection *s, std::string_view key) {
    const int code = parseIniInt(s, key, kNoKeyCode);
    return isUsableKeyCode(code) ? code : kNoKeyCode;
}

// Enum words as the legacy config serializes them; unknown keeps default.
inline AppFilterMode appFilterModeValue(const IniSection *s,
                                        AppFilterMode def) {
    const std::string v = parseIniString(s, "Mode");
    if (v == "Disabled") {
        return AppFilterMode::Disabled;
    }
    if (v == "Blacklist") {
        return AppFilterMode::Blacklist;
    }
    if (v == "Whitelist") {
        return AppFilterMode::Whitelist;
    }
    return def;
}

inline OverlayPlacement placementValue(const IniSection *s,
                                       OverlayPlacement def) {
    const std::string v = parseIniString(s, "Placement");
    if (v == "Grid") {
        return OverlayPlacement::Grid;
    }
    if (v == "MouseCursor") {
        return OverlayPlacement::MouseCursor;
    }
    if (v == "TextCaret") {
        return OverlayPlacement::TextCaret;
    }
    return def;
}

inline OverlayRow rowValue(const IniSection *s, OverlayRow def) {
    const std::string v = parseIniString(s, "Row");
    if (v == "Top") {
        return OverlayRow::Top;
    }
    if (v == "Center") {
        return OverlayRow::Center;
    }
    if (v == "Bottom") {
        return OverlayRow::Bottom;
    }
    return def;
}

inline OverlayColumn columnValue(const IniSection *s, OverlayColumn def) {
    const std::string v = parseIniString(s, "Column");
    if (v.size() == 4 && v.compare(0, 3, "Col") == 0 && v[3] >= '1' &&
        v[3] <= '7') {
        return static_cast<OverlayColumn>(v[3] - '1');
    }
    return def;
}

// [AppFilter/Blacklist]-style section with numeric keys 0..n. Mirrors the
// editor's reader: indices extend the list (gaps stay empty strings), the
// last assignment per index wins, non-numeric keys are skipped.
inline std::vector<std::string> indexedList(const IniSection *s) {
    std::vector<std::string> out;
    if (s == nullptr) {
        return out;
    }
    for (const auto &entry : s->entries) {
        char *end = nullptr;
        const long idx = std::strtol(entry.key.c_str(), &end, 10);
        if (end == entry.key.c_str() || *end != '\0' || idx < 0 ||
            idx > kMaxFilterListIndex) {
            continue;
        }
        if (out.size() <= static_cast<size_t>(idx)) {
            out.resize(static_cast<size_t>(idx) + 1);
        }
        out[static_cast<size_t>(idx)] = entry.value;
    }
    return out;
}

} // namespace detail

inline EngineConfig engineConfigFromIni(const IniDocument &doc) {
    EngineConfig c;

    const IniSection *delay = findIniSection(doc, "Delay");
    c.delay.lowercase = detail::constrainedInt(
        delay, "Lowercase", c.delay.lowercase, kDelayMin, kDelayMax);
    c.delay.uppercase = detail::constrainedInt(
        delay, "Uppercase", c.delay.uppercase, kDelayMin, kDelayMax);
    c.delay.lowercaseMin = detail::constrainedInt(
        delay, "LowercaseMin", c.delay.lowercaseMin, kMinHoldMin, kDelayMax);
    c.delay.uppercaseMin = detail::constrainedInt(
        delay, "UppercaseMin", c.delay.uppercaseMin, kMinHoldMin, kDelayMax);

    const IniSection *leader = findIniSection(doc, "Leader");
    c.leader.space = parseIniBool(leader, "Space", c.leader.space);
    c.leader.spaceReverse =
        parseIniBool(leader, "SpaceReverse", c.leader.spaceReverse);
    c.leader.left = parseIniBool(leader, "Left", c.leader.left);
    c.leader.leftReverse =
        parseIniBool(leader, "LeftReverse", c.leader.leftReverse);
    c.leader.right = parseIniBool(leader, "Right", c.leader.right);
    c.leader.rightReverse =
        parseIniBool(leader, "RightReverse", c.leader.rightReverse);
    c.leader.up = parseIniBool(leader, "Up", c.leader.up);
    c.leader.upReverse = parseIniBool(leader, "UpReverse", c.leader.upReverse);
    c.leader.down = parseIniBool(leader, "Down", c.leader.down);
    c.leader.downReverse =
        parseIniBool(leader, "DownReverse", c.leader.downReverse);
    c.leader.alt = parseIniBool(leader, "Alt", c.leader.alt);
    c.leader.altReverse =
        parseIniBool(leader, "AltReverse", c.leader.altReverse);
    c.leader.altGr = parseIniBool(leader, "AltGr", c.leader.altGr);
    c.leader.altGrReverse =
        parseIniBool(leader, "AltGrReverse", c.leader.altGrReverse);

    const IniSection *custom = findIniSection(doc, "Leader/Custom");
    c.leader.custom.enabled =
        parseIniBool(custom, "CustomKeyEnabled", c.leader.custom.enabled);
    c.leader.custom.key = parseIniString(custom, "CustomKey");
    c.leader.custom.keyCode = detail::keyCodeValue(custom, "CustomKeyCode");
    c.leader.custom.reverse =
        parseIniBool(custom, "CustomKeyReverse", c.leader.custom.reverse);
    c.leader.custom.key2Enabled =
        parseIniBool(custom, "CustomKey2Enabled", c.leader.custom.key2Enabled);
    c.leader.custom.key2 = parseIniString(custom, "CustomKey2");
    c.leader.custom.key2Code = detail::keyCodeValue(custom, "CustomKey2Code");
    c.leader.custom.key2Reverse =
        parseIniBool(custom, "CustomKey2Reverse", c.leader.custom.key2Reverse);

    const IniSection *filter = findIniSection(doc, "AppFilter");
    c.appFilter.mode = detail::appFilterModeValue(filter, c.appFilter.mode);
    c.appFilter.blacklist =
        detail::indexedList(findIniSection(doc, "AppFilter/Blacklist"));
    c.appFilter.whitelist =
        detail::indexedList(findIniSection(doc, "AppFilter/Whitelist"));

    const IniSection *overlay = findIniSection(doc, "Overlay");
    c.overlay.enabled = parseIniBool(overlay, "Enabled", c.overlay.enabled);
    c.overlay.showOnTrigger =
        parseIniBool(overlay, "ShowOnTrigger", c.overlay.showOnTrigger);
    c.overlay.placement = detail::placementValue(overlay, c.overlay.placement);
    c.overlay.progressBar =
        parseIniBool(overlay, "ProgressBar", c.overlay.progressBar);
    c.overlay.row = detail::rowValue(overlay, c.overlay.row);
    c.overlay.column = detail::columnValue(overlay, c.overlay.column);

    c.delay.unlimited = parseIniBool(delay, "Unlimited", c.delay.unlimited);

    const IniSection *behavior = findIniSection(doc, "Behavior");
    c.behavior.sortByFrequency =
        parseIniBool(behavior, "SortByFrequency", c.behavior.sortByFrequency);
    c.behavior.autoSelect =
        parseIniBool(behavior, "AutoSelect", c.behavior.autoSelect);
    c.behavior.leaderAutoRepeat =
        parseIniBool(behavior, "LeaderAutoRepeat", c.behavior.leaderAutoRepeat);
    c.behavior.autoSelectMs =
        detail::constrainedInt(behavior, "AutoSelectMs",
                               c.behavior.autoSelectMs, kDelayMin, kDelayMax);
    c.behavior.pauseToggle = parseIniString(behavior, "PauseToggle");

    return c;
}

inline EngineConfig parseEngineConfig(FILE *fp) {
    return engineConfigFromIni(parseIni(fp));
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_CORE_ENGINE_CONFIG_H
