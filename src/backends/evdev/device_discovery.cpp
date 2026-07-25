// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device_discovery.h"

#include <fcntl.h>
#include <unistd.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>

#include <algorithm>
#include <charconv>
#include <climits>
#include <filesystem>
#include <string>

namespace schnelle_zeichen {

namespace {

// A keyboard for our purposes: delivers key events, has the letter block
// and Space. Filters out mice with a few side buttons and consumer-control
// devices.
bool looksLikeKeyboard(libevdev *dev) {
    if (libevdev_has_event_type(dev, EV_KEY) == 0) {
        return false;
    }
    return libevdev_has_event_code(dev, EV_KEY, KEY_A) != 0 &&
           libevdev_has_event_code(dev, EV_KEY, KEY_Z) != 0 &&
           libevdev_has_event_code(dev, EV_KEY, KEY_SPACE) != 0;
}

// The authoritative virtual-device check, the same rule udev applies:
// software keyboards (uinput clones, Solaar, remappers) live under
// /sys/devices/virtual/, real ones under the PCI/USB/platform tree. Name
// and phys cannot be trusted for FOREIGN devices (Solaar fakes a USB bus id
// and a phys path); the daemon's own clones carry kVirtualDeviceMarker in
// their name (stamped in uinput_forwarder.cpp), so the name filter in
// isEligibleKeyboard guards against self-grab even when this check cannot
// answer.
bool isVirtualDevice(const std::string &devPath) {
    const std::string node = std::filesystem::path(devPath).filename().string();
    std::error_code ec;
    const std::filesystem::path sys =
        std::filesystem::canonical("/sys/class/input/" + node + "/device", ec);
    if (ec) {
        // Unknown: treat as physical rather than skip it. Failing open here
        // is safe for the catastrophic case (grabbing the own clone in an
        // event-feedback loop) because the marker filter catches that by
        // name; a foreign virtual device slipping through on a sysfs
        // hiccup merely gets grabbed like a keyboard.
        return false;
    }
    return sys.string().find("/devices/virtual/") != std::string::npos;
}

// Numeric event-node order, so the primary keyboard (low event number, the
// device the kernel registered first) wins over later HID collections.
int eventNumber(const std::string &devPath) {
    const std::string node = std::filesystem::path(devPath).filename().string();
    if (node.size() <= kEventNodePrefix.size()) {
        return INT_MAX;
    }
    // from_chars leaves the value untouched on a non-numeric suffix, so a
    // malformed node sorts last (INT_MAX) just like the short-node case above.
    int number = INT_MAX;
    const char *first = node.c_str() + kEventNodePrefix.size();
    std::from_chars(first, node.c_str() + node.size(), number);
    return number;
}

} // namespace

bool isEligibleKeyboard(const std::string &devPath, std::string *nameOut) {
    const int fd = open(devPath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    libevdev *dev = nullptr;
    if (libevdev_new_from_fd(fd, &dev) < 0) {
        close(fd);
        return false;
    }
    const char *name = libevdev_get_name(dev);
    const std::string deviceName = name != nullptr ? name : "";
    const bool eligible =
        looksLikeKeyboard(dev) && !isVirtualDevice(devPath) &&
        deviceName.find(kVirtualDeviceMarker) == std::string::npos;
    libevdev_free(dev);
    close(fd);
    if (eligible && nameOut != nullptr) {
        *nameOut = deviceName;
    }
    return eligible;
}

std::vector<DiscoveredKeyboard> discoverKeyboards() {
    std::vector<DiscoveredKeyboard> found;
    std::error_code ec;
    for (const auto &entry :
         std::filesystem::directory_iterator(kInputDevDir, ec)) {
        const std::string path = entry.path().string();
        const std::string node = entry.path().filename().string();
        if (node.compare(0, kEventNodePrefix.size(), kEventNodePrefix) != 0) {
            continue;
        }
        std::string name;
        if (isEligibleKeyboard(path, &name)) {
            found.push_back({path, name});
        }
    }
    std::sort(found.begin(), found.end(),
              [](const DiscoveredKeyboard &a, const DiscoveredKeyboard &b) {
                  return eventNumber(a.path) < eventNumber(b.path);
              });
    return found;
}

} // namespace schnelle_zeichen
