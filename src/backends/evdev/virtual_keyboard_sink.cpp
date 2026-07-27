// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "virtual_keyboard_sink.h"

#include "log.h"
#include "mappings_io.h"  // utf8DecodeFirst (shared UTF-8 decoding)
#include "xkb_resolver.h" // kXkbKeycodeOffset (evdev -> XKB numbering)

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "virtual-keyboard-unstable-v1-client-protocol.h"

#include <array>
#include <cstring>
#include <vector>

namespace schnelle_zeichen {

namespace {

// Upper keycode bound advertised in the generated keymap. Every injection
// slot must fit below it in XKB numbering, or the upper slots silently fall
// outside the keymap.
constexpr uint32_t kKeymapMaxKeycode = 800;
static_assert(kLastInjectionKeycode + kXkbKeycodeOffset <= kKeymapMaxKeycode);

// Control characters have no printable form in a keymap; map them to their
// dedicated keysyms instead of xkb_utf32_to_keysym's generic offset.
uint32_t keysymForCodepoint(uint32_t cp) {
    if (cp == '\n') {
        return XKB_KEY_Return;
    }
    if (cp == '\t') {
        return XKB_KEY_Tab;
    }
    return xkb_utf32_to_keysym(cp);
}

// Decode one UTF-8 sequence at s[i] via the shared decoder; returns the
// codepoint and advances i. Invalid sequences yield U+FFFD and advance one
// byte (defensive; commit strings come from validated mappings).
uint32_t decodeUtf8(const std::string &s, size_t &i) {
    uint32_t cp = 0;
    const size_t n = utf8DecodeFirst(s.data() + i, s.size() - i, cp);
    if (n == 0) {
        ++i;
        return 0xFFFD;
    }
    i += n;
    return cp;
}

void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                    const char *interface, uint32_t version) {
    static_cast<VirtualKeyboardSink *>(data)->onGlobal(registry, name,
                                                       interface, version);
}
void registryGlobalRemove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener kRegistryListener = {registryGlobal,
                                                registryGlobalRemove};

} // namespace

VirtualKeyboardSink::~VirtualKeyboardSink() {
    if (keyboard_ != nullptr) {
        zwp_virtual_keyboard_v1_destroy(keyboard_);
    }
    if (manager_ != nullptr) {
        zwp_virtual_keyboard_manager_v1_destroy(manager_);
    }
    if (seat_ != nullptr) {
        wl_seat_destroy(seat_);
    }
    if (registry_ != nullptr) {
        wl_registry_destroy(registry_);
    }
    if (display_ != nullptr) {
        wl_display_disconnect(display_);
    }
}

void VirtualKeyboardSink::onGlobal(wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t) {
    if (std::strcmp(interface, wl_seat_interface.name) == 0 &&
        seat_ == nullptr) {
        seat_ = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface, 1));
    } else if (std::strcmp(interface,
                           zwp_virtual_keyboard_manager_v1_interface.name) ==
               0) {
        manager_ =
            static_cast<zwp_virtual_keyboard_manager_v1 *>(wl_registry_bind(
                registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
    }
}

SinkInit VirtualKeyboardSink::init() {
    display_ = wl_display_connect(nullptr);
    if (display_ == nullptr) {
        warn("virtual keyboard: no wayland display");
        return SinkInit::NoDisplayServer;
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    wl_display_roundtrip(display_);
    if (seat_ == nullptr || manager_ == nullptr) {
        warn("virtual keyboard: compositor lacks zwp_virtual_keyboard_v1");
        return SinkInit::ProtocolAbsent;
    }
    keyboard_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
        manager_, seat_);
    // The protocol requires a keymap before the first key event.
    uploadKeymap();
    wl_display_roundtrip(display_);
    if (keyboard_ == nullptr) {
        // The manager was there, so the protocol exists in this session and
        // only the object could not be created. Not a session property.
        warn("virtual keyboard: could not create a virtual keyboard although "
             "the compositor advertises the protocol");
        return SinkInit::ProtocolUnusable;
    }
    return SinkInit::Ok;
}

uint32_t VirtualKeyboardSink::slotFor(uint32_t codepoint) {
    const auto it = slotByCodepoint_.find(codepoint);
    if (it != slotByCodepoint_.end()) {
        return it->second;
    }
    if (nextSlot_ > kLastInjectionKeycode) {
        warn("virtual keyboard: injection keymap full");
        return 0;
    }
    const uint32_t slot = nextSlot_++;
    slotByCodepoint_[codepoint] = slot;
    uploadKeymap();
    return slot;
}

void VirtualKeyboardSink::uploadKeymap() {
    // Minimal xkb_v1 keymap holding exactly our injection slots. Keycodes in
    // the keymap are XKB numbering (evdev+8).
    std::string map = "xkb_keymap {\n"
                      "xkb_keycodes \"sz\" { minimum = " +
                      std::to_string(kXkbKeycodeOffset) +
                      "; maximum = " + std::to_string(kKeymapMaxKeycode) +
                      ";\n";
    for (const auto &kv : slotByCodepoint_) {
        map += "<I" + std::to_string(kv.second) +
               "> = " + std::to_string(kv.second + kXkbKeycodeOffset) + ";\n";
    }
    map += "};\n"
           "xkb_types \"sz\" { };\n"
           "xkb_compatibility \"sz\" { };\n"
           "xkb_symbols \"sz\" {\n";
    for (const auto &kv : slotByCodepoint_) {
        std::array<char, 64> name{};
        if (xkb_keysym_get_name(keysymForCodepoint(kv.first), name.data(),
                                name.size()) <= 0) {
            continue;
        }
        map += "key <I" + std::to_string(kv.second) + "> { [ " + name.data() +
               " ] };\n";
    }
    map += "};\n};\n";

    const int fd = memfd_create("sz-keymap", MFD_CLOEXEC);
    if (fd < 0) {
        return;
    }
    // The advertised size counts the terminating NUL; the compositor mmaps
    // that many bytes, so the NUL must be written too or reading the last
    // byte faults when the string length is a page-size multiple.
    const size_t mapSize = map.size() + 1;
    const bool ok =
        write(fd, map.data(), mapSize) == static_cast<ssize_t>(mapSize);
    if (ok) {
        zwp_virtual_keyboard_v1_keymap(keyboard_,
                                       WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                                       static_cast<uint32_t>(mapSize));
        wl_display_flush(display_);
    }
    close(fd);
}

void VirtualKeyboardSink::sendKey(uint32_t evdevCode) {
    zwp_virtual_keyboard_v1_key(keyboard_, 0, evdevCode,
                                WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(keyboard_, 0, evdevCode,
                                WL_KEYBOARD_KEY_STATE_RELEASED);
}

void VirtualKeyboardSink::commit(const std::string &utf8) {
    if (keyboard_ == nullptr || utf8.empty() || dead_) {
        return;
    }
    size_t i = 0;
    while (i < utf8.size()) {
        const uint32_t cp = decodeUtf8(utf8, i);
        const uint32_t slot = slotFor(cp);
        if (slot != 0) {
            sendKey(slot);
        }
    }
    // Serialization barrier: the commit is fully processed by the
    // compositor before any subsequently forwarded uinput event can be
    // handled (the spike's two-channel ordering requirement). It doubles as
    // the liveness check: a failed round trip means the compositor connection
    // is gone, this text never arrived, and no later commit will either.
    // Without the check every keystroke would vanish in silence while the
    // grab keeps swallowing the keyboard.
    if (wl_display_roundtrip(display_) < 0) {
        dead_ = true;
        warn("virtual keyboard: wayland connection lost; no further text can "
             "be injected");
    }
}

} // namespace schnelle_zeichen
