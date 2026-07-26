// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_BACKENDS_EVDEV_OVERLAY_DBUS_CLIENT_H
#define SCHNELLE_ZEICHEN_BACKENDS_EVDEV_OVERLAY_DBUS_CLIENT_H

// OverlayPort implementation over D-Bus (sd-bus): the privileged engine
// daemon drives the unprivileged Qt overlay daemon. Port of the legacy
// OverlayClient semantics: fire-and-forget calls (a missing daemon is
// silently ignored), a layer-shell capability gate sampled once, the
// enable-transition lifecycle and the stale-daemon version handshake.

#include "engine_config.h" // OverlayConfig (position composition)
#include "layer_shell_capability.h"
#include "overlay_lifecycle.h"
#include "overlay_port.h"

#include <optional>
#include <string>
#include <vector>

struct sd_bus;

namespace schnelle_zeichen {

class FocusSource;

// The single "<Row><Col>" position string the daemon expects (for example
// "TopCol4"), prefixed with the shared cursor marker in MouseCursor
// placement. Mirrors the legacy overlayPositionString composition.
std::string overlayPositionString(const OverlayConfig &overlay);

class OverlayDBusClient : public OverlayPort {
public:
    OverlayDBusClient();
    ~OverlayDBusClient() override;
    OverlayDBusClient(const OverlayDBusClient &) = delete;
    OverlayDBusClient &operator=(const OverlayDBusClient &) = delete;

    // The configured placement, re-set on every config load.
    void setPosition(std::string position);

    // Enable TextCaret placement: a non-null source is queried on every show to
    // anchor the overlay at the caret, falling back to the configured grid
    // position when it reports no caret. nullptr (the default) disables it, so
    // the plain position string is used. The source is owned by the caller and
    // must outlive this client.
    void setCaretPlacement(FocusSource *source) { caretSource_ = source; }

    // Diagnostic: whether a bus connection exists (the daemon may still be
    // absent; sends are fire-and-forget either way).
    bool connected() const { return bus_ != nullptr; }

    // Start/stop the daemon to match the Enabled flag (legacy
    // applyEnabledTransition), including the stale-daemon handshake.
    void applyEnabledTransition(bool enabled);

    // OverlayPort.
    void show(const std::vector<std::string> &variants, int index) override;
    void hide() override;
    void setProgress(int leadMs, int windowMs, int holdMs,
                     uint64_t startUsec) override;
    void freezeProgress() override;
    void showProfileName(const std::string &name) override;

private:
    void callSimple(const char *method);
    void sendShow(const std::vector<std::string> &variants, int index,
                  bool label);
    void start();
    void quit();
    void quitStaleDaemon();

    sd_bus *bus_ = nullptr;
    std::string position_ = "TopCol4";
    FocusSource *caretSource_ = nullptr;
    std::optional<bool> lastEnabled_;
    LayerShellCapability capability_;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_BACKENDS_EVDEV_OVERLAY_DBUS_CLIENT_H
