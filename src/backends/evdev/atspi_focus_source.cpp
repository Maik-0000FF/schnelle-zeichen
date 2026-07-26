// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "atspi_focus_source.h"

#include "caret_overlay_geometry.h" // CaretRect, isUsableCaretRect
#include "log.h"

#include <systemd/sd-bus.h>

#include <cstring>
#include <string>

namespace schnelle_zeichen {

namespace {

constexpr char kEventInterface[] = "org.a11y.atspi.Event.Object";
constexpr char kTextInterface[] = "org.a11y.atspi.Text";
constexpr char kRegistryService[] = "org.a11y.atspi.Registry";
constexpr char kRegistryPath[] = "/org/a11y/atspi/registry";
constexpr char kRegistryInterface[] = "org.a11y.atspi.Registry";

// AT-SPI event names to register (the registry suppresses events no client
// asked for, so apps only emit these once we register them).
constexpr char kEventCaretMoved[] = "object:text-caret-moved";
constexpr char kEventFocused[] = "object:state-changed:focused";

// GetCharacterExtents coordinate type: 0 = ATSPI_COORD_TYPE_SCREEN.
constexpr uint32_t kCoordScreen = 0;
// Bound on every a11y call so a hung target app can never stall the engine's
// epoll loop for long.
constexpr uint64_t kCallTimeoutUsec = 100'000; // 100 ms

int caretTrampoline(sd_bus_message *m, void *userdata, sd_bus_error *) {
    return static_cast<AtspiFocusSource *>(userdata)->onCaretMoved(m);
}

int focusTrampoline(sd_bus_message *m, void *userdata, sd_bus_error *) {
    return static_cast<AtspiFocusSource *>(userdata)->onFocusChanged(m);
}

int extentsReplyTrampoline(sd_bus_message *reply, void *userdata,
                           sd_bus_error *) {
    return static_cast<AtspiFocusSource *>(userdata)->onExtentsReply(reply);
}

// The a11y bus address, published by org.a11y.Bus on the session bus. Empty
// when accessibility is unavailable.
std::string a11yBusAddress() {
    sd_bus *session = nullptr;
    if (sd_bus_open_user(&session) < 0) {
        return {};
    }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = nullptr;
    std::string addr;
    if (sd_bus_call_method(session, "org.a11y.Bus", "/org/a11y/bus",
                           "org.a11y.Bus", "GetAddress", &err, &reply,
                           "") >= 0) {
        const char *s = nullptr;
        if (sd_bus_message_read(reply, "s", &s) >= 0 && s != nullptr) {
            addr = s;
        }
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_flush(session);
    sd_bus_unref(session);
    return addr;
}

// Read an AT-SPI event. The body signature is "siiva{sv}": the sub-type string,
// detail1, detail2, an any_data variant, and a properties dict, none of which
// name the source. The source accessible is instead the D-Bus SENDER and object
// PATH of the signal itself. Returns detail1 (the caret offset for a
// caret-moved) plus those source references, valid for the lifetime of `m`.
// False on a malformed body or a signal without a sender/path.
bool readEventSource(sd_bus_message *m, int &detail1, const char *&busName,
                     const char *&path) {
    const char *sub = nullptr;
    int detail2 = 0;
    if (sd_bus_message_read(m, "sii", &sub, &detail1, &detail2) < 0) {
        return false;
    }
    busName = sd_bus_message_get_sender(m);
    path = sd_bus_message_get_path(m);
    return busName != nullptr && path != nullptr;
}

} // namespace

AtspiFocusSource::~AtspiFocusSource() {
    // Unref the outstanding async query first: cancels a reply into a
    // half-destroyed object.
    if (querySlot_ != nullptr) {
        sd_bus_slot_unref(querySlot_);
    }
    if (caretSlot_ != nullptr) {
        sd_bus_slot_unref(caretSlot_);
    }
    if (focusSlot_ != nullptr) {
        sd_bus_slot_unref(focusSlot_);
    }
    if (bus_ != nullptr) {
        sd_bus_flush(bus_);
        sd_bus_unref(bus_);
    }
}

bool AtspiFocusSource::init() {
    const std::string addr = a11yBusAddress();
    if (addr.empty()) {
        warn("caret: accessibility bus unavailable; caret placement inactive");
        return false;
    }
    if (sd_bus_new(&bus_) < 0) {
        bus_ = nullptr;
        return false;
    }
    sd_bus_set_address(bus_, addr.c_str());
    sd_bus_set_bus_client(bus_, 1);
    if (sd_bus_start(bus_) < 0) {
        warn("caret: a11y bus connect failed; caret placement inactive");
        sd_bus_unref(bus_);
        bus_ = nullptr;
        return false;
    }

    // Install the signal matches now; the events themselves are registered
    // with the registry lazily by setActive, so a non-caret placement causes
    // no a11y traffic. Any sender/path.
    sd_bus_match_signal(bus_, &caretSlot_, nullptr, nullptr, kEventInterface,
                        "TextCaretMoved", caretTrampoline, this);
    sd_bus_match_signal(bus_, &focusSlot_, nullptr, nullptr, kEventInterface,
                        "StateChanged", focusTrampoline, this);
    return true;
}

// RegisterEvent(s event, as properties, s app_bus_name) enables an event on the
// registry; DeregisterEvent(s event, s app_bus_name) disables it. Empty
// properties and app name = every app.
bool AtspiFocusSource::registerEvents(bool enable) {
    bool ok = true;
    for (const char *ev : {kEventCaretMoved, kEventFocused}) {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message *call = nullptr;
        int rc = sd_bus_message_new_method_call(
            bus_, &call, kRegistryService, kRegistryPath, kRegistryInterface,
            enable ? "RegisterEvent" : "DeregisterEvent");
        if (rc >= 0) {
            rc = sd_bus_message_append(call, "s", ev);
        }
        if (enable && rc >= 0) {
            rc = sd_bus_message_append(call, "as", 0); // properties (register)
        }
        if (rc >= 0) {
            rc = sd_bus_message_append(call, "s", ""); // every app
        }
        if (rc >= 0) {
            rc = sd_bus_call(bus_, call, kCallTimeoutUsec, &err, nullptr);
        }
        if (rc < 0) {
            warn(std::string("caret: ") +
                 (enable ? "RegisterEvent(" : "DeregisterEvent(") + ev +
                 ") failed: " +
                 (err.message != nullptr ? err.message : strerror(-rc)));
            ok = false;
        }
        sd_bus_message_unref(call);
        sd_bus_error_free(&err);
    }
    return ok;
}

void AtspiFocusSource::setActive(bool active) {
    if (bus_ == nullptr || active == active_) {
        return;
    }
    active_ = active;
    registerEvents(active);
    if (!active) {
        // Leaving caret mode: drop the cache so a later re-entry never shows a
        // stale rect from before.
        cached_ = FocusInfo{};
    }
}

int AtspiFocusSource::fd() const {
    return bus_ != nullptr ? sd_bus_get_fd(bus_) : -1;
}

void AtspiFocusSource::process() {
    if (bus_ == nullptr) {
        return;
    }
    while (sd_bus_process(bus_, nullptr) > 0) {
    }
}

FocusInfo AtspiFocusSource::current() { return cached_; }

void AtspiFocusSource::startQuery(const char *busName, const char *path,
                                  int offset) {
    if (queryInFlight_) {
        // Coalesce: remember only the latest caret; it fires when the current
        // reply lands, so rapid typing never piles up more than one pending
        // query. The refs are copied because the event message they came from
        // is freed after this call returns.
        pendingBus_ = busName;
        pendingPath_ = path;
        pendingOffset_ = offset;
        hasPending_ = true;
        return;
    }
    sd_bus_message *call = nullptr;
    int rc = sd_bus_message_new_method_call(
        bus_, &call, busName, path, kTextInterface, "GetCharacterExtents");
    if (rc >= 0) {
        rc = sd_bus_message_append(call, "iu", offset, kCoordScreen);
    }
    if (rc >= 0 &&
        sd_bus_call_async(bus_, &querySlot_, call, extentsReplyTrampoline, this,
                          kCallTimeoutUsec) >= 0) {
        queryInFlight_ = true;
    }
    sd_bus_message_unref(call);
}

int AtspiFocusSource::onExtentsReply(sd_bus_message *reply) {
    queryInFlight_ = false;
    querySlot_ = sd_bus_slot_unref(querySlot_);

    // A source without the Text interface (a non-text focus) or a vanished app
    // replies with an error; keep the last cache. Only a usable rect replaces
    // it.
    if (sd_bus_message_is_method_error(reply, nullptr) == 0) {
        CaretRect r;
        if (sd_bus_message_read(reply, "iiii", &r.x, &r.y, &r.w, &r.h) >= 0 &&
            isUsableCaretRect(r)) {
            cached_.hasCaretRect = true;
            cached_.caretX = r.x;
            cached_.caretY = r.y;
            cached_.caretW = r.w;
            cached_.caretH = r.h;
        }
    }

    // Fire the caret coalesced while this query was in flight, if any.
    if (hasPending_) {
        hasPending_ = false;
        startQuery(pendingBus_.c_str(), pendingPath_.c_str(), pendingOffset_);
    }
    return 0;
}

int AtspiFocusSource::onCaretMoved(sd_bus_message *m) {
    int offset = 0;
    const char *busName = nullptr;
    const char *path = nullptr;
    if (readEventSource(m, offset, busName, path) && busName != nullptr &&
        path != nullptr) {
        startQuery(busName, path, offset);
    }
    return 0;
}

int AtspiFocusSource::onFocusChanged(sd_bus_message *m) {
    // StateChanged carries the state name in the sub-type string and
    // gained/lost in detail1. Only a "focused" gain matters: it drops the stale
    // caret so switching to a caret-less app (a terminal) falls back to the
    // grid instead of reusing the previous app's caret. The next caret-moved
    // repopulates the cache.
    const char *sub = nullptr;
    int detail1 = 0;
    int detail2 = 0;
    if (sd_bus_message_read(m, "sii", &sub, &detail1, &detail2) < 0) {
        return 0;
    }
    if (sub != nullptr && std::strcmp(sub, "focused") == 0 && detail1 == 1) {
        // New focus: drop the previous app's caret, then ask the freshly
        // focused object for its extents at offset 0. A text field answers with
        // a field-level rect (the fallback tier above the mouse pointer for
        // apps that expose no per-character caret, e.g. a browser address bar);
        // a caret-less widget errors out, leaving the cache empty for the
        // pointer/ grid fallback. A later caret-moved refines it to the exact
        // caret.
        cached_ = FocusInfo{};
        const char *busName = sd_bus_message_get_sender(m);
        const char *path = sd_bus_message_get_path(m);
        if (busName != nullptr && path != nullptr) {
            startQuery(busName, path, 0);
        }
    }
    return 0;
}

} // namespace schnelle_zeichen
