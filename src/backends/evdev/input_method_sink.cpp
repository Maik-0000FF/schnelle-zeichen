// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "input_method_sink.h"

#include "log.h"

#include <wayland-client.h>
#include "input-method-unstable-v1-client-protocol.h"

#include <cstdlib>
#include <cstring>

namespace schnelle_zeichen {

namespace {

// The protocol version this sink speaks. zwp_input_method_v1 never moved past
// its initial revision, so the bind is pinned rather than negotiated.
constexpr uint32_t kInputMethodVersion = 1;

void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                    const char *interface, uint32_t version) {
    static_cast<InputMethodSink *>(data)->onGlobal(registry, name, interface,
                                                   version);
}
void registryGlobalRemove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener kRegistryListener = {registryGlobal,
                                                registryGlobalRemove};

// Context events. Only commit_state carries state this sink needs; the rest
// exist because libwayland dispatches into every slot and a null entry would
// crash. surrounding_text and content_type would be the raw material for a
// future context-aware mode.
void contextSurroundingText(void *, zwp_input_method_context_v1 *, const char *,
                            uint32_t, uint32_t) {}
void contextReset(void *, zwp_input_method_context_v1 *) {}
void contextContentType(void *, zwp_input_method_context_v1 *, uint32_t,
                        uint32_t) {}
void contextInvokeAction(void *, zwp_input_method_context_v1 *, uint32_t,
                         uint32_t) {}
void contextCommitState(void *data, zwp_input_method_context_v1 *,
                        uint32_t serial) {
    static_cast<InputMethodSink *>(data)->onCommitState(serial);
}
void contextPreferredLanguage(void *, zwp_input_method_context_v1 *,
                              const char *) {}

const zwp_input_method_context_v1_listener kContextListener = {
    contextSurroundingText, contextReset,       contextContentType,
    contextInvokeAction,    contextCommitState, contextPreferredLanguage};

void inputMethodActivate(void *data, zwp_input_method_v1 *,
                         zwp_input_method_context_v1 *context) {
    static_cast<InputMethodSink *>(data)->onActivate(context);
}
void inputMethodDeactivate(void *data, zwp_input_method_v1 *,
                           zwp_input_method_context_v1 *context) {
    static_cast<InputMethodSink *>(data)->onDeactivate(context);
}

const zwp_input_method_v1_listener kInputMethodListener = {
    inputMethodActivate, inputMethodDeactivate};

// Value of an IM-framework environment variable when it actually routes the
// toolkit past the Wayland protocol, otherwise nullptr. Unset, empty and every
// entry of kNonBypassingImModules count as "does not bypass".
const char *imFrameworkEnv(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return nullptr;
    }
    for (const char *harmless : kNonBypassingImModules) {
        if (std::strcmp(value, harmless) == 0) {
            return nullptr;
        }
    }
    return value;
}

} // namespace

InputMethodSink::~InputMethodSink() {
    destroyContext();
    if (inputMethod_ != nullptr) {
        zwp_input_method_v1_destroy(inputMethod_);
    }
    if (registry_ != nullptr) {
        wl_registry_destroy(registry_);
    }
    if (display_ != nullptr) {
        wl_display_disconnect(display_);
    }
}

void InputMethodSink::onGlobal(wl_registry *registry, uint32_t name,
                               const char *interface, uint32_t) {
    if (std::strcmp(interface, zwp_input_method_v1_interface.name) == 0 &&
        inputMethod_ == nullptr) {
        inputMethod_ = static_cast<zwp_input_method_v1 *>(
            wl_registry_bind(registry, name, &zwp_input_method_v1_interface,
                             kInputMethodVersion));
        zwp_input_method_v1_add_listener(inputMethod_, &kInputMethodListener,
                                         this);
    }
}

SinkInit InputMethodSink::init() {
    display_ = wl_display_connect(nullptr);
    if (display_ == nullptr) {
        warn("input method: no wayland display");
        return SinkInit::NoDisplayServer;
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    wl_display_roundtrip(display_);
    if (inputMethod_ == nullptr) {
        warn("input method: compositor lacks zwp_input_method_v1");
        return SinkInit::ProtocolAbsent;
    }
    // A rejected bind only surfaces on the next round trip, so a failed one
    // is a failed init rather than a dead sink running on. The global was in
    // the registry, so this is never "the session lacks the protocol": either
    // another input method already holds it (only one client at a time), or
    // the compositor restricts it to the process it starts itself. Both can
    // change while the session keeps running.
    if (wl_display_roundtrip(display_) < 0) {
        warn("input method: zwp_input_method_v1 is advertised but the bind was "
             "rejected; another input method most likely holds it");
        return SinkInit::ProtocolUnusable;
    }

    // Bound, but reach depends on the session: with an IM framework configured
    // the toolkits talk to it directly and never enable Wayland text-input, so
    // this sink would stay inactive forever. Say so once, loudly, instead of
    // letting every later gesture vanish without explanation. Only the
    // variables that actually route past the protocol are named: printing a
    // harmless one as "NAME=" reads like it is unset and misleads the reader.
    std::string bypassing;
    for (const char *name : {kQtImModuleEnv, kGtkImModuleEnv}) {
        if (const char *value = imFrameworkEnv(name)) {
            if (!bypassing.empty()) {
                bypassing += ", ";
            }
            bypassing += std::string(name) + "=" + value;
        }
    }
    if (!bypassing.empty()) {
        warn("input method: an IM framework is configured (" + bypassing +
             "). Qt and GTK applications then bypass the Wayland "
             "input-method protocol, so injection will not reach them.");
    }
    return SinkInit::Ok;
}

int InputMethodSink::fd() const {
    return display_ != nullptr ? wl_display_get_fd(display_) : -1;
}

void InputMethodSink::dispatch() {
    if (display_ == nullptr || dead_) {
        return;
    }
    // Called only when epoll reports the fd readable, so this does not block.
    if (wl_display_dispatch(display_) < 0) {
        // Latch it. A failed dispatch sets the display's error permanently:
        // every later call returns immediately, while the fd stays readable at
        // EOF, so an unlatched retry would spin the event loop at full speed
        // and write one warning per iteration for the rest of the session.
        dead_ = true;
        // The context can never be withdrawn now (deactivate would have to
        // arrive over the same dead connection), so drop it here. Otherwise
        // commit() would keep believing it has somewhere to write to.
        destroyContext();
        warn("input method: wayland connection lost; no further text can be "
             "injected");
    }
}

void InputMethodSink::destroyContext() {
    if (context_ != nullptr) {
        zwp_input_method_context_v1_destroy(context_);
        context_ = nullptr;
    }
    serial_ = 0;
}

void InputMethodSink::onActivate(zwp_input_method_context_v1 *context) {
    // The compositor may activate again without an intervening deactivate
    // (focus moving between text fields); the previous context is dead then.
    destroyContext();
    context_ = context;
    zwp_input_method_context_v1_add_listener(context_, &kContextListener, this);
    // Arm the inactive warning again: the typical session alternates between
    // text-input capable and incapable windows, and warning only once ever
    // would leave every later stretch of swallowed text unexplained.
    inactiveWarned_ = false;
}

void InputMethodSink::onDeactivate(zwp_input_method_context_v1 *context) {
    // Ignore a deactivate for a context already replaced by a newer activate.
    if (context != context_) {
        zwp_input_method_context_v1_destroy(context);
        return;
    }
    destroyContext();
}

void InputMethodSink::onCommitState(uint32_t serial) { serial_ = serial; }

void InputMethodSink::commit(const std::string &utf8) {
    if (utf8.empty() || dead_) {
        return;
    }
    if (context_ == nullptr) {
        // No focused application speaks text-input, so there is nothing to
        // commit into. Warn once: repeating this per keystroke would flood the
        // journal on a session that simply uses non-text-input applications.
        if (!inactiveWarned_) {
            inactiveWarned_ = true;
            warn("input method: no active text-input context; the focused "
                 "application does not support the Wayland input-method "
                 "protocol, so nothing was inserted");
        }
        return;
    }
    zwp_input_method_context_v1_commit_string(context_, serial_, utf8.c_str());
    // Serialization barrier against the uinput passthrough channel: the commit
    // must be fully processed before any subsequently forwarded key event, the
    // same two-channel ordering requirement the virtual-keyboard sink obeys.
    // It doubles as the liveness check on the commit path: a failed round trip
    // means this text did not arrive and no later one will either.
    if (wl_display_roundtrip(display_) < 0) {
        dead_ = true;
        destroyContext();
        warn("input method: wayland connection lost during commit; no further "
             "text can be injected");
    }
}

bool InputMethodSink::preeditSupported() const {
    // The protocol could render a real in-app pre-edit via preedit_string, but
    // switching the cycling preview from schnelle-zeichen's own overlay to the
    // application's pre-edit is a separate behavioral change. Until that is
    // deliberately made, the proven overlay path stays in charge.
    return false;
}

void InputMethodSink::commitPreedit(const std::string &) {}

void InputMethodSink::clearPreedit() {}

} // namespace schnelle_zeichen
