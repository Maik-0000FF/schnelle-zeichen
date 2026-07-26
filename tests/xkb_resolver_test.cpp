// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// XkbResolver state tests: modifier tracking from raw key transitions and
// the grab-time lock seeding (LED sync must flip a stale lock exactly once
// and stay idempotent across several keyboards). Needs a compilable system
// keymap (xkeyboard-config); the test skips itself where none is available
// (exit 77, wired as SKIP_RETURN_CODE).

#include "KeySource.h" // KeyModifier
#include "check.h"
#include "xkb_resolver.h"

#include <linux/input.h>

#include <cstdio>
#include <string>

using namespace schnelle_zeichen;

namespace {

constexpr int kSkipExitCode = 77;

bool capsLocked(const XkbResolver &r) {
    return (r.modifierMask() & KeyModifier::CapsLock) != 0;
}

} // namespace

int main() {
    XkbResolver resolver;
    if (!resolver.init("us")) {
        std::printf("SKIP: no compilable xkb keymap in this environment\n");
        return kSkipExitCode;
    }

    // Plain key: no modifiers, lowercase text.
    CHECK(resolver.modifierMask() == 0);
    CHECK(resolver.text(KEY_A) == "a");

    // Held Shift resolves the next key under it (press-before-resolve
    // convention lives in the caller; state-wise Shift is active while
    // down).
    resolver.updateKey(KEY_LEFTSHIFT, true);
    CHECK((resolver.modifierMask() & KeyModifier::Shift) != 0);
    CHECK(resolver.text(KEY_A) == "A");
    resolver.updateKey(KEY_LEFTSHIFT, false);
    CHECK(resolver.modifierMask() == 0);
    CHECK(resolver.text(KEY_A) == "a");

    // LED seeding: LED on + fresh state flips the lock exactly once; the
    // same sync from a second keyboard is a no-op (idempotent).
    CHECK(!capsLocked(resolver));
    resolver.syncLockedModFromLed(XKB_MOD_NAME_CAPS, KEY_CAPSLOCK, true);
    CHECK(capsLocked(resolver));
    CHECK(resolver.text(KEY_A) == "A");
    resolver.syncLockedModFromLed(XKB_MOD_NAME_CAPS, KEY_CAPSLOCK, true);
    CHECK(capsLocked(resolver));

    // A real CapsLock press+release afterwards unlocks again: the seeded
    // state continues to track reality instead of double-counting.
    resolver.updateKey(KEY_CAPSLOCK, true);
    resolver.updateKey(KEY_CAPSLOCK, false);
    CHECK(!capsLocked(resolver));
    CHECK(resolver.text(KEY_A) == "a");

    // LED-off sync against an already-clear state stays a no-op.
    resolver.syncLockedModFromLed(XKB_MOD_NAME_CAPS, KEY_CAPSLOCK, false);
    CHECK(!capsLocked(resolver));

    // Replayed (SYNC-path) transitions drive the same state updates: a
    // modifier press lost to the engine still lands in the resolver.
    resolver.updateKey(KEY_RIGHTSHIFT, true);
    CHECK(resolver.text(KEY_A) == "A");
    resolver.updateKey(KEY_RIGHTSHIFT, false);

    // Re-init (config reload with a changed layout) carries the lock state
    // over: the grabbed devices' LEDs are no longer a truthful source, so
    // the old state must survive the keymap swap.
    resolver.syncLockedModFromLed(XKB_MOD_NAME_CAPS, KEY_CAPSLOCK, true);
    CHECK(capsLocked(resolver));
    CHECK(resolver.init("us"));
    CHECK(capsLocked(resolver));
    CHECK(resolver.text(KEY_A) == "A");

    // A broken layout must not compile, and the failed re-init keeps the
    // previous resolver fully working (swap-on-success), lock included.
    CHECK(!resolver.init("definitely-not-a-layout"));
    CHECK(capsLocked(resolver));
    CHECK(resolver.text(KEY_A) == "A");

    if (failures == 0) {
        std::printf("xkb_resolver_test: all checks passed\n");
        return 0;
    }
    std::printf("xkb_resolver_test: %d check(s) failed\n", failures);
    return 1;
}
