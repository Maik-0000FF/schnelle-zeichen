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

namespace schnelle_zeichen {

class AtspiFocusSource : public FocusSource {
public:
    AtspiFocusSource() = default;
    ~AtspiFocusSource() override;
    AtspiFocusSource(const AtspiFocusSource &) = delete;
    AtspiFocusSource &operator=(const AtspiFocusSource &) = delete;

    // Connect to the a11y bus and install the signal matches (but register no
    // events yet: setActive does that). Returns false (and stays inert) when
    // accessibility is off or the bus is absent, so the caller simply never
    // gets a caret rect. Call once; the fd then lives in the epoll set for the
    // process lifetime regardless of the active state.
    bool init();

    // Register (active) or deregister (inactive) the AT-SPI caret/focus events
    // with the registry, gating whether apps emit them to us at all. Toggled by
    // the caller when the overlay placement enters/leaves TextCaret, so a
    // non-caret placement pays no a11y traffic. Deactivating clears the cache
    // and the focus state.
    // Activation can fail (the registry call errored): it then stays inactive
    // and current() reports no caret, so a wired overlay simply uses its
    // pointer/grid fallback until a later call (a reload) retries.
    void setActive(bool active);

    // epoll integration, mirroring ControlService.
    int fd() const;
    void process();

    // FocusSource: the latest cached caret snapshot (cheap, no bus traffic).
    FocusInfo current() override;

    // Signal and reply handlers. Public so the C trampolines can dispatch to
    // them; not part of the FocusSource contract.
    int onCaretMoved(sd_bus_message *m);
    int onFocusChanged(sd_bus_message *m);
    int onExtentsReply(sd_bus_message *reply);
    int onCaretOffsetReply(sd_bus_message *reply);

private:
    // Start an ASYNC GetCharacterExtents on the event's source object. The
    // reply lands via process() on the epoll pump, so the input loop never
    // blocks on the target app. Coalesced: at most one query is in flight; a
    // caret-move arriving meanwhile is remembered and fired once the current
    // reply lands.
    void startQuery(const char *busName, const char *path, int offset);

    // Register or deregister the caret/focus events with the registry (used by
    // setActive); enabling is transactional (rolls back a partial success).
    bool registerEvents(bool enable);
    // A single RegisterEvent/DeregisterEvent call, false on failure.
    bool registerOne(const char *event, bool enable);

    sd_bus *bus_ = nullptr;
    sd_bus_slot *caretSlot_ = nullptr;
    sd_bus_slot *focusSlot_ = nullptr;
    sd_bus_slot *querySlot_ = nullptr;
    sd_bus_slot *offsetSlot_ = nullptr;
    bool active_ = false;
    bool queryInFlight_ = false;
    bool hasPending_ = false;
    std::string pendingBus_;
    std::string pendingPath_;
    int pendingOffset_ = 0;
    // The object that last took focus: the offset reply issues its extents
    // query against it, and its bus name filters incoming carets down to the
    // focused application. Cleared on deactivation with the rest of the focus
    // state.
    std::string focusBus_;
    std::string focusPath_;
    // Set when a caret-moved arrives after a focus; makes the (slower) focus
    // offset reply defer to the fresher caret instead of dragging it back.
    bool caretMovedSinceFocus_ = false;
    FocusInfo cached_;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_ATSPI_FOCUS_SOURCE_H
