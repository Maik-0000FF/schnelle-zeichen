// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKENDS_EVDEV_EVDEV_KEY_SOURCE_H
#define SCHNELLE_ZEICHEN_BACKENDS_EVDEV_EVDEV_KEY_SOURCE_H

// The KeySource implementation for the raw Linux backend: grab the physical
// keyboard exclusively (EVIOCGRAB), translate the kernel's unambiguous
// 0/1/2 values to Release/Press/Repeat, resolve keysym/text/modifiers via
// XkbResolver, hand each event to the engine's handler and forward
// everything not consumed through the uinput clone. Non-key events (SYN,
// MSC, LED) pass through verbatim so device state stays consistent.

#include "KeySource.h"
#include "uinput_forwarder.h"
#include "xkb_resolver.h"

#include <libevdev/libevdev.h>

#include <string>

namespace schnelle_zeichen {

class EvdevKeySource : public KeySource {
public:
    EvdevKeySource(XkbResolver &resolver, UinputForwarder &forwarder);
    ~EvdevKeySource() override;
    EvdevKeySource(const EvdevKeySource &) = delete;
    EvdevKeySource &operator=(const EvdevKeySource &) = delete;

    // Open the device; must run before start(). The forwarder is
    // initialized from this device's capabilities here (before the grab, so
    // the clone exists first, mirroring the spike order). The shared
    // resolver is seeded from the device's current state: keys held right
    // now always, the lock LEDs only when seedLocks is set. Callers pass
    // seedLocks=true for the startup grabs (the compositor kept those LEDs
    // truthful until this moment) and false for hotplug grabs, where a
    // fresh kernel device reports dark LEDs regardless of the seat state
    // and a blind sync would wrongly clear an active lock.
    bool open(const std::string &devicePath, bool seedLocks);

    void setHandler(Handler handler) override;
    bool start() override; // takes the grab
    void stop() override;  // releases it

    // The fd to register with epoll (EPOLLIN).
    int fd() const { return fd_; }
    // Drain and process all pending kernel events.
    void dispatch();
    // True once the kernel reported the device gone (unplug, BT
    // disconnect); the owner should drop this source.
    bool dead() const { return dead_; }

    libevdev *device() const { return dev_; }

private:
    void processEvent(unsigned int type, unsigned int code, int value);
    void seedResolverState(bool seedLocks);

    XkbResolver &resolver_;
    UinputForwarder &forwarder_;
    Handler handler_;
    libevdev *dev_ = nullptr;
    int fd_ = -1;
    bool grabbed_ = false;
    bool dead_ = false;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_EVDEV_KEY_SOURCE_H
