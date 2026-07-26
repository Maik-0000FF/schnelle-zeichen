// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKENDS_EVDEV_ATSPI_FOCUS_SOURCE_H
#define SCHNELLE_ZEICHEN_BACKENDS_EVDEV_ATSPI_FOCUS_SOURCE_H

// FocusSource backed by the AT-SPI accessibility bus, spoken directly over
// sd-bus (no libatspi / GLib): the engine's epoll loop owns the a11y bus fd and
// pumps it via process(), exactly like ControlService. It follows the focused
// text object's caret by registering AT-SPI caret-moved and focus listeners on
// the registry and querying GetCharacterExtents in SCREEN coordinates on each
// caret-moved, caching the latest usable rect (rect-validity rule in
// caret_overlay_geometry.h). current() returns that cache cheaply; when
// accessibility is off, the bus is absent, or the focused widget exposes no
// caret, hasCaretRect stays false and the caller falls back (grid/pointer).
//
// Single-threaded: every mutation happens inside process() on the engine's
// epoll thread, and current() is read from the same thread, so the cache needs
// no locking.

#include "FocusSource.h"

#include <string>

struct sd_bus;
struct sd_bus_slot;
struct sd_bus_message;
struct sd_bus_error;

namespace schnelle_zeichen {

class AtspiFocusSource : public FocusSource {
public:
    AtspiFocusSource() = default;
    ~AtspiFocusSource() override;
    AtspiFocusSource(const AtspiFocusSource &) = delete;
    AtspiFocusSource &operator=(const AtspiFocusSource &) = delete;

    // Connect to the a11y bus and register the caret/focus listeners. Returns
    // false (and stays inert) when accessibility is off or the bus is absent,
    // so the caller simply never gets a caret rect.
    bool init();

    // epoll integration, mirroring ControlService.
    int fd() const;
    void process();

    // FocusSource: the latest cached caret snapshot (cheap, no bus traffic).
    FocusInfo current() override;

    // Signal and reply handlers. Public so the C trampolines can dispatch to
    // them; not part of the FocusSource contract.
    int onCaretMoved(sd_bus_message *m);
    int onFocusChanged(sd_bus_message *m);
    int onExtentsReply(sd_bus_message *reply, sd_bus_error *err);

private:
    // Start an ASYNC GetCharacterExtents on the event's source object. The
    // reply lands via process() on the epoll pump, so the input loop never
    // blocks on the target app. Coalesced: at most one query is in flight; a
    // caret-move arriving meanwhile is remembered and fired once the current
    // reply lands.
    void startQuery(const char *busName, const char *path, int offset);

    sd_bus *bus_ = nullptr;
    sd_bus_slot *caretSlot_ = nullptr;
    sd_bus_slot *focusSlot_ = nullptr;
    sd_bus_slot *querySlot_ = nullptr;
    bool queryInFlight_ = false;
    bool hasPending_ = false;
    std::string pendingBus_;
    std::string pendingPath_;
    int pendingOffset_ = 0;
    FocusInfo cached_;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_ATSPI_FOCUS_SOURCE_H
