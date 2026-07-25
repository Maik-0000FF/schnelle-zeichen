// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// The schnelle-zeichen daemon: wires the cycling engine to the raw Linux
// backend (evdev grab + uinput passthrough + Wayland virtual-keyboard
// injection). Safety: SIGINT/SIGTERM release the grab, the panic combo
// (both Shifts held) exits, and --timeout-s arms an auto-exit for test
// runs. FocusSource (per-app filter, caret overlay) lands in phase 5; until
// then the app id stays empty and the filter's Disabled default applies.

#include "combo_parse.h"
#include "config_dir.h"
#include "control_protocol.h"
#include "control_service.h"
#include "device_discovery.h"
#include "engine.h"
#include "epoll_timer_port.h"
#include "evdev_key_source.h"
#include "log.h"
#include "overlay_dbus_client.h"
#include "profile_compose.h"
#include "profile_paths.h"
#include "uinput_forwarder.h"
#include "usage_sort.h"
#include "virtual_keyboard_sink.h"
#include "xkb_resolver.h"

#include <unistd.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>

#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace schnelle_zeichen {
namespace {

// Headless fallback (unused once the D-Bus client is wired; kept for
// --no-overlay style debugging if ever needed).
class NullOverlay : public OverlayPort {
public:
    void show(const std::vector<std::string> &, int) override {}
    void hide() override {}
    void setProgress(int, int, int, uint64_t) override {}
    void freezeProgress() override {}
    void showProfileName(const std::string &) override {}
};

// The profile File field -> loader path (bare form; unsafe/empty falls back
// to the Standard mappings, the legacy single choke point).
std::string profileRelPath(const std::string &file) {
    if (file.empty() || !isSafeProfileFile(file)) {
        return kMappingsFile;
    }
    return file;
}

// Runtime + stored map for a profile file: its mappings, composed with the
// merge (only when this profile is the manifest's base), then
// frequency-sorted. The legacy buildRuntimeMap/finishRuntimeMap glue; a
// candidate for centralizing once the editor port needs the same pass.
std::pair<UmlautMap, UmlautMap> buildRuntimeMaps(const std::string &bareFile,
                                                 const EngineConfig &config,
                                                 const UsageCounts &usage) {
    UmlautMap map = loadMappingsFromFile(profileRelPath(bareFile));
    const MergeManifest manifest = loadMergeManifest();
    if (!manifest.base.empty() && manifest.base == bareFile) {
        std::vector<UmlautMap> extra;
        std::vector<std::string> extraRefs;
        for (const auto &src : manifest.sources) {
            if (src == manifest.base || !isSafeProfileFile(src)) {
                continue;
            }
            extra.push_back(loadMappingsFromFile(profileRelPath(src)));
            extraRefs.push_back(src);
        }
        std::vector<ComposeSource> sources;
        sources.push_back({manifest.base, &map});
        for (size_t i = 0; i < extra.size(); ++i) {
            sources.push_back({extraRefs[i], &extra[i]});
        }
        map = projectValues(compose(sources, manifest.order));
    }
    UmlautMap stored = map;
    if (config.behavior.sortByFrequency) {
        for (auto &kv : map) {
            const auto it = usage.find(kv.first);
            if (it != usage.end()) {
                kv.second = sortVariantsByUsage(kv.second, it->second);
            }
        }
    }
    return {std::move(map), std::move(stored)};
}

// The active profile's bare File field (legacy fallback chain: active by
// name, else first entry, else the Standard mappings).
std::string activeBareFile(const ProfilesData &profiles) {
    for (const auto &p : profiles.entries) {
        if (p.name == profiles.active) {
            return p.file;
        }
    }
    return profiles.entries.empty() ? kMappingsFile
                                    : profiles.entries.front().file;
}

// Backend-level input options from settings.conf ([Input] section; engine
// semantics stay in EngineConfig).
std::string configuredLayout() {
    std::string layout;
    const std::string path = configFilePath(kSettingsFile);
    if (path.empty()) {
        return layout;
    }
    FILE *fp = std::fopen(path.c_str(), "r");
    if (fp == nullptr) {
        return layout;
    }
    const IniDocument doc = parseIni(fp);
    std::fclose(fp);
    return parseIniString(findIniSection(doc, "Input"), "Layout");
}

volatile sig_atomic_t g_stop = 0;

// One grabbed physical keyboard: its own uinput clone plus its key source.
// All keyboards share the resolver (seat-level modifier state, like a
// compositor merges them) and the same engine handler.
struct GrabbedKeyboard {
    explicit GrabbedKeyboard(XkbResolver &resolver)
        : source(resolver, forwarder) {}
    UinputForwarder forwarder;
    EvdevKeySource source;
    std::string path;
};

} // namespace
} // namespace schnelle_zeichen

int main(int argc, char **argv) {
    using namespace schnelle_zeichen;

    std::string devicePath;
    std::string layoutOverride;
    int timeoutS = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], kDevPathPrefix.data(),
                         kDevPathPrefix.size()) == 0) {
            devicePath = argv[i];
        } else if (std::strcmp(argv[i], "--layout") == 0 && i + 1 < argc) {
            layoutOverride = argv[++i];
        } else if (std::strcmp(argv[i], "--timeout-s") == 0 && i + 1 < argc) {
            const char *arg = argv[++i];
            std::from_chars(arg, arg + std::strlen(arg), timeoutS);
        }
    }

    // Config + data from the config root. Mutable: the config watcher below
    // reloads everything when the editor (or a hand edit) rewrites files.
    EngineConfig config = loadEngineConfig();
    ProfilesData profiles = loadProfiles();
    UsageCounts usage = loadUsage();

    // Backend pieces.
    XkbResolver resolver;
    const std::string layout =
        !layoutOverride.empty() ? layoutOverride : configuredLayout();
    if (!resolver.init(layout)) {
        std::fprintf(stderr, "xkb keymap init failed (layout '%s')\n",
                     layout.c_str());
        return 1;
    }
    VirtualKeyboardSink sink;
    if (!sink.init()) {
        std::fprintf(stderr, "virtual-keyboard init failed (Wayland session "
                             "with zwp_virtual_keyboard_v1 required)\n");
        return 1;
    }

    // Engine wiring.
    EpollTimerPort timers;
    if (timers.fd() < 0) {
        // Without the timerfd every gesture window is silently dead; fail
        // loudly instead of running a daemon that cannot commit anything.
        std::fprintf(stderr, "timerfd_create failed: %s\n",
                     std::strerror(errno));
        return 1;
    }
    OverlayDBusClient overlay;
    overlay.setPosition(overlayPositionString(config.overlay));
    overlay.applyEnabledTransition(config.overlay.enabled);
    std::fprintf(stderr, "[overlay] bus=%s enabled=%d position=%s\n",
                 overlay.connected() ? "connected" : "UNAVAILABLE",
                 config.overlay.enabled ? 1 : 0,
                 overlayPositionString(config.overlay).c_str());
    Engine engine(sink, overlay, timers);
    engine.setConfig(config);
    engine.setProfiles(profiles);
    engine.setUsageCounts(std::move(usage));
    auto maps = buildRuntimeMaps(activeBareFile(profiles), config,
                                 engine.usageCounts());
    size_t multiVariant = 0;
    for (const auto &kv : maps.first) {
        if (kv.second.size() > 1) {
            ++multiVariant;
        }
    }
    std::fprintf(stderr,
                 "[config] root=%s profile='%s' mappings=%zu (%zu with "
                 "cycling variants)\n",
                 configDir().c_str(), profiles.active.c_str(),
                 maps.first.size(), multiVariant);
    engine.setMappings(std::move(maps.first), std::move(maps.second));
    engine.rebuildMaps = [&](const std::string &file) {
        return buildRuntimeMaps(file, config, engine.usageCounts());
    };
    engine.persistProfiles = [](const ProfilesData &p) { saveProfiles(p); };
    engine.persistUsage = [](const UsageCounts &c) { saveUsage(c); };

    // Runtime pause: a flag, never a config write, so resuming restores the
    // exact prior state. While paused every event passes through untouched;
    // only the pause-toggle shortcut is still matched. Driven by the D-Bus
    // control service (tray) and the [Behavior] PauseToggle combo alike.
    bool paused = false;
    ShortcutCombo pauseCombo = parseShortcutCombo(config.behavior.pauseToggle);
    ControlService control;
    const auto setPaused = [&](bool on) {
        if (paused == on) {
            return;
        }
        paused = on;
        // Cancel any half-open gesture, hide the overlay and flush pending
        // usage counts, so pausing mid-gesture leaves nothing dangling.
        engine.focusChanged(std::string());
        control.setPaused(paused);
        std::fprintf(stderr, "[pause] %s\n", paused ? "paused" : "resumed");
    };

    // Full config reload, mirroring the legacy reloadConfig/applyConfig
    // sequence: consume a pending usage-reset marker first (so the rebuild
    // sorts on the cleared counts), then settings, profiles and maps, then
    // the overlay lifecycle. Driven by the config watcher below; also safe
    // to call any time.
    const auto reloadAll = [&] {
        if (takeUsageResetMarker()) {
            engine.setUsageCounts({});
            deleteUsage();
        }
        config = loadEngineConfig();
        profiles = loadProfiles();
        engine.setConfig(config);
        engine.setProfiles(profiles);
        auto rebuilt = buildRuntimeMaps(activeBareFile(profiles), config,
                                        engine.usageCounts());
        engine.setMappings(std::move(rebuilt.first), std::move(rebuilt.second));
        overlay.setPosition(overlayPositionString(config.overlay));
        overlay.applyEnabledTransition(config.overlay.enabled);
        pauseCombo = parseShortcutCombo(config.behavior.pauseToggle);
        // Never strand a paused engine: if the reload removed (or broke) the
        // toggle shortcut, the keyboard way back is gone, so resume. The
        // tray still works either way; this guards the shortcut-only
        // workflow.
        if (paused && !pauseCombo.valid()) {
            std::fprintf(stderr, "[pause] toggle shortcut removed\n");
            setPaused(false);
        }
        std::fprintf(stderr, "[config] reloaded: profile='%s'\n",
                     profiles.active.c_str());
    };

    // Panic combo (both Shifts) wraps the engine handler; shared by every
    // grabbed keyboard.
    bool leftShift = false;
    bool rightShift = false;
    const KeySource::Handler handler = [&](const KeyEvent &e) {
        const bool down = e.action != KeyAction::Release;
        if (e.code == KEY_LEFTSHIFT + kXkbKeycodeOffset) {
            leftShift = down;
        }
        if (e.code == KEY_RIGHTSHIFT + kXkbKeycodeOffset) {
            rightShift = down;
        }
        if (leftShift && rightShift) {
            std::fprintf(stderr, "[panic] both shifts held, exiting\n");
            g_stop = 1;
            return false;
        }
        // Pause toggle: matched on press, before (and instead of) the
        // engine. While paused this is the only combo still recognized;
        // everything else forwards untouched.
        if (e.action == KeyAction::Press && pauseCombo.valid() &&
            pauseCombo.matches(e.modifiers, e.keysym)) {
            setPaused(!paused);
            return true;
        }
        if (paused) {
            return false;
        }
        return engine.onKeyEvent(e) == Engine::Decision::Consume;
    };

    // Grab every physical keyboard (multi-keyboard setups, BT + USB); a
    // single explicit /dev/... argument restricts to that device.
    const int ep = epoll_create1(EPOLL_CLOEXEC);
    if (ep < 0) {
        std::fprintf(stderr, "epoll_create1 failed: %s\n",
                     std::strerror(errno));
        return 1;
    }
    // Shared epoll registration with a loud failure path: a silently
    // unregistered fd is a dead subsystem (keyboard, timer, signal) that
    // looks alive. fd < 0 marks an optional subsystem that never came up.
    const auto addToEpoll = [&](int fd, const char *what) {
        if (fd < 0) {
            return false;
        }
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0) {
            std::fprintf(stderr, "epoll_ctl(%s) failed: %s\n", what,
                         std::strerror(errno));
            return false;
        }
        return true;
    };

    // Hotplug: watch /dev/input BEFORE the initial scan, so a device that
    // appears between the two is caught by the watch instead of falling
    // into an unwatched gap (there is no later rescan). The scan may then
    // race the watch into a duplicate report; grabKeyboard's path guard
    // makes that harmless. Degrades to no-hotplug with a warning when
    // inotify is unavailable.
    const int inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd < 0 ||
        inotify_add_watch(inotifyFd, kInputDevDir, IN_CREATE | IN_ATTRIB) < 0) {
        std::fprintf(stderr,
                     "[hotplug] inotify unavailable (%s); replugged "
                     "keyboards will not be re-grabbed\n",
                     std::strerror(errno));
    }

    std::vector<std::unique_ptr<GrabbedKeyboard>> keyboards;
    // seedLocks: the startup grabs may trust the lock LEDs (the compositor
    // kept them truthful until this moment); hotplug grabs must not (a
    // fresh kernel device reports dark LEDs regardless of the seat state).
    const auto grabKeyboard = [&](const std::string &path,
                                  const std::string &name, bool seedLocks) {
        for (const auto &kb : keyboards) {
            if (kb->path == path) {
                return; // hotplug can report a node twice (CREATE + ATTRIB)
            }
        }
        auto kb = std::make_unique<GrabbedKeyboard>(resolver);
        kb->path = path;
        if (!kb->source.open(path, seedLocks)) {
            std::fprintf(stderr, "open %s failed (sudo? uinput module?)\n",
                         path.c_str());
            return;
        }
        kb->source.setHandler(handler);
        if (!kb->source.start()) {
            std::fprintf(stderr, "EVIOCGRAB failed on %s\n", path.c_str());
            return;
        }
        if (!addToEpoll(kb->source.fd(), path.c_str())) {
            return; // dropping the source releases the grab again
        }
        std::fprintf(stderr, "[dev] grabbed %s (%s)\n", path.c_str(),
                     name.c_str());
        keyboards.push_back(std::move(kb));
    };
    if (!devicePath.empty()) {
        grabKeyboard(devicePath, devicePath, /*seedLocks=*/true);
    } else {
        for (const auto &found : discoverKeyboards()) {
            grabKeyboard(found.path, found.name, /*seedLocks=*/true);
        }
    }
    if (keyboards.empty()) {
        std::fprintf(stderr, "no keyboard found under %s (permissions?)\n",
                     kInputDevDir);
        return 1;
    }

    // Config watcher: the engine reloads itself when the editor (or a hand
    // edit) rewrites config files, replacing the legacy addon-reload
    // D-Bus round-trip with a decoupled file watch. The atomic writers land
    // as rename targets (IN_MOVED_TO); plain editors close-write; deletes
    // matter too (dissolving a merge removes merge.conf, removing a profile
    // deletes its mappings file). usage.conf is the engine's own output and
    // must not self-trigger (the name filter below also covers the engine's
    // own usage.conf delete on a counter reset). Debounced, since one editor
    // save touches several files back to back.
    constexpr uint64_t kConfigReloadDebounceMs = 300;
    constexpr uint32_t kConfigWatchMask =
        IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE;
    std::error_code cfgEc;
    std::filesystem::create_directories(configDir(), cfgEc);
    const std::string profilesSubdir = configDir() + "/" + kProfilesSubdir;
    std::filesystem::create_directories(profilesSubdir, cfgEc);
    const int cfgWatchFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (cfgWatchFd < 0 ||
        inotify_add_watch(cfgWatchFd, configDir().c_str(), kConfigWatchMask) <
            0 ||
        inotify_add_watch(cfgWatchFd, profilesSubdir.c_str(),
                          kConfigWatchMask) < 0) {
        // max_user_watches exhausted or inotify unavailable: run degraded
        // instead of silently never reloading.
        std::fprintf(stderr,
                     "[config] inotify unavailable (%s); config edits apply "
                     "only after a restart\n",
                     std::strerror(errno));
    }
    TimerPort::TimerId reloadDebounce = TimerPort::kInvalidTimer;

    // Session-bus control surface (tray, OS shortcuts): pause/resume/quit.
    // Optional; the engine runs fine without a session bus.
    control.onPauseRequested = setPaused;
    control.onQuitRequested = [] { g_stop = 1; };
    if (control.init()) {
        std::fprintf(stderr, "[control] %s on session bus\n", kEngineService);
    }

    if (timeoutS > 0) {
        timers.schedule(static_cast<uint64_t>(timeoutS) * 1'000'000, [] {
            std::fprintf(stderr, "[timeout] auto-exit\n");
            g_stop = 1;
        });
    }

    // Event loop: devices + hotplug + timers + signals.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        std::fprintf(stderr, "sigprocmask failed: %s\n", std::strerror(errno));
        return 1;
    }
    const int sigFd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sigFd < 0) {
        // The signals are blocked already: without the fd they would never
        // be consumed, Ctrl+C and systemctl stop would do nothing and the
        // grab would hold until SIGKILL. Unblock and abort instead of
        // running an unkillable daemon.
        std::fprintf(stderr, "signalfd failed: %s\n", std::strerror(errno));
        sigprocmask(SIG_UNBLOCK, &mask, nullptr);
        return 1;
    }

    // Timers and signal delivery are load-bearing; refuse to run without
    // them. The watches are optional (their absence was warned above) and
    // the control bus is optional by design.
    if (!addToEpoll(timers.fd(), "timerfd") || !addToEpoll(sigFd, "signalfd")) {
        return 1;
    }
    addToEpoll(inotifyFd, "hotplug watch");
    addToEpoll(cfgWatchFd, "config watch");
    if (control.connected()) {
        addToEpoll(control.fd(), "control bus");
    }

    std::fprintf(stderr,
                 "[grab] %zu keyboard(s); engine running. Panic: "
                 "both Shifts. Ctrl+C to quit.\n",
                 keyboards.size());

    epoll_event events[8];
    while (g_stop == 0) {
        const int n = epoll_wait(ep, events, 8, 500);
        for (int i = 0; i < n && g_stop == 0; ++i) {
            const int fd = events[i].data.fd;
            if (fd == timers.fd()) {
                timers.dispatch();
            } else if (fd == sigFd) {
                g_stop = 1;
            } else if (fd == inotifyFd) {
                // New /dev/input nodes: grab eligible keyboards (only in
                // auto-discovery mode; an explicit device stays exclusive).
                char buf[4096];
                const ssize_t len = read(inotifyFd, buf, sizeof(buf));
                for (ssize_t off = 0; devicePath.empty() && off < len;) {
                    const auto *ie =
                        reinterpret_cast<const inotify_event *>(buf + off);
                    if (ie->len > 0 &&
                        std::strncmp(ie->name, kEventNodePrefix.data(),
                                     kEventNodePrefix.size()) == 0) {
                        const std::string path =
                            std::string(kInputDevDir) + "/" + ie->name;
                        std::string name;
                        if (isEligibleKeyboard(path, &name)) {
                            grabKeyboard(path, name, /*seedLocks=*/false);
                        }
                    }
                    off +=
                        static_cast<ssize_t>(sizeof(inotify_event)) + ie->len;
                }
            } else if (fd == cfgWatchFd) {
                // Config files changed: debounce, then reload everything.
                // The engine's own usage writes must not self-trigger.
                char buf[4096];
                const ssize_t len = read(cfgWatchFd, buf, sizeof(buf));
                bool relevant = false;
                // Prefix match on purpose: it also covers the atomic
                // writer's sibling temp file (kUsageFile + kAtomicTmpSuffix,
                // see mappings_loader.cpp).
                const std::string_view usageName(kUsageFile);
                for (ssize_t off = 0; off < len;) {
                    const auto *ie =
                        reinterpret_cast<const inotify_event *>(buf + off);
                    if (ie->len > 0 && std::strncmp(ie->name, usageName.data(),
                                                    usageName.size()) != 0) {
                        relevant = true;
                    }
                    off +=
                        static_cast<ssize_t>(sizeof(inotify_event)) + ie->len;
                }
                if (relevant) {
                    timers.cancel(reloadDebounce);
                    reloadDebounce =
                        timers.schedule(kConfigReloadDebounceMs * 1'000, [&] {
                            reloadDebounce = TimerPort::kInvalidTimer;
                            reloadAll();
                        });
                }
            } else if (control.connected() && fd == control.fd()) {
                control.process();
            } else {
                for (auto &kb : keyboards) {
                    if (kb->source.fd() == fd) {
                        kb->source.dispatch();
                        break;
                    }
                }
            }
        }
        // Idle pump for the control bus: sd-bus occasionally needs a process
        // pass beyond fd readability (internal queues, flushes). Cheap no-op
        // when nothing is pending.
        control.process();
        // Drop dead devices (unplug, BT disconnect); their fds leave the
        // epoll set when closed by the destructor.
        for (auto it = keyboards.begin(); it != keyboards.end();) {
            if ((*it)->source.dead()) {
                std::fprintf(stderr, "[dev] lost %s\n", (*it)->path.c_str());
                it = keyboards.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto &kb : keyboards) {
        kb->source.stop();
    }
    engine.focusChanged(""); // final gesture clear + usage flush
    std::fprintf(stderr, "[exit] grab released\n");
    return 0;
}
