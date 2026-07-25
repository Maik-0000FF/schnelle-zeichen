// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "xkb_resolver.h"

#include "KeySource.h" // KeyModifier

#include <linux/input.h> // KEY_CAPSLOCK/KEY_NUMLOCK (lock carry-over)

#include <array>

namespace schnelle_zeichen {

namespace {

void freeXkb(xkb_state *state, xkb_keymap *keymap, xkb_context *context) {
    if (state != nullptr) {
        xkb_state_unref(state);
    }
    if (keymap != nullptr) {
        xkb_keymap_unref(keymap);
    }
    if (context != nullptr) {
        xkb_context_unref(context);
    }
}

} // namespace

XkbResolver::~XkbResolver() { freeXkb(state_, keymap_, context_); }

bool XkbResolver::init(const std::string &layout) {
    // Build the new keymap/state completely before touching the members and
    // swap only on success: a failed RE-init (a broken Layout value from a
    // config reload) must keep the old resolver working instead of leaving
    // it stateless.
    xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context == nullptr) {
        return false;
    }
    xkb_rule_names names{};
    if (!layout.empty()) {
        names.layout = layout.c_str();
    }
    xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == nullptr) {
        xkb_context_unref(context);
        return false;
    }
    xkb_state *state = xkb_state_new(keymap);
    if (state == nullptr) {
        freeXkb(nullptr, keymap, context);
        return false;
    }
    // Carry the lock state across a re-init: the LEDs of grabbed devices
    // are no longer compositor-managed, so the OLD state is the only
    // truthful source left (held modifiers are transient and self-correct
    // on their next transition; locks would stay inverted forever).
    const bool capsLocked = state_ != nullptr && xkb_state_mod_name_is_active(
                                                     state_, XKB_MOD_NAME_CAPS,
                                                     XKB_STATE_MODS_LOCKED) > 0;
    const bool numLocked = state_ != nullptr && xkb_state_mod_name_is_active(
                                                    state_, XKB_MOD_NAME_NUM,
                                                    XKB_STATE_MODS_LOCKED) > 0;
    freeXkb(state_, keymap_, context_);
    context_ = context;
    keymap_ = keymap;
    state_ = state;
    if (capsLocked) {
        syncLockedModFromLed(XKB_MOD_NAME_CAPS, KEY_CAPSLOCK, true);
    }
    if (numLocked) {
        syncLockedModFromLed(XKB_MOD_NAME_NUM, KEY_NUMLOCK, true);
    }
    return true;
}

void XkbResolver::updateKey(uint32_t evdevCode, bool pressed) {
    xkb_state_update_key(state_, evdevCode + kXkbKeycodeOffset,
                         pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
}

void XkbResolver::syncLockedModFromLed(const char *modName, uint32_t evdevCode,
                                       bool ledOn) {
    const bool locked = xkb_state_mod_name_is_active(state_, modName,
                                                     XKB_STATE_MODS_LOCKED) > 0;
    if (locked == ledOn) {
        return;
    }
    // Replayed through updateKey on purpose: the resolver stays a pure
    // update_key consumer (mixing in xkb_state_update_mask would clobber the
    // depressed-modifier component the seeded held keys just built).
    updateKey(evdevCode, true);
    updateKey(evdevCode, false);
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
