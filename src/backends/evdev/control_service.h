// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKEND_EVDEV_CONTROL_SERVICE_H
#define SCHNELLE_ZEICHEN_BACKEND_EVDEV_CONTROL_SERVICE_H

// The engine's session-bus control surface (de.schnelle_zeichen.Engine1):
// Pause/Resume/Toggle/GetPaused/Quit plus the PausedChanged signal, consumed
// by the tray (and bindable to an OS shortcut via a one-shot busctl call).
// See core/control_protocol.h for the wire contract.
//
// The service owns no policy: it forwards state changes to the main loop
// through the two callbacks and mirrors the authoritative paused flag the
// main loop pushes back via setPaused(). Event-loop integration is a plain
// fd (epoll) + process() pump, like the other backend pieces.

#include <functional>

struct sd_bus;

namespace schnelle_zeichen {

class ControlService {
public:
    ControlService() = default;
    ~ControlService();
    ControlService(const ControlService &) = delete;
    ControlService &operator=(const ControlService &) = delete;

    // Connect to the session bus and claim the service name. Returns false
    // (service unavailable, engine keeps running) when the bus or the name
    // is not obtainable.
    bool init();
    bool connected() const { return bus_ != nullptr; }

    // Bus fd for epoll (EPOLLIN); -1 when not connected.
    int fd() const;
    // Dispatch pending bus messages; call on fd readability (and cheap to
    // call on idle ticks).
    void process();

    // Authoritative pause state, pushed by the main loop (which also owns
    // the shortcut toggle path). Emits PausedChanged on a change.
    void setPaused(bool paused);
    bool paused() const { return paused_; }

    // Main-loop hooks. onPauseRequested receives the requested state; the
    // main loop applies it and pushes the result back via setPaused().
    std::function<void(bool)> onPauseRequested;
    std::function<void()> onQuitRequested;

private:
    friend struct ControlServiceVtable;
    void emitPausedChanged();

    sd_bus *bus_ = nullptr;
    bool paused_ = false;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKEND_EVDEV_CONTROL_SERVICE_H
