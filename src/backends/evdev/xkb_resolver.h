// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKENDS_EVDEV_XKB_RESOLVER_H
#define SCHNELLE_ZEICHEN_BACKENDS_EVDEV_XKB_RESOLVER_H

// libxkbcommon layer for the raw backend: builds the keymap (configured
// layout, falling back to the XKB_DEFAULT_* environment and the system
// default), tracks modifier/lock state from the raw key stream, and
// resolves evdev keycodes to keysym, UTF-8 text and the KeyModifier mask.
// Keycode convention: the resolver takes RAW evdev codes and applies the
// +8 XKB offset internally; everything above (engine, hand classifier)
// speaks evdev+8.

#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <string>

namespace schnelle_zeichen {

// The evdev-to-XKB keycode offset (X11 heritage, shared by every consumer).
inline constexpr uint32_t kXkbKeycodeOffset = 8;

class XkbResolver {
public:
    ~XkbResolver();
    XkbResolver() = default;
    XkbResolver(const XkbResolver &) = delete;
    XkbResolver &operator=(const XkbResolver &) = delete;

    // layout may be empty (XKB defaults / environment apply).
    bool init(const std::string &layout);

    // Feed every physical key transition (consumed or not) so modifier and
    // lock state mirror the real keyboard.
    void updateKey(uint32_t evdevCode, bool pressed);

    uint32_t keysym(uint32_t evdevCode) const;
    std::string text(uint32_t evdevCode) const;
    // Current effective modifiers as the KeyModifier mask (incl. CapsLock).
    uint32_t modifierMask() const;

private:
    xkb_context *context_ = nullptr;
    xkb_keymap *keymap_ = nullptr;
    xkb_state *state_ = nullptr;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_XKB_RESOLVER_H
