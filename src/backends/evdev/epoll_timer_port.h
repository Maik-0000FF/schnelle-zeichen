// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKENDS_EVDEV_EPOLL_TIMER_PORT_H
#define SCHNELLE_ZEICHEN_BACKENDS_EVDEV_EPOLL_TIMER_PORT_H

// TimerPort implementation over one timerfd in the daemon's epoll loop: a
// min-heap of deadlines, the timerfd always armed to the earliest one. The
// main loop calls dispatch() when the fd turns readable.

#include "timer_port.h"

#include <cstdint>
#include <map>

namespace schnelle_zeichen {

class EpollTimerPort : public TimerPort {
public:
    EpollTimerPort();
    ~EpollTimerPort() override;
    EpollTimerPort(const EpollTimerPort &) = delete;
    EpollTimerPort &operator=(const EpollTimerPort &) = delete;

    uint64_t nowUsec() override;
    TimerId schedule(uint64_t delayUsec, Callback callback) override;
    void cancel(TimerId id) override;

    // The fd to register with epoll (EPOLLIN).
    int fd() const { return timerFd_; }
    // Fire every due callback; re-arms the fd to the next deadline.
    void dispatch();

private:
    struct Pending {
        uint64_t dueUsec = 0;
        Callback callback;
    };
    void rearm();

    int timerFd_ = -1;
    TimerId nextId_ = 1;
    std::map<TimerId, Pending> pending_;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_EPOLL_TIMER_PORT_H
