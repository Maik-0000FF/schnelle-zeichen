// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKENDS_EVDEV_DEVICE_DISCOVERY_H
#define SCHNELLE_ZEICHEN_BACKENDS_EVDEV_DEVICE_DISCOVERY_H

// Keyboard discovery under /dev/input: every event device that looks like a
// real keyboard (EV_KEY with a letter range and Space), excluding our own
// uinput clones so the daemon never grabs itself.

#include <string>
#include <vector>

namespace schnelle_zeichen {

// Substring stamped into the uinput clone's name and filtered here.
inline constexpr const char *kVirtualDeviceMarker = "schnelle-zeichen";

struct DiscoveredKeyboard {
    std::string path; // /dev/input/eventN
    std::string name; // kernel device name
};

std::vector<DiscoveredKeyboard> discoverKeyboards();

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_DEVICE_DISCOVERY_H
