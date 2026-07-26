// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Factored engine behavior tests: one named case per legacy semantic, all
// driven through fake ports (manual clock, recording sink and overlay). The
// cases mirror the schnelle-umlaute behaviors the port must keep 1:1, plus
// explicit A/B assertions that the opt-in extensions change nothing while
// off.

#include "engine.h"
#include "keysyms.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace schnelle_zeichen;

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

namespace {

// ------------------------------------------------------------------ fakes

class FakeTimers : public TimerPort {
public:
    uint64_t nowUsec() override { return now_; }
    TimerId schedule(uint64_t delayUsec, Callback cb) override {
        const TimerId id = nextId_++;
        pending_[id] = {now_ + delayUsec, std::move(cb)};
        return id;
    }
    void cancel(TimerId id) override { pending_.erase(id); }

    // Advance the clock WITHOUT firing due timers: models the epoll batch
    // where a key event is dispatched before the timerfd even though the
    // window deadline has already passed.
    void advanceClockOnlyMs(uint64_t ms) { now_ += ms * 1000; }

    // Advance the clock, firing every timer that comes due, in due order.
    void advanceMs(uint64_t ms) {
        const uint64_t target = now_ + ms * 1000;
        for (;;) {
            TimerId dueId = 0;
            uint64_t dueAt = target + 1;
            for (const auto &kv : pending_) {
                if (kv.second.due <= target && kv.second.due < dueAt) {
                    dueAt = kv.second.due;
                    dueId = kv.first;
                }
            }
            if (dueId == 0) {
                break;
            }
            now_ = dueAt;
            Callback cb = std::move(pending_[dueId].cb);
            pending_.erase(dueId);
            cb();
        }
        now_ = target;
    }

private:
    struct Pending {
        uint64_t due = 0;
        Callback cb;
    };
    uint64_t now_ = 1'000'000; // arbitrary nonzero start
    TimerId nextId_ = 1;
    std::map<TimerId, Pending> pending_;
};

class FakeSink : public TextSink {
public:
    void commit(const std::string &utf8) override { commits.push_back(utf8); }
    bool preeditSupported() const override { return false; }
    void commitPreedit(const std::string &) override {}
    void clearPreedit() override {}
    std::vector<std::string> commits;
};

class FakeOverlay : public OverlayPort {
public:
    void show(const std::vector<std::string> &variants, int index) override {
        lastVariants = variants;
        lastIndex = index;
        ++shows;
    }
    void hide() override { ++hides; }
    void setProgress(int, int, int, uint64_t) override {}
    void freezeProgress() override {}
    void showProfileName(const std::string &name) override {
        lastProfileName = name;
    }
    std::vector<std::string> lastVariants;
    int lastIndex = -99;
    int shows = 0;
    int hides = 0;
    std::string lastProfileName;
};

// ---------------------------------------------------------------- fixture

// Keycodes for the fixture's fake keyboard (evdev+8 convention). kKeyA/F/J
// sit on defined halves for the dual-split case (hand_classifier rows).
constexpr uint32_t kKeyA = 38, kKeyS = 39, kKeyF = 41, kKeyJ = 44;
constexpr uint32_t kKeySpaceCode = 65, kKeyAltCode = 64, kKeyX = 53;

struct Fixture {
    FakeTimers timers;
    FakeSink sink;
    FakeOverlay overlay;
    Engine engine{sink, overlay, timers};

    Fixture() {
        EngineConfig cfg; // legacy defaults: Space leader, window [0, 400]
        engine.setConfig(cfg);
        engine.setMappings(defaultMap(), defaultMap());
    }

    static UmlautMap defaultMap() {
        return {{"a", {"ä", "á", "à"}}, {"s", {"ß"}}, {"A", {"Ä"}}};
    }

    void reconfigure(const EngineConfig &cfg) { engine.setConfig(cfg); }

    static KeyEvent ev(KeyAction action, uint32_t code, uint32_t keysym,
                       const std::string &text, uint32_t mods = 0) {
        KeyEvent e;
        e.action = action;
        e.code = code;
        e.keysym = keysym;
        e.text = text;
        e.modifiers = mods;
        return e;
    }

    Engine::Decision press(uint32_t code, const std::string &text,
                           uint32_t keysym = 0, uint32_t mods = 0) {
        return engine.onKeyEvent(ev(KeyAction::Press, code,
                                    keysym         ? keysym
                                    : text.empty() ? 0
                                                   : text[0],
                                    text, mods));
    }
    Engine::Decision release(uint32_t code, const std::string &text = "",
                             uint32_t keysym = 0, uint32_t mods = 0) {
        return engine.onKeyEvent(
            ev(KeyAction::Release, code, keysym, text, mods));
    }
    Engine::Decision repeat(uint32_t code, const std::string &text) {
        return engine.onKeyEvent(
            ev(KeyAction::Repeat, code, text.empty() ? 0 : text[0], text));
    }
    Engine::Decision space(KeyAction action = KeyAction::Press) {
        return engine.onKeyEvent(ev(action, kKeySpaceCode, kKeysymSpace, " "));
    }
};

using D = Engine::Decision;

// ------------------------------------------------------------- test cases

// Tap: press withheld, release commits the plain char (historic timing).
void tapCommitsPlainOnRelease() {
    Fixture f;
    CHECK(f.press(kKeyA, "a") == D::Consume);
    CHECK(f.sink.commits.empty());
    CHECK(f.release(kKeyA) == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"a"}));
}

// Flash gesture with min-hold 0: the leader commits a single output
// instantly; leader press and key release are consumed.
void singleOutputLeaderCommitsInstantly() {
    Fixture f;
    CHECK(f.press(kKeyS, "s") == D::Consume);
    f.timers.advanceMs(30);
    CHECK(f.space() == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"ß"}));
    CHECK(f.space(KeyAction::Release) == D::Forward); // space release passes
    CHECK(f.release(kKeyS) == D::Consume);            // orphan release consumed
    CHECK(f.sink.commits.size() == 1);
}

// Multi-variant: first leader enters cycling at index 0, further leaders
// step, the release commits the selection.
void cyclingStepsAndCommitsOnRelease() {
    Fixture f;
    EngineConfig cfg;
    cfg.overlay.enabled = true;
    f.reconfigure(cfg);
    f.press(kKeyA, "a");
    f.space();
    CHECK(f.overlay.lastIndex == 0); // first leader highlights variant 1
    f.space();
    f.space(); // index 2 (à)
    CHECK(f.overlay.lastIndex == 2);
    CHECK(f.release(kKeyA) == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"à"}));
    CHECK(f.overlay.hides > 0); // release tears the picker down
}

// Reverse leader starting a fresh session lands on the LAST variant.
void reverseLeaderStartsAtLastVariant() {
    Fixture f;
    EngineConfig cfg;
    cfg.leader.spaceReverse = true;
    f.reconfigure(cfg);
    f.press(kKeyA, "a");
    f.space();
    f.release(kKeyA);
    CHECK((f.sink.commits == std::vector<std::string>{"à"}));
}

// Rollover: a second mapped key commits the pending char instantly and
// starts its own gesture ("as" at speed).
void rolloverSecondMappedKey() {
    Fixture f;
    f.press(kKeyA, "a");
    CHECK(f.press(kKeyS, "s") == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"a"}));
    f.release(kKeyA); // 'a' gone from gesture: plain forwarded release
    f.release(kKeyS);
    CHECK((f.sink.commits == std::vector<std::string>{"a", "s"}));
}

// Rollover with a non-mapped key: pending commits, the key passes through.
void rolloverOtherKeyPassesThrough() {
    Fixture f;
    f.press(kKeyA, "a");
    CHECK(f.press(kKeyX, "x") == D::Forward);
    CHECK((f.sink.commits == std::vector<std::string>{"a"}));
}

// Min-hold dead zone: a leader below min commits plain and passes through;
// repeats of the still-held key are suppressed (the "üu" guard).
void minHoldDeadZone() {
    Fixture f;
    EngineConfig cfg;
    cfg.delay.lowercaseMin = 150;
    f.reconfigure(cfg);
    f.press(kKeyA, "a");
    f.timers.advanceMs(50);
    CHECK(f.space() == D::Forward); // leader acts as a normal key
    CHECK((f.sink.commits == std::vector<std::string>{"a"}));
    CHECK(f.repeat(kKeyA, "a") == D::Consume); // suppressed, no "aa"
    CHECK(f.release(kKeyA) == D::Consume);
    CHECK(f.sink.commits.size() == 1);
}

// Leader inside [min, max] still triggers with min-hold configured.
void minHoldLeaderInsideWindowTriggers() {
    Fixture f;
    EngineConfig cfg;
    cfg.delay.lowercaseMin = 150;
    f.reconfigure(cfg);
    f.press(kKeyS, "s");
    f.timers.advanceMs(200);
    CHECK(f.space() == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"ß"}));
}

// Unlimited mode has no upper bound, so a min-hold above the stored max is
// NOT degenerate there: the full dead zone stands (the min >= max guard only
// applies to a real window).
void minHoldAboveMaxAppliesWhenUnlimited() {
    Fixture f;
    EngineConfig cfg;
    cfg.delay.unlimited = true;
    cfg.delay.lowercaseMin = 800; // above the 400 ms stored max
    f.reconfigure(cfg);
    f.press(kKeyS, "s");
    f.timers.advanceMs(500);
    CHECK(f.space() == D::Forward); // still inside the dead zone
    CHECK((f.sink.commits == std::vector<std::string>{"s"}));
    f.release(kKeyS);
    f.press(kKeyS, "s");
    f.timers.advanceMs(900);
    CHECK(f.space() == D::Consume); // past the dead zone: accent
    CHECK((f.sink.commits == std::vector<std::string>{"s", "ß"}));
}

// Window timeout commits the plain char; a held key restarts per window
// through its repeats; the final release is consumed silently.
void windowTimeoutCommitsAndRestartsPerWindow() {
    Fixture f;
    f.press(kKeyA, "a");
    f.timers.advanceMs(401);
    CHECK((f.sink.commits == std::vector<std::string>{"a"}));
    CHECK(f.repeat(kKeyA, "a") == D::Consume); // restarts a fresh gesture
    f.timers.advanceMs(401);
    CHECK((f.sink.commits == std::vector<std::string>{"a", "a"}));
    CHECK(f.release(kKeyA) == D::Consume);
    CHECK(f.sink.commits.size() == 2);
}

// Uppercase inputs use the uppercase window bound.
void uppercaseWindowApplies() {
    Fixture f;
    f.press(kKeyA, "A", 'A', static_cast<uint32_t>(KeyModifier::Shift));
    f.timers.advanceMs(500); // > lowercase 400, < uppercase 700
    CHECK(f.sink.commits.empty());
    f.timers.advanceMs(201);
    CHECK((f.sink.commits == std::vector<std::string>{"A"}));
}

// Multi-byte uppercase inputs (parseMappings allows non-ASCII inputs) use
// the uppercase window too; the ASCII-only check gave Ä the short
// lowercase window.
void multibyteUppercaseWindowApplies() {
    Fixture f;
    const UmlautMap map{{"\xC3\x84", {"\xC3\x86"}}}; // Ä -> Æ
    f.engine.setMappings(map, map);
    f.press(kKeyA, "\xC3\x84", 0x00C4,
            static_cast<uint32_t>(KeyModifier::Shift));
    f.timers.advanceMs(500); // > lowercase 400, < uppercase 700
    CHECK(f.sink.commits.empty());
    f.timers.advanceMs(201);
    CHECK((f.sink.commits == std::vector<std::string>{"\xC3\x84"}));
}

// A commit-flash hide armed by a single-output commit must not fire into
// the next gesture's progress display (fast typing inside the flash
// window).
void commitFlashHideDoesNotHideNextProgress() {
    Fixture f;
    EngineConfig cfg;
    cfg.overlay.enabled = true;
    cfg.overlay.progressBar = true;
    f.reconfigure(cfg);
    // Single-output gesture: progress shows, the leader commits and arms
    // the flash hide.
    f.press(kKeyS, "s");
    f.space();
    f.release(kKeyS);
    // The next gesture starts within the flash window.
    f.press(kKeyA, "a");
    const int hidesBefore = f.overlay.hides;
    f.timers.advanceMs(200);               // past the flash-hide deadline
    CHECK(f.overlay.hides == hidesBefore); // canceled, not fired mid-gesture
    CHECK(f.space() == D::Consume);        // the gesture is still alive
}

// Window expired, but the next key's event is dispatched BEFORE the window
// timer (epoll can deliver the evdev fd and the timerfd in one batch, in
// either order): the press-side commit must arm the withheld key exactly
// like the timer path, so its orphan release is consumed instead of going
// to the app.
void expiredWindowPressSideArmsRelease() {
    Fixture f;
    CHECK(f.press(kKeyA, "a") == D::Consume);
    f.timers.advanceClockOnlyMs(401); // window over; timer NOT yet fired
    CHECK(f.press(kKeyX, "x") == D::Forward);
    CHECK((f.sink.commits == std::vector<std::string>{"a"}));
    // The release of the still-held 'a' is an orphan (its press was
    // withheld and committed as text): consume it.
    CHECK(f.release(kKeyA) == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"a"}));
}

// A leader after the window has expired no longer triggers: the pending
// char commits and the leader passes through.
void leaderAfterWindowExpiryIsPlain() {
    Fixture f;
    EngineConfig cfg;
    cfg.delay.unlimited = false;
    f.reconfigure(cfg);
    f.press(kKeyS, "s");
    // Let the window timeout fire and commit the plain char; a leader arriving
    // afterwards no longer triggers. (The expiry-before-timer race is covered
    // by expiredWindowPressSideArmsRelease.)
    f.timers.advanceMs(401);
    CHECK((f.sink.commits == std::vector<std::string>{"s"}));
    CHECK(f.space() == D::Forward);
}

// Shortcut modifiers end the gesture and pass through (Ctrl+A stays
// Ctrl+A).
void shortcutModifierEndsGesture() {
    Fixture f;
    f.press(kKeyA, "a");
    CHECK(f.press(kKeyX, "x", 'x', static_cast<uint32_t>(KeyModifier::Ctrl)) ==
          D::Forward);
    CHECK((f.sink.commits == std::vector<std::string>{"a"}));
}

// Repeats of a second held key during a gesture are suppressed.
void otherKeyRepeatSuppressedDuringGesture() {
    Fixture f;
    f.press(kKeyA, "a");
    CHECK(f.repeat(kKeyX, "x") == D::Consume);
    f.release(kKeyA);
    CHECK(f.repeat(kKeyX, "x") == D::Forward); // gesture over: repeats pass
}

// Dual custom split: with leaders on opposite halves, each triggers only
// the other half's inputs.
void dualCustomHandSplit() {
    Fixture f;
    EngineConfig cfg;
    cfg.leader.custom.enabled = true;
    cfg.leader.custom.key = "f";
    cfg.leader.custom.keyCode = static_cast<int>(kKeyF); // left hand
    cfg.leader.custom.key2Enabled = true;
    cfg.leader.custom.key2 = "j";
    cfg.leader.custom.key2Code = static_cast<int>(kKeyJ); // right hand
    f.reconfigure(cfg);
    // 'a' (left hand) + left-hand leader f: downgraded, f is no leader for
    // it. The press of f is another mapped-less key: pending commits plain.
    f.press(kKeyA, "a");
    CHECK(f.press(kKeyF, "f") == D::Forward);
    CHECK((f.sink.commits == std::vector<std::string>{"a"}));
    f.release(kKeyF);
    f.release(kKeyA);
    // 'a' (left hand) + right-hand leader j: triggers.
    f.sink.commits.clear();
    f.press(kKeyA, "a");
    CHECK(f.press(kKeyJ, "j") == D::Consume);
    f.release(kKeyA);
    CHECK(!f.sink.commits.empty());
    CHECK(f.sink.commits.back() == "ä");
}

// Profile shortcut switches the active profile, rebuilds maps and persists.
void profileShortcutSwitches() {
    Fixture f;
    ProfilesData profiles;
    profiles.entries.push_back({"Standard", "mappings.txt", "", false});
    profiles.entries.push_back(
        {"Emoji", "profiles/emoji.txt", "Control+Alt+2", false});
    profiles.active = "Standard";
    f.engine.setProfiles(profiles);
    bool rebuilt = false, persisted = false;
    f.engine.rebuildMaps = [&](const std::string &file) {
        rebuilt = (file == "profiles/emoji.txt");
        UmlautMap m{{"e", {"\U0001f600"}}};
        return std::make_pair(m, m);
    };
    f.engine.persistProfiles = [&](const ProfilesData &p) {
        persisted = (p.active == "Emoji");
    };
    CHECK(f.press('2', "2", '2', KeyModifier::Ctrl | KeyModifier::Alt) ==
          D::Consume);
    CHECK(rebuilt);
    CHECK(persisted);
    CHECK(f.overlay.lastProfileName == "Emoji");
}

// App filter: blacklisted app forwards everything untouched.
void appFilterBlacklist() {
    Fixture f;
    EngineConfig cfg;
    cfg.appFilter.mode = AppFilterMode::Blacklist;
    cfg.appFilter.blacklist = {"firefox"};
    f.reconfigure(cfg);
    f.engine.focusChanged("org.mozilla.firefox");
    CHECK(f.press(kKeyA, "a") == D::Forward);
    CHECK(f.sink.commits.empty());
    f.engine.focusChanged("kate");
    CHECK(f.press(kKeyA, "a") == D::Consume);
}

// Focus change clears the gesture without committing (legacy deactivate).
void focusChangeClearsGesture() {
    Fixture f;
    f.press(kKeyA, "a");
    f.engine.focusChanged("other-app");
    CHECK(f.sink.commits.empty());
    CHECK(f.release(kKeyA) == D::Forward); // no gesture left to consume it
}

// Usage counting: gated on the sort toggle, flushed via the hook.
void usageCountingAndFlush() {
    Fixture f;
    EngineConfig cfg;
    cfg.behavior.sortByFrequency = true;
    f.reconfigure(cfg);
    UsageCounts flushed;
    f.engine.persistUsage = [&](const UsageCounts &c) { flushed = c; };
    f.press(kKeyS, "s");
    f.space();
    f.release(kKeyS);
    f.engine.focusChanged("elsewhere"); // flush point
    CHECK(flushed["s"]["ß"] == 1);
}

// --- opt-in extensions -------------------------------------------------

// A/B: with AutoSelect off (default), long holds behave exactly like the
// legacy window (timeout commits plain; nothing pre-selects).
void autoSelectOffChangesNothing() {
    Fixture fOff;
    fOff.press(kKeyA, "a");
    fOff.timers.advanceMs(1000);
    fOff.release(kKeyA);
    CHECK((fOff.sink.commits == std::vector<std::string>{"a"}));
}

// AutoSelect on: the threshold pre-selects variant 1, release commits it,
// the window timeout never fires.
void autoSelectOnPreselectsFirstVariant() {
    Fixture f;
    EngineConfig cfg;
    cfg.behavior.autoSelect = true;
    cfg.behavior.autoSelectMs = 300;
    f.reconfigure(cfg);
    f.press(kKeyA, "a");
    f.timers.advanceMs(1000);      // past autoSelect AND past the old window
    CHECK(f.sink.commits.empty()); // nothing committed yet: selection only
    f.release(kKeyA);
    CHECK((f.sink.commits == std::vector<std::string>{"ä"}));
}

// AutoSelect composes with leaders: threshold pre-selects, a leader steps.
void autoSelectComposesWithLeaders() {
    Fixture f;
    EngineConfig cfg;
    cfg.behavior.autoSelect = true;
    cfg.behavior.autoSelectMs = 300;
    f.reconfigure(cfg);
    f.press(kKeyA, "a");
    f.timers.advanceMs(400);
    f.space(); // steps from ä to á
    f.release(kKeyA);
    CHECK((f.sink.commits == std::vector<std::string>{"á"}));
}

// A/B: with LeaderAutoRepeat off (default), a held Space's repeats do NOT
// step the selection; each deliberate press steps once.
void leaderRepeatOffDoesNotStep() {
    Fixture f;
    f.press(kKeyA, "a");
    f.space();
    CHECK(f.engine.onKeyEvent(Fixture::ev(KeyAction::Repeat, kKeySpaceCode,
                                          kKeysymSpace, " ")) == D::Consume);
    f.release(kKeyA);
    CHECK((f.sink.commits == std::vector<std::string>{"ä"}));
}

// LeaderAutoRepeat on: the held leader's repeats cycle like fresh presses
// (the legacy hold-to-cycle feel).
void leaderRepeatOnSteps() {
    Fixture f;
    EngineConfig cfg;
    cfg.behavior.leaderAutoRepeat = true;
    f.reconfigure(cfg);
    f.press(kKeyA, "a");
    f.space(); // index 0
    f.engine.onKeyEvent(
        Fixture::ev(KeyAction::Repeat, kKeySpaceCode, kKeysymSpace, " "));
    f.engine.onKeyEvent(
        Fixture::ev(KeyAction::Repeat, kKeySpaceCode, kKeysymSpace, " "));
    f.release(kKeyA); // index 2
    CHECK((f.sink.commits == std::vector<std::string>{"à"}));
}

// Unlimited window: no timeout, the leader still triggers after seconds.
void unlimitedWindowNeverExpires() {
    Fixture f;
    EngineConfig cfg;
    cfg.delay.unlimited = true;
    f.reconfigure(cfg);
    f.press(kKeyS, "s");
    f.timers.advanceMs(10'000);
    CHECK(f.sink.commits.empty());
    CHECK(f.space() == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"ß"}));
}

// --- Alt / AltGr leader paths (N8) --------------------------------------

// Helper: a bare Alt-key press/release (no text, keysym carries the identity).
KeyEvent altPress(uint32_t keysym) {
    return Fixture::ev(KeyAction::Press, kKeyAltCode, keysym, "");
}
KeyEvent altRelease(uint32_t keysym) {
    return Fixture::ev(KeyAction::Release, kKeyAltCode, keysym, "");
}

// A pure Alt modifier only acts as a leader while a gesture is active; with no
// gesture it stays a plain modifier and passes through.
void altLeaderOnlyInsideGesture() {
    Fixture f;
    EngineConfig cfg;
    cfg.leader.alt = true;
    f.reconfigure(cfg);
    CHECK(f.engine.onKeyEvent(altPress(kKeysymAltL)) == D::Forward);
    CHECK(f.engine.onKeyEvent(altRelease(kKeysymAltL)) == D::Forward);
}

// With Alt as leader, it cycles and steps just like Space; the consumed Alt
// release is swallowed so no orphan modifier release reaches the app.
void altLeaderCyclesStepsAndConsumesRelease() {
    Fixture f;
    EngineConfig cfg;
    cfg.leader.alt = true;
    cfg.overlay.enabled = true;
    f.reconfigure(cfg);
    f.press(kKeyA, "a");
    CHECK(f.engine.onKeyEvent(altPress(kKeysymAltL)) == D::Consume);
    CHECK(f.overlay.lastIndex == 0);
    CHECK(f.engine.onKeyEvent(altPress(kKeysymAltL)) == D::Consume);
    CHECK(f.overlay.lastIndex == 1);
    CHECK(f.release(kKeyA) == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"á"}));
    // The Alt release matches the consumed Alt press code: consumed, not
    // forwarded as an orphan.
    CHECK(f.engine.onKeyEvent(altRelease(kKeysymAltL)) == D::Consume);
}

// AltGr as leader triggers a single-output accent the same way.
void altGrLeaderSingleOutput() {
    Fixture f;
    EngineConfig cfg;
    cfg.leader.altGr = true;
    f.reconfigure(cfg);
    f.press(kKeyS, "s");
    CHECK(f.engine.onKeyEvent(altPress(kKeysymAltR)) == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"ß"}));
    CHECK(f.engine.onKeyEvent(altRelease(kKeysymAltR)) == D::Consume);
}

// Alt bypass: an Alt-held printable key inside a gesture resolves to its base
// char and is delivered as text, swallowing the app's Alt+key shortcut.
void altBypassDeliversHeldChar() {
    Fixture f;
    EngineConfig cfg;
    cfg.leader.alt = true;
    f.reconfigure(cfg);
    f.press(kKeyX, "x"); // learn the base char for kKeyX
    f.release(kKeyX);
    f.press(kKeyA, "a"); // gesture active
    CHECK(f.press(kKeyX, "x", 'x', static_cast<uint32_t>(KeyModifier::Alt)) ==
          D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"a", "x"}));
}

// Without an active gesture (and no armed Alt), an Alt+key combo is a real
// shortcut and passes through untouched.
void altShortcutForwardsWithoutGesture() {
    Fixture f;
    EngineConfig cfg;
    cfg.leader.alt = true;
    f.reconfigure(cfg);
    CHECK(f.press(kKeyX, "x", 'x', static_cast<uint32_t>(KeyModifier::Alt)) ==
          D::Forward);
    CHECK(f.sink.commits.empty());
}

// --- Whitelist app filter (N8) ------------------------------------------

// Whitelist is the mirror of blacklist: only listed apps are processed; an
// unknown/empty app is not whitelisted and forwards untouched.
void appFilterWhitelist() {
    Fixture f;
    EngineConfig cfg;
    cfg.appFilter.mode = AppFilterMode::Whitelist;
    cfg.appFilter.whitelist = {"kate"};
    f.reconfigure(cfg);
    // Not whitelisted: forwarded untouched, nothing withheld.
    f.engine.focusChanged("org.mozilla.firefox");
    CHECK(f.press(kKeyA, "a") == D::Forward);
    CHECK(f.sink.commits.empty());
    // Unknown/empty app is not whitelisted either.
    f.engine.focusChanged("");
    CHECK(f.press(kKeyA, "a") == D::Forward);
    // Whitelisted app is processed (the press is withheld).
    f.engine.focusChanged("kate");
    CHECK(f.press(kKeyA, "a") == D::Consume);
    // Substring match, like the blacklist path (focus change clears the
    // still-held gesture without committing).
    f.engine.focusChanged("org.kde.kate");
    CHECK(f.press(kKeyA, "a") == D::Consume);
}

// --- CycleNext / CyclePrev profile hotkeys (N8) -------------------------

// Arbitrary keycode for the arrow-combo presses: matchProfileShortcuts keys
// off keysym+modifiers, so the code is irrelevant here.
constexpr uint32_t kKeyArrow = 100;

struct CycleFixture : Fixture {
    std::string rebuiltFile;
    std::string persistedActive;
    CycleFixture() {
        ProfilesData p;
        p.entries.push_back({"Standard", "mappings.txt", "", false});
        p.entries.push_back({"Emoji", "profiles/emoji.txt", "", false});
        p.entries.push_back({"Symbols", "profiles/sym.txt", "", false});
        p.active = "Standard";
        p.cycleNext = "Control+Alt+Right";
        p.cyclePrev = "Control+Alt+Left";
        engine.setProfiles(p);
        engine.rebuildMaps = [this](const std::string &file) {
            rebuiltFile = file;
            UmlautMap m{{"a", {"ä", "á", "à"}}};
            return std::make_pair(m, m);
        };
        engine.persistProfiles = [this](const ProfilesData &pd) {
            persistedActive = pd.active;
        };
    }
    D cycleNext() {
        return engine.onKeyEvent(ev(KeyAction::Press, kKeyArrow, kKeysymRight,
                                    "", KeyModifier::Ctrl | KeyModifier::Alt));
    }
    D cyclePrev() {
        return engine.onKeyEvent(ev(KeyAction::Press, kKeyArrow, kKeysymLeft,
                                    "", KeyModifier::Ctrl | KeyModifier::Alt));
    }
};

// CycleNext walks the profile list in order and wraps; CyclePrev steps back.
void profileCycleNextPrevWraps() {
    CycleFixture f;
    CHECK(f.cycleNext() == D::Consume);
    CHECK(f.overlay.lastProfileName == "Emoji");
    CHECK(f.rebuiltFile == "profiles/emoji.txt");
    CHECK(f.persistedActive == "Emoji");
    CHECK(f.cycleNext() == D::Consume);
    CHECK(f.overlay.lastProfileName == "Symbols");
    CHECK(f.cycleNext() == D::Consume); // wrap
    CHECK(f.overlay.lastProfileName == "Standard");
    CHECK(f.cyclePrev() == D::Consume); // back-wrap
    CHECK(f.overlay.lastProfileName == "Symbols");
}

// With favorites set, the cycle set is only the favorites; from a non-favorite
// active, next lands on the first favorite and prev on the last.
void profileCycleFavoritesOnly() {
    CycleFixture f;
    ProfilesData p;
    p.entries.push_back({"Standard", "mappings.txt", "", false});
    p.entries.push_back({"Emoji", "profiles/emoji.txt", "", true});
    p.entries.push_back({"Symbols", "profiles/sym.txt", "", true});
    p.active = "Standard";
    p.cycleNext = "Control+Alt+Right";
    p.cyclePrev = "Control+Alt+Left";
    f.engine.setProfiles(p);
    f.cycleNext();
    CHECK(f.overlay.lastProfileName == "Emoji"); // first favorite
    f.engine.setProfiles(p);                     // reset active to Standard
    f.cyclePrev();
    CHECK(f.overlay.lastProfileName == "Symbols"); // last favorite
}

// --- Live re-sort on recordUsage (N8) -----------------------------------

// With SortByFrequency on, a commit bumps the usage counter and re-sorts the
// runtime cycle live, so the next gesture on the same base sees the new order.
void liveResortReordersNextGesture() {
    Fixture f;
    EngineConfig cfg;
    cfg.behavior.sortByFrequency = true;
    cfg.overlay.enabled = true;
    f.reconfigure(cfg);
    // First gesture: cycle to the last variant (à) and commit it.
    f.press(kKeyA, "a");
    f.space();
    f.space();
    f.space(); // index 2 -> à
    f.release(kKeyA);
    CHECK((f.sink.commits == std::vector<std::string>{"à"}));
    CHECK(f.engine.usageCounts().at("a").at("à") == 1);
    // Next gesture: à sorted to the front, first leader highlights it.
    f.press(kKeyA, "a");
    f.space();
    CHECK((f.overlay.lastVariants == std::vector<std::string>{"à", "ä", "á"}));
    CHECK(f.overlay.lastIndex == 0);
    f.release(kKeyA);
    CHECK((f.sink.commits == std::vector<std::string>{"à", "à"}));
}

// --- Profile switch mid-gesture (N8) ------------------------------------

// A profile switch while a key is waiting commits the plain char via the old
// map first, then swaps maps for the next gesture; the orphan release forwards.
void profileSwitchMidWaitingCommitsPlain() {
    CycleFixture f;
    f.press(kKeyA, "a"); // waiting
    CHECK(f.cycleNext() == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"a"})); // plain, old map
    CHECK(f.overlay.lastProfileName == "Emoji");
    CHECK(f.rebuiltFile == "profiles/emoji.txt");
    CHECK(f.release(kKeyA) == D::Forward); // gesture already torn down
}

// A profile switch while cycling commits the highlighted variant, not the
// plain char.
void profileSwitchMidCyclingCommitsSelection() {
    CycleFixture f;
    f.press(kKeyA, "a");
    f.space(); // cycling, index 0 -> ä
    CHECK(f.cycleNext() == D::Consume);
    CHECK((f.sink.commits == std::vector<std::string>{"ä"}));
}

} // namespace

int main() {
    tapCommitsPlainOnRelease();
    singleOutputLeaderCommitsInstantly();
    cyclingStepsAndCommitsOnRelease();
    reverseLeaderStartsAtLastVariant();
    rolloverSecondMappedKey();
    rolloverOtherKeyPassesThrough();
    minHoldDeadZone();
    minHoldLeaderInsideWindowTriggers();
    minHoldAboveMaxAppliesWhenUnlimited();
    windowTimeoutCommitsAndRestartsPerWindow();
    uppercaseWindowApplies();
    multibyteUppercaseWindowApplies();
    commitFlashHideDoesNotHideNextProgress();
    expiredWindowPressSideArmsRelease();
    leaderAfterWindowExpiryIsPlain();
    shortcutModifierEndsGesture();
    otherKeyRepeatSuppressedDuringGesture();
    dualCustomHandSplit();
    profileShortcutSwitches();
    appFilterBlacklist();
    focusChangeClearsGesture();
    usageCountingAndFlush();
    autoSelectOffChangesNothing();
    autoSelectOnPreselectsFirstVariant();
    autoSelectComposesWithLeaders();
    leaderRepeatOffDoesNotStep();
    leaderRepeatOnSteps();
    unlimitedWindowNeverExpires();
    altLeaderOnlyInsideGesture();
    altLeaderCyclesStepsAndConsumesRelease();
    altGrLeaderSingleOutput();
    altBypassDeliversHeldChar();
    altShortcutForwardsWithoutGesture();
    appFilterWhitelist();
    profileCycleNextPrevWraps();
    profileCycleFavoritesOnly();
    liveResortReordersNextGesture();
    profileSwitchMidWaitingCommitsPlain();
    profileSwitchMidCyclingCommitsSelection();

    if (failures == 0) {
        std::printf("ALL OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
