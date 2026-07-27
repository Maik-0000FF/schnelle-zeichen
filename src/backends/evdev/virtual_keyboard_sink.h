// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKENDS_EVDEV_VIRTUAL_KEYBOARD_SINK_H
#define SCHNELLE_ZEICHEN_BACKENDS_EVDEV_VIRTUAL_KEYBOARD_SINK_H

// TextSink over the Wayland virtual-keyboard protocol
// (zwp_virtual_keyboard_v1, implemented by wlroots-based compositors): an
// in-process replacement for the spike's wtype instrument. This is the
// preferred sink because it injects below the toolkit and therefore reaches
// every application, but its reach across compositors is narrow: the protocol
// never left wlr-protocols, KWin does not implement it (measured on 6.7:
// absent from the registry, including the privileged input-method socket) and
// Mutter refuses it. InputMethodSink covers the KWin case; see
// input_method_sink.h. A private xkb
// keymap maps each needed codepoint onto an INERT high keycode (spike
// finding: wtype's slot 0 lands on Escape, so browsers report
// code=Escape); the keymap grows on demand and is re-uploaded when new
// characters appear. After sending a commit the display is round-tripped,
// which serializes the commit against subsequently forwarded uinput events
// (the spike's two-channel ordering requirement).

#include "TextSink.h"

#include <cstdint>
#include <map>
#include <string>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;

namespace schnelle_zeichen {

// First injection slot in RAW evdev numbering: far above every physical
// key on real keyboards, still below KEY_MAX, so browsers and apps report
// no real event.code for injected characters.
inline constexpr uint32_t kFirstInjectionKeycode = 600;
inline constexpr uint32_t kLastInjectionKeycode = 760;

class VirtualKeyboardSink : public TextSink {
public:
    ~VirtualKeyboardSink() override;
    VirtualKeyboardSink() = default;
    VirtualKeyboardSink(const VirtualKeyboardSink &) = delete;
    VirtualKeyboardSink &operator=(const VirtualKeyboardSink &) = delete;

    // Connect to the compositor and create the virtual keyboard.
    // NoProtocol means the compositor is there but does not implement
    // virtual-keyboard (KWin, Mutter), which is where InputMethodSink takes
    // over; NoDisplayServer means no compositor was reachable at all, which
    // says nothing about protocols and may simply be a startup race.
    SinkInit init();

    void commit(const std::string &utf8) override;
    bool preeditSupported() const override { return false; }
    void commitPreedit(const std::string &) override {}
    void clearPreedit() override {}
    bool dead() const override { return dead_; }
    // Injection goes in below the toolkit as ordinary key events, so there is
    // no such thing as a target that cannot receive them. Only a dead
    // connection takes that away.
    bool canDeliver() const override { return !dead_; }

    // Registry plumbing (public for the C callback trampoline only).
    void onGlobal(wl_registry *registry, uint32_t name, const char *interface,
                  uint32_t version);

private:
    uint32_t slotFor(uint32_t codepoint); // 0 = no slot (table full or upload
                                          // failed)
    // False when the keymap could not be handed to the compositor. Callers
    // must not assume the compositor knows any slot added since the last
    // successful upload.
    bool uploadKeymap();
    void sendKey(uint32_t evdevCode);

    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_seat *seat_ = nullptr;
    zwp_virtual_keyboard_manager_v1 *manager_ = nullptr;
    zwp_virtual_keyboard_v1 *keyboard_ = nullptr;

    // The compositor connection is gone for good. Latched: a display whose
    // round trip failed never recovers, and every further commit would be
    // swallowed in silence while the keyboard grab stays in place.
    //
    // Asymmetry worth knowing: this sink only sends, so its fd is not in the
    // daemon's epoll set and it notices the death on the next commit, not
    // when it happens. A compositor that dies while the user is idle leaves
    // the grab in place until the next keystroke. InputMethodSink has to
    // receive anyway and notices immediately.
    bool dead_ = false;

    // slotFor() runs on the per-keystroke commit path, so a standing fault
    // must not write one journal line per character. Both conditions latch
    // separately, or the first one reported would hide the other diagnosis.
    // The upload latch is cleared again after a successful upload, making it
    // one warning per failure phase rather than one per process.
    bool keymapFullWarned_ = false;
    bool uploadFailedWarned_ = false;

    std::map<uint32_t, uint32_t> slotByCodepoint_; // codepoint -> evdev code
    uint32_t nextSlot_ = kFirstInjectionKeycode;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_VIRTUAL_KEYBOARD_SINK_H
