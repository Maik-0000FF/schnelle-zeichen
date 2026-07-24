// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_ENGINE_GESTURE_STATE_H
#define SCHNELLE_ZEICHEN_ENGINE_GESTURE_STATE_H

// The gesture state, ported from the legacy per-InputContext state.h. One
// instance suffices: there is one physical keyboard, and a focus change
// clears the gesture (the legacy deactivate semantics).
//
// Deliberately absent (collapsed by the clean three-state KeyAction signal
// of the raw backend): the entire synthetic-release machinery
// (waitingKeyTime_, sawSyntheticRelease_, issue #73/#92 timestamp checks),
// the Alt release-press-pair session (altGestureSession_, deferred cycling
// commit), heldRawCodes_ (Repeat is explicit now), the recentlyCommitted_
// channel-ordering guard and the deferred space commit (issue #90; commits
// and forwarded events are serialized by the backend contract).

#include "timer_port.h"

#include <cstdint>
#include <optional>
#include <string>

namespace schnelle_zeichen {

struct GestureState {
    // Waiting phase (mapped key held, before the first leader).
    std::optional<std::string> waitingKey;
    uint32_t waitingKeyCode = 0;
    uint64_t startUsec = 0;
    bool inputKeyPressed = false;

    // Cycling phase (after the first leader, while the key stays held).
    std::optional<std::string> cyclingInput;
    size_t cyclingIndex = 0;

    // Committed-key suppression: the raw code of a key whose press was
    // consumed and whose character was already committed. Its release is
    // always consumed (the app never saw the press). suppressRepeats
    // distinguishes the two legacy repeat semantics: true after a
    // single-output leader commit (repeats must NOT restart a gesture, the
    // "üu" guard), false after a window-timeout commit (repeats restart a
    // gesture, the historic one-char-per-window behavior).
    uint32_t committedCode = 0;
    bool committedSuppressRepeats = false;

    // Alt/AltGr press consumed as leader: consume its release too, so no
    // orphan modifier release reaches the app.
    uint32_t consumedAltCode = 0;

    // Timer slots (ids into the TimerPort). Separate slots for the same
    // reasons the legacy kept separate EventSourceTime members: they can be
    // pending concurrently.
    TimerPort::TimerId windowTimer = TimerPort::kInvalidTimer;
    TimerPort::TimerId overlayShowTimer = TimerPort::kInvalidTimer;
    TimerPort::TimerId overlayHideTimer = TimerPort::kInvalidTimer;
    TimerPort::TimerId autoSelectTimer = TimerPort::kInvalidTimer;

    void resetWaitingGesture() {
        waitingKey.reset();
        waitingKeyCode = 0;
        inputKeyPressed = false;
    }

    void resetCycling() {
        cyclingInput.reset();
        cyclingIndex = 0;
    }

    void armCommitted(uint32_t code, bool suppressRepeats) {
        committedCode = code;
        committedSuppressRepeats = suppressRepeats;
    }
    void clearCommitted() {
        committedCode = 0;
        committedSuppressRepeats = false;
    }

    bool gestureActive() const {
        return waitingKey.has_value() || cyclingInput.has_value();
    }
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_ENGINE_GESTURE_STATE_H
