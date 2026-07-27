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

// Non-empty value of an IM-framework environment variable, or nullptr.
const char *imFrameworkEnv(const char *name) {
    const char *value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? value : nullptr;
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

bool InputMethodSink::init() {
    display_ = wl_display_connect(nullptr);
    if (display_ == nullptr) {
        warn("input method: no wayland display");
        return false;
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    wl_display_roundtrip(display_);
    if (inputMethod_ == nullptr) {
        warn("input method: compositor lacks zwp_input_method_v1");
        return false;
    }
    // A protocol error on bind (a compositor restricting the global to its own
    // input-method process) only surfaces on the next round trip, so treat a
    // failed one as a failed init rather than running a dead sink.
    if (wl_display_roundtrip(display_) < 0) {
        warn("input method: compositor rejected the zwp_input_method_v1 bind");
        return false;
    }

    // Bound, but reach depends on the session: with an IM framework configured
    // the toolkits talk to it directly and never enable Wayland text-input, so
    // this sink would stay inactive forever. Say so once, loudly, instead of
    // letting every later gesture vanish without explanation.
    const char *qtIm = imFrameworkEnv(kQtImModuleEnv);
    const char *gtkIm = imFrameworkEnv(kGtkImModuleEnv);
    if (qtIm != nullptr || gtkIm != nullptr) {
        warn(std::string("input method: an IM framework is configured (") +
             kQtImModuleEnv + "=" + (qtIm != nullptr ? qtIm : "") + ", " +
             kGtkImModuleEnv + "=" + (gtkIm != nullptr ? gtkIm : "") +
             "). Qt and GTK applications then bypass the Wayland "
             "input-method protocol, so injection will not reach them.");
    }
    return true;
}

int InputMethodSink::fd() const {
    return display_ != nullptr ? wl_display_get_fd(display_) : -1;
}

void InputMethodSink::dispatch() {
    if (display_ == nullptr) {
        return;
    }
    // Called only when epoll reports the fd readable, so this does not block.
    if (wl_display_dispatch(display_) < 0) {
        warn("input method: wayland connection lost");
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
    if (utf8.empty()) {
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
    wl_display_roundtrip(display_);
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
