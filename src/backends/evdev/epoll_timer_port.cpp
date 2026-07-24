// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "epoll_timer_port.h"

#include <unistd.h>
#include <sys/timerfd.h>

#include <ctime>
#include <utility>
#include <vector>

namespace schnelle_zeichen {

namespace {
constexpr uint64_t kUsecPerSec = 1'000'000;
constexpr uint64_t kNsecPerUsec = 1'000;
} // namespace

EpollTimerPort::EpollTimerPort() {
    timerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
}

EpollTimerPort::~EpollTimerPort() {
    if (timerFd_ >= 0) {
        close(timerFd_);
    }
}

uint64_t EpollTimerPort::nowUsec() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * kUsecPerSec +
           static_cast<uint64_t>(ts.tv_nsec) / kNsecPerUsec;
}

TimerPort::TimerId EpollTimerPort::schedule(uint64_t delayUsec,
                                            Callback callback) {
    const TimerId id = nextId_++;
    pending_[id] = {nowUsec() + delayUsec, std::move(callback)};
    rearm();
    return id;
}

void EpollTimerPort::cancel(TimerId id) {
    pending_.erase(id);
    rearm();
}

void EpollTimerPort::rearm() {
    itimerspec ts{};
    if (!pending_.empty()) {
        uint64_t earliest = UINT64_MAX;
        for (const auto &kv : pending_) {
            if (kv.second.dueUsec < earliest) {
                earliest = kv.second.dueUsec;
            }
        }
        const uint64_t now = nowUsec();
        // An already-due deadline still needs a nonzero arming (zero would
        // disarm the fd); one microsecond fires immediately.
        const uint64_t in = earliest > now ? earliest - now : 1;
        ts.it_value.tv_sec = static_cast<time_t>(in / kUsecPerSec);
        ts.it_value.tv_nsec =
            static_cast<long>((in % kUsecPerSec) * kNsecPerUsec);
    }
    timerfd_settime(timerFd_, 0, &ts, nullptr);
}

void EpollTimerPort::dispatch() {
    uint64_t expirations = 0;
    [[maybe_unused]] const ssize_t n =
        read(timerFd_, &expirations, sizeof(expirations));
    const uint64_t now = nowUsec();
    // Collect due callbacks first: a callback may schedule or cancel timers,
    // which mutates pending_.
    std::vector<Callback> due;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second.dueUsec <= now) {
            due.push_back(std::move(it->second.callback));
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto &cb : due) {
        cb();
    }
    rearm();
}

} // namespace schnelle_zeichen
