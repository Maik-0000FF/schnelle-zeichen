// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_ENGINE_LEADER_CLASSIFY_H
#define SCHNELLE_ZEICHEN_ENGINE_LEADER_CLASSIFY_H

// Pure leader classification, factored out of the legacy engine's
// classifyLeader/leaderStep/isDualCustomAllowed/sanitize helpers. Built-in
// leaders (Space, arrows, Alt, AltGr) match by keysym; a custom leader IS
// its physical key and matches by keycode only, so it fires through any
// layout and any shift state. The precedence (Alt/AltGr, then custom, then
// Space, then arrows) is shared between classification and step direction,
// so the step always comes from the same flag that classified the press.

#include "engine_config.h"
#include "hand_classifier.h" // kNoKeyCode, isUsableKeyCode, isLeftHandKeycode
#include "keysyms.h"
#include "mappings_io.h" // utf8FirstCharBytes

#include <cstdint>
#include <string>

namespace schnelle_zeichen {

enum class LeaderType { None, BuiltIn, Custom1, Custom2 };

// The sanitized runtime view of the custom leaders, rebuilt on config load
// (mirrors the legacy cachedCustomKey*_ fields): disabled leaders collapse
// to kNoKeyCode, characters are trimmed to one lowercased UTF-8 char.
struct LeaderSetup {
    int custom1Code = kNoKeyCode;
    int custom2Code = kNoKeyCode;
    std::string custom1Char;
    std::string custom2Char;
};

// A keycode outside the pressable range collapses to kNoKeyCode: it would
// otherwise count as "configured" while never matching a real key.
inline int sanitizeKeyCode(int raw) {
    return isUsableKeyCode(raw) ? raw : kNoKeyCode;
}

// Trim whitespace, keep only the first UTF-8 character, lowercase ASCII.
// The character does not trigger the leader (its keycode does); it only
// shapes labels and the mapped-input collision check.
inline std::string sanitizeCustomKey(const std::string &raw) {
    const size_t start = raw.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = raw.find_last_not_of(" \t\n\r");
    const std::string trimmed = raw.substr(start, end - start + 1);
    const size_t firstBytes =
        utf8FirstCharBytes(trimmed.data(), trimmed.size());
    if (firstBytes == 0) {
        return ""; // invalid UTF-8: never smuggle garbage into the label
    }
    std::string result = trimmed.substr(0, firstBytes);
    if (result.size() == 1 && result[0] >= 'A' && result[0] <= 'Z') {
        result[0] = static_cast<char>(result[0] - 'A' + 'a');
    }
    return result;
}

inline LeaderSetup buildLeaderSetup(const CustomLeaderConfig &c) {
    LeaderSetup s;
    s.custom1Code = c.enabled ? sanitizeKeyCode(c.keyCode) : kNoKeyCode;
    s.custom2Code = c.key2Enabled ? sanitizeKeyCode(c.key2Code) : kNoKeyCode;
    s.custom1Char = c.enabled ? sanitizeCustomKey(c.key) : "";
    s.custom2Char = c.key2Enabled ? sanitizeCustomKey(c.key2) : "";
    return s;
}

inline bool matchCustomLeader(int customKeyCode, int code) {
    return customKeyCode != kNoKeyCode && code == customKeyCode;
}

inline LeaderType classifyLeader(const LeaderConfig &leader,
                                 const LeaderSetup &setup, uint32_t keysym,
                                 int code) {
    if (leader.alt && isAltSym(keysym)) {
        return LeaderType::BuiltIn;
    }
    if (leader.altGr && isAltGrSym(keysym)) {
        return LeaderType::BuiltIn;
    }
    if (matchCustomLeader(setup.custom1Code, code)) {
        return LeaderType::Custom1;
    }
    if (matchCustomLeader(setup.custom2Code, code)) {
        return LeaderType::Custom2;
    }
    if (leader.space && keysym == kKeysymSpace) {
        return LeaderType::BuiltIn;
    }
    if (leader.left && keysym == kKeysymLeft) {
        return LeaderType::BuiltIn;
    }
    if (leader.right && keysym == kKeysymRight) {
        return LeaderType::BuiltIn;
    }
    if (leader.up && keysym == kKeysymUp) {
        return LeaderType::BuiltIn;
    }
    if (leader.down && keysym == kKeysymDown) {
        return LeaderType::BuiltIn;
    }
    return LeaderType::None;
}

// Cycle step for a leader press: -1 when that leader's reverse flag is set,
// +1 otherwise. Precedence mirrors classifyLeader.
inline int leaderStep(const LeaderConfig &leader, const LeaderSetup &setup,
                      uint32_t keysym, int code) {
    bool reverse = false;
    if (isAltSym(keysym)) {
        reverse = leader.altReverse;
    } else if (isAltGrSym(keysym)) {
        reverse = leader.altGrReverse;
    } else if (matchCustomLeader(setup.custom1Code, code)) {
        reverse = leader.custom.reverse;
    } else if (matchCustomLeader(setup.custom2Code, code)) {
        reverse = leader.custom.key2Reverse;
    } else if (keysym == kKeysymSpace) {
        reverse = leader.spaceReverse;
    } else if (keysym == kKeysymLeft) {
        reverse = leader.leftReverse;
    } else if (keysym == kKeysymRight) {
        reverse = leader.rightReverse;
    } else if (keysym == kKeysymUp) {
        reverse = leader.upReverse;
    } else if (keysym == kKeysymDown) {
        reverse = leader.downReverse;
    }
    return reverse ? -1 : +1;
}

// Dual custom split: when BOTH custom leaders are configured on opposite
// keyboard halves, each triggers only inputs on the OTHER half. Built-ins
// are unrestricted; the split switches off whenever it has no meaning.
inline bool isDualCustomAllowed(const LeaderSetup &setup, LeaderType leader,
                                int inputKeyCode) {
    if (leader == LeaderType::BuiltIn || leader == LeaderType::None) {
        return true;
    }
    if (setup.custom1Code == kNoKeyCode || setup.custom2Code == kNoKeyCode) {
        return true;
    }
    if (setup.custom1Code == setup.custom2Code) {
        return true;
    }
    const bool key1Left = isLeftHandKeycode(setup.custom1Code);
    const bool key2Left = isLeftHandKeycode(setup.custom2Code);
    if (key1Left == key2Left) {
        return true;
    }
    if (inputKeyCode == kNoKeyCode) {
        return true;
    }
    const bool inputLeft = isLeftHandKeycode(inputKeyCode);
    if (leader == LeaderType::Custom1) {
        return key1Left ? !inputLeft : inputLeft;
    }
    return key2Left ? !inputLeft : inputLeft;
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_ENGINE_LEADER_CLASSIFY_H
