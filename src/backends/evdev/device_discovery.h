// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKENDS_EVDEV_DEVICE_DISCOVERY_H
#define SCHNELLE_ZEICHEN_BACKENDS_EVDEV_DEVICE_DISCOVERY_H

// Keyboard discovery under /dev/input: every event device that looks like a
// real keyboard (EV_KEY with a letter range and Space), excluding our own
// uinput clones so the daemon never grabs itself.

#include <string>
#include <string_view>
#include <vector>

namespace schnelle_zeichen {

// Substring stamped into the uinput clone's name and filtered here.
inline constexpr const char *kVirtualDeviceMarker = "schnelle-zeichen";

// The evdev device directory and the name prefix of its event nodes,
// shared by discovery, the daemon's hotplug watch and its CLI device-path
// detection so the literals live in one place.
inline constexpr const char *kInputDevDir = "/dev/input";
inline constexpr std::string_view kEventNodePrefix = "event";
inline constexpr std::string_view kDevPathPrefix = "/dev/";

struct DiscoveredKeyboard {
    std::string path; // /dev/input/eventN
    std::string name; // kernel device name
};

std::vector<DiscoveredKeyboard> discoverKeyboards();

// Single-device eligibility check (used by discovery and by hotplug when a
// new /dev/input node appears): a real, physical keyboard that is not one
// of our own clones. Fills nameOut on success.
bool isEligibleKeyboard(const std::string &devPath, std::string *nameOut);

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_DEVICE_DISCOVERY_H
