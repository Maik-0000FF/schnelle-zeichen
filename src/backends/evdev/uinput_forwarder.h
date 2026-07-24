// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKENDS_EVDEV_UINPUT_FORWARDER_H
#define SCHNELLE_ZEICHEN_BACKENDS_EVDEV_UINPUT_FORWARDER_H

// The transparent passthrough channel, from the spike: a uinput clone of
// the grabbed keyboard. Every event the engine does not consume is written
// here unchanged, so typing feel is untouched.

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

namespace schnelle_zeichen {

// Give the compositor time to bind the new uinput device before events
// flow (spike-measured).
inline constexpr int kUinputSettleMs = 700;

class UinputForwarder {
public:
    ~UinputForwarder();
    UinputForwarder() = default;
    UinputForwarder(const UinputForwarder &) = delete;
    UinputForwarder &operator=(const UinputForwarder &) = delete;

    // Clone the source device's capabilities and wait the settle period.
    bool init(libevdev *sourceDevice);

    void forward(unsigned int type, unsigned int code, int value);
    void syn();

private:
    libevdev_uinput *uinput_ = nullptr;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_UINPUT_FORWARDER_H
