// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Regression guard for the self-grab name rule: a device whose name carries
// the clone marker must never count as an eligible keyboard. Pure name
// logic, no device fd involved, so the safety-relevant rule is pinned even
// though isEligibleKeyboard itself needs real hardware.

#include "check.h"
#include "device_discovery.h"

#include <cstdio>
#include <string>

using namespace schnelle_zeichen;

int main() {
    // The stamped clone-name format (marker prefix, uinput_forwarder.cpp).
    const std::string stamped =
        std::string(kVirtualDeviceMarker) + ": AT Translated Set 2 keyboard";
    CHECK(hasVirtualDeviceMarker(stamped));
    // The prefix placement must survive the kernel's uinput name
    // truncation: any truncated tail of the stamped name still matches.
    CHECK(hasVirtualDeviceMarker(std::string_view(stamped).substr(
        0, std::string(kVirtualDeviceMarker).size())));
    // Marker anywhere in the name counts (substring rule).
    CHECK(hasVirtualDeviceMarker("virtual schnelle-zeichen clone"));
    CHECK(hasVirtualDeviceMarker(kVirtualDeviceMarker));

    // Real keyboards and other virtual devices never match.
    CHECK(!hasVirtualDeviceMarker("AT Translated Set 2 keyboard"));
    CHECK(!hasVirtualDeviceMarker("Logitech MX Keys"));
    CHECK(!hasVirtualDeviceMarker("Solaar virtual input"));
    CHECK(!hasVirtualDeviceMarker(""));
    // Near-misses stay ineligible for the filter: the rule is the exact
    // marker substring, not fuzzy matching.
    CHECK(!hasVirtualDeviceMarker("schnelle zeichen"));
    CHECK(!hasVirtualDeviceMarker("schnelle-zeiche"));

    if (failures == 0) {
        std::printf("device_discovery_test: all checks passed\n");
        return 0;
    }
    std::printf("device_discovery_test: %d check(s) failed\n", failures);
    return 1;
}
