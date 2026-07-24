// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// The schnelle-zeichen daemon: wires the cycling engine to the raw Linux
// backend (evdev grab + uinput passthrough + Wayland virtual-keyboard
// injection). Safety: SIGINT/SIGTERM release the grab, the panic combo
// (both Shifts held) exits, and --timeout-s arms an auto-exit for test
// runs. FocusSource (per-app filter, caret overlay) lands in phase 5; until
// then the app id stays empty and the filter's Disabled default applies.

#include "config_dir.h"
#include "device_discovery.h"
#include "engine.h"
#include "epoll_timer_port.h"
#include "evdev_key_source.h"
#include "log.h"
#include "profile_compose.h"
#include "uinput_forwarder.h"
#include "usage_sort.h"
#include "virtual_keyboard_sink.h"
#include "xkb_resolver.h"

#include <unistd.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace schnelle_zeichen {
namespace {

// Overlay arrives in phase 5; the engine runs headless until then.
class NullOverlay : public OverlayPort {
public:
    void show(const std::vector<std::string> &, int) override {}
    void hide() override {}
    void setProgress(int, int, uint64_t) override {}
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
        if (std::strncmp(argv[i], "/dev/", 5) == 0) {
            devicePath = argv[i];
        } else if (std::strcmp(argv[i], "--layout") == 0 && i + 1 < argc) {
            layoutOverride = argv[++i];
        } else if (std::strcmp(argv[i], "--timeout-s") == 0 && i + 1 < argc) {
            timeoutS = std::atoi(argv[++i]);
        }
    }

    // Config + data from the config root.
    const EngineConfig config = loadEngineConfig();
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
    NullOverlay overlay;
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
        return engine.onKeyEvent(e) == Engine::Decision::Consume;
    };

    // Grab every physical keyboard (multi-keyboard setups, BT + USB); a
    // single explicit /dev/... argument restricts to that device.
    const int ep = epoll_create1(EPOLL_CLOEXEC);
    std::vector<std::unique_ptr<GrabbedKeyboard>> keyboards;
    const auto grabKeyboard = [&](const std::string &path,
                                  const std::string &name) {
        for (const auto &kb : keyboards) {
            if (kb->path == path) {
                return; // hotplug can report a node twice (CREATE + ATTRIB)
            }
        }
        auto kb = std::make_unique<GrabbedKeyboard>(resolver);
        kb->path = path;
        if (!kb->source.open(path)) {
            std::fprintf(stderr, "open %s failed (sudo? uinput module?)\n",
                         path.c_str());
            return;
        }
        kb->source.setHandler(handler);
        if (!kb->source.start()) {
            std::fprintf(stderr, "EVIOCGRAB failed on %s\n", path.c_str());
            return;
        }
        epoll_event kev{};
        kev.events = EPOLLIN;
        kev.data.fd = kb->source.fd();
        epoll_ctl(ep, EPOLL_CTL_ADD, kb->source.fd(), &kev);
        std::fprintf(stderr, "[dev] grabbed %s (%s)\n", path.c_str(),
                     name.c_str());
        keyboards.push_back(std::move(kb));
    };
    if (!devicePath.empty()) {
        grabKeyboard(devicePath, devicePath);
    } else {
        for (const auto &found : discoverKeyboards()) {
            grabKeyboard(found.path, found.name);
        }
    }
    if (keyboards.empty()) {
        std::fprintf(stderr,
                     "no keyboard found under /dev/input (permissions?)\n");
        return 1;
    }

    // Hotplug: watch /dev/input so a replugged or BT-reconnected keyboard
    // is grabbed again; dead devices are dropped when their read fails.
    const int inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    inotify_add_watch(inotifyFd, "/dev/input", IN_CREATE | IN_ATTRIB);

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
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    const int sigFd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = timers.fd();
    epoll_ctl(ep, EPOLL_CTL_ADD, timers.fd(), &ev);
    ev.data.fd = sigFd;
    epoll_ctl(ep, EPOLL_CTL_ADD, sigFd, &ev);
    ev.data.fd = inotifyFd;
    epoll_ctl(ep, EPOLL_CTL_ADD, inotifyFd, &ev);

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
                        std::strncmp(ie->name, "event", 5) == 0) {
                        const std::string path =
                            std::string("/dev/input/") + ie->name;
                        std::string name;
                        if (isEligibleKeyboard(path, &name)) {
                            grabKeyboard(path, name);
                        }
                    }
                    off +=
                        static_cast<ssize_t>(sizeof(inotify_event)) + ie->len;
                }
            } else {
                for (auto &kb : keyboards) {
                    if (kb->source.fd() == fd) {
                        kb->source.dispatch();
                        break;
                    }
                }
            }
        }
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
