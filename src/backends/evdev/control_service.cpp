// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "control_service.h"

#include "control_protocol.h"
#include "log.h"

#include <systemd/sd-bus.h>

#include <cstring>
#include <string>

namespace schnelle_zeichen {

namespace {

ControlService *serviceFrom(void *userdata) {
    return static_cast<ControlService *>(userdata);
}

int methodPause(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *self = serviceFrom(userdata);
    if (self->onPauseRequested) {
        self->onPauseRequested(true);
    }
    return sd_bus_reply_method_return(m, "");
}

int methodResume(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *self = serviceFrom(userdata);
    if (self->onPauseRequested) {
        self->onPauseRequested(false);
    }
    return sd_bus_reply_method_return(m, "");
}

int methodToggle(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *self = serviceFrom(userdata);
    if (self->onPauseRequested) {
        self->onPauseRequested(!self->paused());
    }
    return sd_bus_reply_method_return(m, "b", static_cast<int>(self->paused()));
}

int methodGetPaused(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *self = serviceFrom(userdata);
    return sd_bus_reply_method_return(m, "b", static_cast<int>(self->paused()));
}

int methodQuit(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *self = serviceFrom(userdata);
    // Reply before the quit callback flips the stop flag, so the caller gets
    // its answer even though the loop exits right after.
    const int rc = sd_bus_reply_method_return(m, "");
    if (self->onQuitRequested) {
        self->onQuitRequested();
    }
    return rc;
}

const sd_bus_vtable kEngineVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Pause", "", "", methodPause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Resume", "", "", methodResume, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Toggle", "", "b", methodToggle, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetPaused", "", "b", methodGetPaused,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Quit", "", "", methodQuit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("PausedChanged", "b", 0),
    SD_BUS_VTABLE_END,
};

} // namespace

ControlService::~ControlService() {
    if (bus_ != nullptr) {
        sd_bus_flush(bus_);
        sd_bus_unref(bus_);
    }
}

bool ControlService::init() {
    int rc = sd_bus_open_user(&bus_);
    if (rc < 0) {
        bus_ = nullptr;
        warn(std::string("engine control: session bus connect failed: ") +
             strerror(-rc));
        return false;
    }
    rc = sd_bus_add_object_vtable(bus_, nullptr, kEnginePath, kEngineInterface,
                                  kEngineVtable, this);
    if (rc >= 0) {
        rc = sd_bus_request_name(bus_, kEngineService, 0);
    }
    if (rc < 0) {
        // A second engine instance already owns the name, or the bus refused
        // the object; run without the control surface rather than failing.
        warn(std::string("engine control: service setup failed: ") +
             strerror(-rc));
        sd_bus_unref(bus_);
        bus_ = nullptr;
        return false;
    }
    return true;
}

int ControlService::fd() const {
    return bus_ != nullptr ? sd_bus_get_fd(bus_) : -1;
}

void ControlService::process() {
    if (bus_ == nullptr) {
        return;
    }
    // Drain everything that is ready; sd_bus_process handles one message per
    // call and returns > 0 while more work is pending.
    while (sd_bus_process(bus_, nullptr) > 0) {
    }
}

void ControlService::setPaused(bool paused) {
    if (paused_ == paused) {
        return;
    }
    paused_ = paused;
    emitPausedChanged();
}

void ControlService::emitPausedChanged() {
    if (bus_ == nullptr) {
        return;
    }
    sd_bus_emit_signal(bus_, kEnginePath, kEngineInterface, "PausedChanged",
                       "b", static_cast<int>(paused_));
    sd_bus_flush(bus_);
}

} // namespace schnelle_zeichen
