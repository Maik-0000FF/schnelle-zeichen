// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlay_dbus_client.h"

#include "cursor_overlay_geometry.h" // cursorPositionPrefix
#include "log.h"
#include "overlay_protocol.h"

#include <systemd/sd-bus.h>

#include <cstdlib>
#include <cstring>
#include <utility>

namespace schnelle_zeichen {

namespace {
// Timeout for the synchronous version handshake; off the hot path (once per
// enable transition) and only when a daemon already owns the name.
constexpr uint64_t kHandshakeTimeoutUsec = 200'000;

const char *rowName(OverlayRow row) {
    switch (row) {
    case OverlayRow::Center:
        return "Center";
    case OverlayRow::Bottom:
        return "Bottom";
    case OverlayRow::Top:
    default:
        return "Top";
    }
}
} // namespace

namespace {
void reportSendErrorImpl(int rc) {
    static bool reported = false;
    if (rc < 0 && !reported) {
        reported = true;
        warn(std::string("overlay: send failed: ") + strerror(-rc));
    }
}
} // namespace

std::string overlayPositionString(const OverlayConfig &overlay) {
    std::string position;
    if (overlay.placement == OverlayPlacement::MouseCursor) {
        position = cursorPositionPrefix();
    }
    position += rowName(overlay.row);
    position += "Col" + std::to_string(static_cast<int>(overlay.column) + 1);
    return position;
}

OverlayDBusClient::OverlayDBusClient()
    : capability_(detectLayerShellCapability()) {
    if (!capability_.supported) {
        warn("overlay disabled, session " + capability_.session + ": " +
             capability_.reason);
        return;
    }
    int rc = sd_bus_open_user(&bus_);
    if (rc < 0) {
        // Privileged context: sd_bus_open_user's own env/uid resolution can
        // refuse under sudo even though the user's bus address was passed.
        // Connect to that address directly instead.
        bus_ = nullptr;
        const char *address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
        std::string fallback;
        if (address == nullptr || *address == '\0') {
            const char *runtime = std::getenv("XDG_RUNTIME_DIR");
            if (runtime != nullptr && *runtime != '\0') {
                fallback = std::string("unix:path=") + runtime + "/bus";
                address = fallback.c_str();
            }
        }
        if (address != nullptr) {
            rc = sd_bus_new(&bus_);
            if (rc >= 0) {
                rc = sd_bus_set_address(bus_, address);
            }
            if (rc >= 0) {
                rc = sd_bus_set_bus_client(bus_, 1);
            }
            if (rc >= 0) {
                rc = sd_bus_start(bus_);
            }
        }
        if (rc < 0) {
            if (bus_ != nullptr) {
                sd_bus_unref(bus_);
                bus_ = nullptr;
            }
            warn(std::string("overlay: session bus connect failed: ") +
                 strerror(-rc));
        }
    }
}

OverlayDBusClient::~OverlayDBusClient() {
    if (bus_ != nullptr) {
        reportSendErrorImpl(sd_bus_flush(bus_));
        sd_bus_unref(bus_);
    }
}

void OverlayDBusClient::setPosition(std::string position) {
    position_ = std::move(position);
}

void OverlayDBusClient::callSimple(const char *method) {
    if (bus_ == nullptr) {
        return;
    }
    sd_bus_message *msg = nullptr;
    if (sd_bus_message_new_method_call(bus_, &msg, kOverlayService,
                                       kOverlayPath, kOverlayInterface,
                                       method) < 0) {
        return;
    }
    sd_bus_message_set_expect_reply(msg, 0);
    reportSendErrorImpl(sd_bus_send(bus_, msg, nullptr));
    sd_bus_message_unref(msg);
    reportSendErrorImpl(sd_bus_flush(bus_));
}

void OverlayDBusClient::sendShow(const std::vector<std::string> &variants,
                                 int index, bool label) {
    if (!capability_.supported || bus_ == nullptr || variants.empty()) {
        return;
    }
    sd_bus_message *msg = nullptr;
    if (sd_bus_message_new_method_call(bus_, &msg, kOverlayService,
                                       kOverlayPath, kOverlayInterface,
                                       "Show") < 0) {
        return;
    }
    sd_bus_message_open_container(msg, 'a', "s");
    for (const auto &v : variants) {
        sd_bus_message_append(msg, "s", v.c_str());
    }
    sd_bus_message_close_container(msg);
    sd_bus_message_append(msg, "isb", index, position_.c_str(),
                          static_cast<int>(label));
    sd_bus_message_set_expect_reply(msg, 0);
    reportSendErrorImpl(sd_bus_send(bus_, msg, nullptr));
    sd_bus_message_unref(msg);
    reportSendErrorImpl(sd_bus_flush(bus_));
}

void OverlayDBusClient::show(const std::vector<std::string> &variants,
                             int index) {
    sendShow(variants, index, /*label=*/false);
}

void OverlayDBusClient::hide() {
    if (bus_ == nullptr) {
        return;
    }
    callSimple("Hide");
}

void OverlayDBusClient::setProgress(int leadMs, int windowMs,
                                    uint64_t startUsec) {
    if (!capability_.supported || bus_ == nullptr) {
        return;
    }
    sd_bus_message *msg = nullptr;
    if (sd_bus_message_new_method_call(bus_, &msg, kOverlayService,
                                       kOverlayPath, kOverlayInterface,
                                       "SetProgress") < 0) {
        return;
    }
    sd_bus_message_append(msg, "iix", leadMs, windowMs,
                          static_cast<int64_t>(startUsec));
    sd_bus_message_set_expect_reply(msg, 0);
    reportSendErrorImpl(sd_bus_send(bus_, msg, nullptr));
    sd_bus_message_unref(msg);
    reportSendErrorImpl(sd_bus_flush(bus_));
}

void OverlayDBusClient::freezeProgress() {
    if (!capability_.supported) {
        return;
    }
    callSimple("FreezeProgress");
}

void OverlayDBusClient::showProfileName(const std::string &name) {
    sendShow({name}, kNoHighlightIndex, /*label=*/true);
}

void OverlayDBusClient::quitStaleDaemon() {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = nullptr;
    bool hasOwner = false;
    if (sd_bus_call_method(bus_, "org.freedesktop.DBus",
                           "/org/freedesktop/DBus", "org.freedesktop.DBus",
                           "NameHasOwner", &err, &reply, "s",
                           kOverlayService) >= 0) {
        int owned = 0;
        sd_bus_message_read(reply, "b", &owned);
        hasOwner = owned != 0;
    }
    sd_bus_error_free(&err);
    if (reply != nullptr) {
        sd_bus_message_unref(reply);
        reply = nullptr;
    }

    bool gotVersion = false;
    int reported = -1;
    if (hasOwner) {
        sd_bus_message *query = nullptr;
        if (sd_bus_message_new_method_call(bus_, &query, kOverlayService,
                                           kOverlayPath, kOverlayInterface,
                                           "GetProtocolVersion") >= 0) {
            sd_bus_error qerr = SD_BUS_ERROR_NULL;
            if (sd_bus_call(bus_, query, kHandshakeTimeoutUsec, &qerr,
                            &reply) >= 0) {
                int32_t v = 0;
                if (sd_bus_message_read(reply, "i", &v) >= 0) {
                    reported = v;
                    gotVersion = true;
                }
                sd_bus_message_unref(reply);
            }
            sd_bus_error_free(&qerr);
            sd_bus_message_unref(query);
        }
    }
    if (overlayDaemonIsStale(hasOwner, gotVersion, reported,
                             kOverlayProtocolVersion)) {
        warn("overlay daemon protocol mismatch, restarting it");
        quit();
    }
}

void OverlayDBusClient::start() {
    if (!capability_.supported || bus_ == nullptr) {
        return;
    }
    quitStaleDaemon();
    // A no-op Hide activates the daemon via its D-Bus .service file when it
    // is not yet running; when it is, Hide is idempotent.
    callSimple("Hide");
}

void OverlayDBusClient::quit() {
    if (bus_ == nullptr) {
        return;
    }
    callSimple("Quit");
}

void OverlayDBusClient::applyEnabledTransition(bool enabled) {
    switch (decideOverlayLifecycleAction(lastEnabled_, enabled)) {
    case OverlayLifecycleAction::Start:
        start();
        break;
    case OverlayLifecycleAction::Quit:
        quit();
        break;
    case OverlayLifecycleAction::None:
        break;
    }
    lastEnabled_ = enabled;
}

} // namespace schnelle_zeichen
