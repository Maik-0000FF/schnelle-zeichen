// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKENDS_EVDEV_INPUT_METHOD_SINK_H
#define SCHNELLE_ZEICHEN_BACKENDS_EVDEV_INPUT_METHOD_SINK_H

// TextSink over the Wayland input-method protocol (zwp_input_method_v1), the
// fallback for compositors that do not implement zwp_virtual_keyboard_v1.
// KWin/Plasma is the case that forces it: measured against KWin 6.7, the
// virtual-keyboard manager is absent from the registry entirely, including on
// the privileged socket a compositor-launched input method gets.
//
// Unlike the virtual-keyboard sink this one transports text, not keycodes, so
// there is no synthetic keymap and no injection slots. In exchange it is not
// fire-and-forget: the compositor hands out a context only while an
// application that speaks text-input v2/v3 holds focus, so this sink must
// receive events (fd()/dispatch(), driven by the daemon's epoll loop) and can
// only commit while that context exists.
//
// Reach is therefore narrower than raw injection. Applications that do not
// implement text-input never activate an input method; X11 applications are
// out of reach entirely, because Xwayland never requests the protocol from
// the compositor; and a configured IM framework (QT_IM_MODULE/GTK_IM_MODULE)
// routes Qt and GTK past the protocol to itself, which leaves this sink
// permanently inactive. init() names that last case instead of failing
// silently.

#include "TextSink.h"

#include <cstdint>
#include <string>

struct wl_display;
struct wl_registry;
struct zwp_input_method_v1;
struct zwp_input_method_context_v1;

namespace schnelle_zeichen {

// Environment variables that hand Qt/GTK text input to an IM framework. Their
// presence does not break this sink, but it does mean the toolkits bypass the
// Wayland protocol, so nothing will ever be injected into those applications.
inline constexpr const char *kQtImModuleEnv = "QT_IM_MODULE";
inline constexpr const char *kGtkImModuleEnv = "GTK_IM_MODULE";

class InputMethodSink : public TextSink {
public:
    ~InputMethodSink() override;
    InputMethodSink() = default;
    InputMethodSink(const InputMethodSink &) = delete;
    InputMethodSink &operator=(const InputMethodSink &) = delete;

    // Connects and binds zwp_input_method_v1. Success means the protocol
    // exists, not that anything is focused; activation follows at runtime.
    bool init();

    // Wayland connection fd for the daemon's epoll set, -1 before init().
    int fd() const;

    // Reads and dispatches pending events (activate/deactivate/commit_state).
    // Called when epoll reports the fd readable.
    void dispatch();

    void commit(const std::string &utf8) override;
    bool preeditSupported() const override;
    void commitPreedit(const std::string &utf8) override;
    void clearPreedit() override;

    // Whether a context is currently held, i.e. whether commit() can reach an
    // application right now.
    bool active() const { return context_ != nullptr; }

    // Called from the C listener trampolines; not part of the TextSink
    // contract.
    void onGlobal(wl_registry *registry, uint32_t name, const char *interface,
                  uint32_t version);
    void onActivate(zwp_input_method_context_v1 *context);
    void onDeactivate(zwp_input_method_context_v1 *context);
    void onCommitState(uint32_t serial);

private:
    void destroyContext();

    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    zwp_input_method_v1 *inputMethod_ = nullptr;
    // Non-null exactly while a text-input-capable application holds focus.
    zwp_input_method_context_v1 *context_ = nullptr;
    // Last serial the compositor announced via commit_state; every
    // commit_string/preedit_string request has to carry it.
    uint32_t serial_ = 0;
    // One-shot so a session spent in non-text-input applications does not
    // repeat the same warning on every gesture.
    bool inactiveWarned_ = false;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_INPUT_METHOD_SINK_H
