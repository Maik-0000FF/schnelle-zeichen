// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "xkb_resolver.h"

#include "KeySource.h" // KeyModifier

#include <array>

namespace schnelle_zeichen {

XkbResolver::~XkbResolver() {
    if (state_ != nullptr) {
        xkb_state_unref(state_);
    }
    if (keymap_ != nullptr) {
        xkb_keymap_unref(keymap_);
    }
    if (context_ != nullptr) {
        xkb_context_unref(context_);
    }
}

bool XkbResolver::init(const std::string &layout) {
    context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context_ == nullptr) {
        return false;
    }
    xkb_rule_names names{};
    if (!layout.empty()) {
        names.layout = layout.c_str();
    }
    keymap_ = xkb_keymap_new_from_names(context_, &names,
                                        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap_ == nullptr) {
        return false;
    }
    state_ = xkb_state_new(keymap_);
    return state_ != nullptr;
}

void XkbResolver::updateKey(uint32_t evdevCode, bool pressed) {
    xkb_state_update_key(state_, evdevCode + kXkbKeycodeOffset,
                         pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
}

uint32_t XkbResolver::keysym(uint32_t evdevCode) const {
    return xkb_state_key_get_one_sym(state_, evdevCode + kXkbKeycodeOffset);
}

std::string XkbResolver::text(uint32_t evdevCode) const {
    std::array<char, 64> buf{};
    const int n = xkb_state_key_get_utf8(state_, evdevCode + kXkbKeycodeOffset,
                                         buf.data(), buf.size());
    if (n <= 0) {
        return {};
    }
    return {buf.data(), static_cast<size_t>(n)};
}

uint32_t XkbResolver::modifierMask() const {
    uint32_t mask = 0;
    const auto active = [this](const char *name) {
        return xkb_state_mod_name_is_active(state_, name,
                                            XKB_STATE_MODS_EFFECTIVE) > 0;
    };
    if (active(XKB_MOD_NAME_SHIFT)) {
        mask = mask | KeyModifier::Shift;
    }
    if (active(XKB_MOD_NAME_CTRL)) {
        mask = mask | KeyModifier::Ctrl;
    }
    if (active(XKB_MOD_NAME_ALT)) {
        mask = mask | KeyModifier::Alt;
    }
    // AltGr is the level-3 shift, reported as Mod5 on standard layouts.
    if (active("Mod5")) {
        mask = mask | KeyModifier::AltGr;
    }
    if (active(XKB_MOD_NAME_LOGO)) {
        mask = mask | KeyModifier::Super;
    }
    if (active(XKB_MOD_NAME_CAPS)) {
        mask = mask | KeyModifier::CapsLock;
    }
    return mask;
}

} // namespace schnelle_zeichen
