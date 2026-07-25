// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "uinput_forwarder.h"

#include "device_discovery.h" // kVirtualDeviceMarker

#include <linux/input.h>

#include <string>

namespace schnelle_zeichen {

UinputForwarder::~UinputForwarder() {
    if (uinput_ != nullptr) {
        libevdev_uinput_destroy(uinput_);
    }
}

bool UinputForwarder::init(libevdev *sourceDevice) {
    // Stamp the marker into the clone's name so device discovery can filter
    // the daemon's own clones by name even when the sysfs virtual-device
    // check cannot answer (hotplug race). As a PREFIX on purpose: the
    // kernel truncates long uinput names, and a truncated suffix would
    // silently disable the filter. The source handle's in-memory name is
    // restored right after; only the created clone keeps the stamp.
    const char *name = libevdev_get_name(sourceDevice);
    const std::string originalName = name != nullptr ? name : "";
    const std::string cloneName =
        std::string(kVirtualDeviceMarker) + ": " + originalName;
    libevdev_set_name(sourceDevice, cloneName.c_str());
    const int rc = libevdev_uinput_create_from_device(
        sourceDevice, LIBEVDEV_UINPUT_OPEN_MANAGED, &uinput_);
    libevdev_set_name(sourceDevice, originalName.c_str());
    return rc >= 0;
}

void UinputForwarder::forward(unsigned int type, unsigned int code, int value) {
    libevdev_uinput_write_event(uinput_, type, code, value);
}

void UinputForwarder::syn() { forward(EV_SYN, SYN_REPORT, 0); }

} // namespace schnelle_zeichen
