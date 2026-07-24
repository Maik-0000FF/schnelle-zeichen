// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "uinput_forwarder.h"

#include <unistd.h>
#include <linux/input.h>

namespace schnelle_zeichen {

namespace {
constexpr useconds_t kUsecPerMs = 1'000;
} // namespace

UinputForwarder::~UinputForwarder() {
    if (uinput_ != nullptr) {
        libevdev_uinput_destroy(uinput_);
    }
}

bool UinputForwarder::init(libevdev *sourceDevice) {
    if (libevdev_uinput_create_from_device(
            sourceDevice, LIBEVDEV_UINPUT_OPEN_MANAGED, &uinput_) < 0) {
        return false;
    }
    usleep(static_cast<useconds_t>(kUinputSettleMs) * kUsecPerMs);
    return true;
}

void UinputForwarder::forward(unsigned int type, unsigned int code, int value) {
    libevdev_uinput_write_event(uinput_, type, code, value);
}

void UinputForwarder::syn() { forward(EV_SYN, SYN_REPORT, 0); }

} // namespace schnelle_zeichen
