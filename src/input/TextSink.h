// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_INPUT_TEXTSINK_H
#define SCHNELLE_ZEICHEN_INPUT_TEXTSINK_H

#include <string>

namespace schnelle_zeichen {

// Why a backend's init() did or did not come up. The failures stay apart
// because only one of them is permanent, and a service manager has to react in
// opposite ways: give up on the hopeless session, keep retrying everything
// else. Exactly one value here means "this can never work".
enum class SinkInit {
    Ok,
    // Nothing answered: the session socket may not be up yet (the documented
    // race around importing WAYLAND_DISPLAY) or the compositor is restarting.
    // Transient.
    NoDisplayServer,
    // The display server answered and does not offer the protocol at all.
    // Permanent for this session.
    ProtocolAbsent,
    // The protocol is advertised but could not be taken into use: another
    // input method already holds it, or the compositor refused. Transient,
    // because it says something about the current competition for the
    // protocol, not about the session. Keeping this apart from
    // ProtocolAbsent matters: reporting it as permanent would stop the
    // service for good over a conflict that resolves on its own.
    ProtocolUnusable,
};

// Injects finalized text into the focused application and, where the backend
// can, shows a provisional pre-edit.
//
// Pre-edit (the underlined, not-yet-committed preview) is a cooperative
// contract with the target app over an IME protocol, not something
// schnelle-zeichen can draw on its own. A raw-injection backend (uinput,
// CGEventPost) has no such channel, so preeditSupported() is false and the
// engine drives the cycling preview through schnelle-zeichen's own overlay
// instead. Only an IME-protocol backend (an IM framework) returns true and
// renders a real in-app pre-edit.
class TextSink {
public:
    virtual ~TextSink() = default;

    // Inject finalized UTF-8 text at the caret.
    virtual void commit(const std::string &utf8) = 0;

    // Whether commitPreedit()/clearPreedit() reach the app as marked text.
    virtual bool preeditSupported() const = 0;

    // Show / clear provisional text. No-op when preeditSupported() is false.
    virtual void commitPreedit(const std::string &utf8) = 0;
    virtual void clearPreedit() = 0;

    // Whether the channel into the applications is permanently gone (the
    // compositor connection died). A dead sink swallows every commit, so the
    // daemon must not keep running on one: it still holds an exclusive
    // keyboard grab, and the user would lose text without any sign of it.
    // Backends that cannot die this way keep the default.
    virtual bool dead() const { return false; }
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_INPUT_TEXTSINK_H
