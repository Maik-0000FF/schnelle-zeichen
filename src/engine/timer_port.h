// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_ENGINE_TIMER_PORT_H
#define SCHNELLE_ZEICHEN_ENGINE_TIMER_PORT_H

// Minimal timer/clock abstraction the engine schedules against, replacing
// fcitx's EventSourceTime. Keeping the engine event-loop-free makes the
// whole state machine unit-testable with a fake clock (advance time, fire
// due timers, assert commits) and lets each backend adapt its own loop
// (epoll timerfd on Linux, CFRunLoop on macOS).

#include <cstdint>
#include <functional>

namespace schnelle_zeichen {

class TimerPort {
public:
    using TimerId = uint64_t;
    using Callback = std::function<void()>;

    static constexpr TimerId kInvalidTimer = 0;

    virtual ~TimerPort() = default;

    // Monotonic microseconds (the engine's only clock).
    virtual uint64_t nowUsec() = 0;

    // One-shot timer; the returned id stays valid until fired or cancelled.
    virtual TimerId schedule(uint64_t delayUsec, Callback callback) = 0;

    // Cancel a pending timer; cancelling a fired or invalid id is a no-op.
    virtual void cancel(TimerId id) = 0;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_ENGINE_TIMER_PORT_H
