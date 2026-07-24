// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_INPUT_TEXTSINK_H
#define SCHNELLE_ZEICHEN_INPUT_TEXTSINK_H

#include <string>

namespace schnelle_zeichen {

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
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_INPUT_TEXTSINK_H
