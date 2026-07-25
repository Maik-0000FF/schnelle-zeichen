// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "evdev_key_source.h"

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

#include <cerrno>
#include <ctime>

#include <utility>

namespace schnelle_zeichen {

namespace {
// Kernel EV_KEY values (the clean three-state signal at its source).
constexpr int kKeyValueRelease = 0;
constexpr int kKeyValuePress = 1;
constexpr int kKeyValueRepeat = 2;
} // namespace

EvdevKeySource::EvdevKeySource(XkbResolver &resolver,
                               UinputForwarder &forwarder)
    : resolver_(resolver), forwarder_(forwarder) {}

EvdevKeySource::~EvdevKeySource() {
    // Inline ungrab instead of the virtual stop(): virtual dispatch is
    // already gone during destruction.
    if (grabbed_ && dev_ != nullptr) {
        libevdev_grab(dev_, LIBEVDEV_UNGRAB);
        grabbed_ = false;
    }
    if (dev_ != nullptr) {
        libevdev_free(dev_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool EvdevKeySource::open(const std::string &devicePath, bool seedLocks) {
    fd_ = ::open(devicePath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        return false;
    }
    if (libevdev_new_from_fd(fd_, &dev_) < 0) {
        return false;
    }
    seedResolverState(seedLocks);
    // Clone first, then grab (start()): the passthrough channel must exist
    // and be settled before any event can need forwarding.
    return forwarder_.init(dev_);
}

// Seed the shared resolver from the state snapshot libevdev took at open,
// so the xkb state starts from reality instead of all-clear: without this a
// CapsLock active before the daemon started stayed inverted until the next
// restart (every physical toggle flipped reality and the resolver in
// opposite directions), and modifiers held across a start or hotplug were
// wrong until re-pressed.
void EvdevKeySource::seedResolverState(bool seedLocks) {
    for (unsigned int code = 0; code <= KEY_MAX; ++code) {
        if (libevdev_get_event_value(dev_, EV_KEY, code) != 0) {
            resolver_.updateKey(code, true);
        }
    }
    // Held keys first: the lock sync below compares against the state these
    // presses produced (a physically held lock key has already toggled it).
    if (seedLocks && libevdev_has_event_type(dev_, EV_LED) != 0) {
        resolver_.syncLockedModFromLed(
            XKB_MOD_NAME_CAPS, KEY_CAPSLOCK,
            libevdev_get_event_value(dev_, EV_LED, LED_CAPSL) != 0);
        resolver_.syncLockedModFromLed(
            XKB_MOD_NAME_NUM, KEY_NUMLOCK,
            libevdev_get_event_value(dev_, EV_LED, LED_NUML) != 0);
    }
}

void EvdevKeySource::setHandler(Handler handler) {
    handler_ = std::move(handler);
}

bool EvdevKeySource::start() {
    if (dev_ == nullptr) {
        return false;
    }
    if (libevdev_grab(dev_, LIBEVDEV_GRAB) < 0) {
        return false;
    }
    grabbed_ = true;
    return true;
}

void EvdevKeySource::stop() {
    if (grabbed_ && dev_ != nullptr) {
        libevdev_grab(dev_, LIBEVDEV_UNGRAB);
        grabbed_ = false;
    }
}

void EvdevKeySource::processEvent(unsigned int type, unsigned int code,
                                  int value) {
    if (type != EV_KEY) {
        // SYN/MSC/LED etc.: forward verbatim.
        forwarder_.forward(type, code, value);
        return;
    }

    KeyEvent e;
    switch (value) {
    case kKeyValuePress:
        e.action = KeyAction::Press;
        break;
    case kKeyValueRepeat:
        e.action = KeyAction::Repeat;
        break;
    case kKeyValueRelease:
    default:
        e.action = KeyAction::Release;
        break;
    }
    // Resolve BEFORE updating state: a press is interpreted under the
    // modifiers held before it (Shift+a resolves as "A" because Shift's own
    // earlier press already updated the state).
    e.code = code + kXkbKeycodeOffset;
    e.keysym = resolver_.keysym(code);
    e.text = resolver_.text(code);
    e.modifiers = resolver_.modifierMask();
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    e.timeUsec = static_cast<uint64_t>(ts.tv_sec) * 1'000'000 +
                 static_cast<uint64_t>(ts.tv_nsec) / 1'000;

    if (value == kKeyValuePress || value == kKeyValueRelease) {
        resolver_.updateKey(code, value == kKeyValuePress);
    }

    const bool consumed = handler_ && handler_(e);
    if (!consumed) {
        forwarder_.forward(EV_KEY, code, value);
        forwarder_.syn();
    }
}

void EvdevKeySource::dispatch() {
    input_event ie{};
    int rc = libevdev_next_event(
        dev_, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING, &ie);
    while (rc == LIBEVDEV_READ_STATUS_SUCCESS ||
           rc == LIBEVDEV_READ_STATUS_SYNC) {
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // Dropped events: drain the sync sequence, forwarding it so
            // device state stays consistent (spike behavior). The replay
            // bypasses the engine on purpose (no gestures from stale
            // events), but the resolver must see every key transition: a
            // modifier or lock flip lost in the drop would otherwise leave
            // the xkb state divergent until that key is pressed again.
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                if (ie.type == EV_KEY && (ie.value == kKeyValuePress ||
                                          ie.value == kKeyValueRelease)) {
                    resolver_.updateKey(ie.code, ie.value == kKeyValuePress);
                }
                forwarder_.forward(ie.type, ie.code, ie.value);
                rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_SYNC, &ie);
            }
            continue;
        }
        processEvent(ie.type, ie.code, ie.value);
        rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_NORMAL, &ie);
    }
    // Anything but "no more events" means the device is gone (unplug, BT
    // disconnect); mark it for removal by the owner.
    if (rc != -EAGAIN) {
        dead_ = true;
    }
}

} // namespace schnelle_zeichen
