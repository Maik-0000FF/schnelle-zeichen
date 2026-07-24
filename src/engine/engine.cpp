// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.h"

#include "accent_window.h"
#include "app_filter.h"
#include "keysyms.h"
#include "log.h"
#include "profile_cycle.h"
#include "usage_sort.h"

#include <utility>

namespace schnelle_zeichen {

namespace {

// Batched usage flush interval and the overlay flash timings, from legacy.
constexpr int kUsageFlushIntervalMs = 60'000;
constexpr int kCommitFlashMs = 150;
constexpr int kProfileFlashMs = 1'500;

// Ctrl/Alt/Super turn a key into a shortcut. Shift is excluded (uppercase
// accents); AltGr is excluded like the legacy check (it selects level-3
// characters rather than shortcuts).
bool hasShortcutModifiers(uint32_t mods) {
    return (mods & KeyModifier::Ctrl) != 0 || (mods & KeyModifier::Alt) != 0 ||
           (mods & KeyModifier::Super) != 0;
}

// Every modifier that can change which character a key produces (the
// base-char learning gate, legacy charChangingModifiers).
bool hasCharChangingModifiers(uint32_t mods) {
    return (mods & KeyModifier::Shift) != 0 ||
           (mods & KeyModifier::Ctrl) != 0 || (mods & KeyModifier::Alt) != 0 ||
           (mods & KeyModifier::AltGr) != 0 ||
           (mods & KeyModifier::Super) != 0 ||
           (mods & KeyModifier::CapsLock) != 0;
}

} // namespace

Engine::Engine(TextSink &sink, OverlayPort &overlay, TimerPort &timers)
    : sink_(sink), overlay_(overlay), timers_(timers) {}

void Engine::setConfig(const EngineConfig &config) {
    config_ = config;
    leaderSetup_ = buildLeaderSetup(config_.leader.custom);
    if (!leaderSetup_.custom1Char.empty() &&
        umlautMap_.count(leaderSetup_.custom1Char) != 0) {
        warn("custom leader 1 '" + leaderSetup_.custom1Char +
             "' is also a mapped input, it cannot trigger its own mapping");
    }
    if (!leaderSetup_.custom2Char.empty() &&
        umlautMap_.count(leaderSetup_.custom2Char) != 0) {
        warn("custom leader 2 '" + leaderSetup_.custom2Char +
             "' is also a mapped input, it cannot trigger its own mapping");
    }
    applyUsageTracking();
}

void Engine::setMappings(UmlautMap runtime, UmlautMap stored) {
    umlautMap_ = std::move(runtime);
    storedMap_ = std::move(stored);
}

void Engine::setProfiles(ProfilesData profiles) {
    profiles_ = std::move(profiles);
    profileSelectShortcuts_.clear();
    for (const auto &p : profiles_.entries) {
        ShortcutCombo combo = parseShortcutCombo(p.selectKey);
        if (combo.valid()) {
            profileSelectShortcuts_.push_back({combo, p.name});
        }
    }
    cycleNextCombo_ = parseShortcutCombo(profiles_.cycleNext);
    cyclePrevCombo_ = parseShortcutCombo(profiles_.cyclePrev);
}

void Engine::setUsageCounts(UsageCounts counts) {
    usageCounts_ = std::move(counts);
}

uint64_t Engine::elapsedUsec() const {
    return timers_.nowUsec() - state_.startUsec;
}

const std::vector<std::string> *
Engine::variantsFor(const std::string &input) const {
    const auto it = umlautMap_.find(input);
    if (it == umlautMap_.end() || it->second.empty()) {
        return nullptr;
    }
    return &it->second;
}

// ---------------------------------------------------------------- commits

void Engine::commitText(const std::string &text) { sink_.commit(text); }

void Engine::commitVariant(const std::string &base,
                           const std::string &variant) {
    sink_.commit(variant);
    recordUsage(base, variant);
}

void Engine::commitPendingKey() {
    if (!state_.waitingKey) {
        return;
    }
    hideTriggerOverlay();
    commitText(*state_.waitingKey);
    state_.resetWaitingGesture();
    cancelTimer(state_.windowTimer);
    cancelTimer(state_.autoSelectTimer);
}

void Engine::commitCyclingValue() {
    if (!state_.cyclingInput) {
        return;
    }
    if (const auto *variants = variantsFor(*state_.cyclingInput)) {
        if (state_.cyclingIndex < variants->size()) {
            commitVariant(*state_.cyclingInput,
                          (*variants)[state_.cyclingIndex]);
        }
    }
    state_.resetWaitingGesture();
    state_.resetCycling();
    overlayHideAll();
}

// ------------------------------------------------------------------ usage

void Engine::recordUsage(const std::string &base, const std::string &variant) {
    if (base.empty() || variant.empty() || !config_.behavior.sortByFrequency) {
        return;
    }
    ++usageCounts_[base][variant];
    usageDirty_ = true;
    // Live re-sort of this key's cycle from its STORED order, so runtime and
    // editor preview agree on the tie-break (legacy behavior).
    const auto sit = storedMap_.find(base);
    const auto it = umlautMap_.find(base);
    if (sit != storedMap_.end() && it != umlautMap_.end()) {
        it->second = sortVariantsByUsage(sit->second, usageCounts_[base]);
    }
}

void Engine::flushUsage() {
    if (!usageDirty_) {
        return;
    }
    if (persistUsage) {
        persistUsage(usageCounts_);
    }
    usageDirty_ = false;
}

void Engine::applyUsageTracking() {
    if (config_.behavior.sortByFrequency) {
        if (usageFlushTimer_ == TimerPort::kInvalidTimer) {
            const uint64_t interval =
                static_cast<uint64_t>(kUsageFlushIntervalMs) * kUsecPerMs;
            usageFlushTimer_ = timers_.schedule(interval, [this, interval] {
                flushUsage();
                usageFlushTimer_ = timers_.schedule(interval, [this] {
                    // Re-arm indirection lives in applyUsageTracking's
                    // lambda; one level is enough because each fire
                    // reschedules through the same path.
                    flushUsage();
                    usageFlushTimer_ = TimerPort::kInvalidTimer;
                    applyUsageTracking();
                });
            });
        }
    } else {
        flushUsage();
        cancelTimer(usageFlushTimer_);
    }
}

// --------------------------------------------------------------- profiles

bool Engine::matchProfileShortcuts(const KeyEvent &event) {
    if (cycleNextCombo_.matches(event.modifiers, event.keysym)) {
        cycleActiveProfile(+1);
        return true;
    }
    if (cyclePrevCombo_.matches(event.modifiers, event.keysym)) {
        cycleActiveProfile(-1);
        return true;
    }
    for (const auto &s : profileSelectShortcuts_) {
        if (s.combo.matches(event.modifiers, event.keysym)) {
            switchToProfileName(s.name);
            return true;
        }
    }
    return false;
}

void Engine::switchToProfileName(const std::string &name) {
    if (name.empty() || name == profiles_.active) {
        return;
    }
    bool known = false;
    std::string file;
    for (const auto &p : profiles_.entries) {
        if (p.name == name) {
            known = true;
            file = p.file;
            break;
        }
    }
    if (!known) {
        return;
    }
    // Commit any in-flight gesture against the OLD map before switching
    // (cycling and waiting are mutually exclusive, legacy invariant).
    if (state_.cyclingInput) {
        commitCyclingValue();
    } else if (state_.waitingKey) {
        commitPendingKey();
    }
    cancelTimer(state_.windowTimer);
    cancelTimer(state_.overlayShowTimer);
    cancelTimer(state_.overlayHideTimer);
    cancelTimer(state_.autoSelectTimer);
    state_.resetWaitingGesture();
    state_.resetCycling();

    profiles_.active = name;
    if (rebuildMaps) {
        auto maps = rebuildMaps(file);
        setMappings(std::move(maps.first), std::move(maps.second));
    }
    if (persistProfiles) {
        persistProfiles(profiles_);
    }
    cancelTimer(profileFlashTimer_);
    overlay_.showProfileName(name);
    overlayVisible_ = true;
    profileFlashTimer_ = timers_.schedule(
        static_cast<uint64_t>(kProfileFlashMs) * kUsecPerMs, [this] {
            profileFlashTimer_ = TimerPort::kInvalidTimer;
            overlay_.hide();
            overlayVisible_ = false;
        });
}

void Engine::cycleActiveProfile(int delta) {
    std::vector<CycleEntry> entries;
    entries.reserve(profiles_.entries.size());
    for (const auto &p : profiles_.entries) {
        entries.push_back({p.name, p.favorite});
    }
    switchToProfileName(
        cycleTarget(cycleNames(entries), profiles_.active, delta));
}

// ----------------------------------------------------------------- timers

void Engine::cancelTimer(TimerPort::TimerId &id) {
    if (id != TimerPort::kInvalidTimer) {
        timers_.cancel(id);
        id = TimerPort::kInvalidTimer;
    }
}

void Engine::scheduleWindowTimeout() {
    cancelTimer(state_.windowTimer);
    if (!state_.waitingKey) {
        return;
    }
    const AccentWindow w = effectiveWindow(config_.delay, *state_.waitingKey);
    if (w.unlimited) {
        return; // opt-in popup feel: the window never times out
    }
    state_.windowTimer =
        timers_.schedule(static_cast<uint64_t>(w.maxMs) * kUsecPerMs, [this] {
            state_.windowTimer = TimerPort::kInvalidTimer;
            if (!state_.waitingKey) {
                return;
            }
            // Window elapsed without a leader: commit the plain char now
            // (the timer-flush; legacy scheduleTimeout callback). A still-
            // held key restarts a gesture per window via its repeats, so
            // the arming does NOT suppress repeats; it only consumes the
            // orphan release of the withheld press.
            const uint32_t code = state_.waitingKeyCode;
            const bool held = state_.inputKeyPressed;
            hideTriggerOverlay();
            commitText(*state_.waitingKey);
            state_.resetWaitingGesture();
            cancelTimer(state_.autoSelectTimer);
            if (held) {
                state_.armCommitted(code, /*suppressRepeats=*/false);
            }
        });
}

void Engine::scheduleAutoSelect() {
    cancelTimer(state_.autoSelectTimer);
    if (!config_.behavior.autoSelect) {
        return; // opt-in off: no timer, no behavior change
    }
    state_.autoSelectTimer = timers_.schedule(
        static_cast<uint64_t>(config_.behavior.autoSelectMs) * kUsecPerMs,
        [this] {
            state_.autoSelectTimer = TimerPort::kInvalidTimer;
            if (!state_.waitingKey || state_.cyclingInput ||
                !state_.inputKeyPressed) {
                return;
            }
            // Long press pre-selects variant 1; the release commits it.
            cancelTimer(state_.windowTimer);
            enterCycling(*state_.waitingKey, 0);
        });
}

void Engine::enterCycling(const std::string &input, size_t startIndex) {
    const auto *variants = variantsFor(input);
    if (variants == nullptr) {
        return;
    }
    state_.cyclingInput = input;
    state_.cyclingIndex = startIndex;
    state_.waitingKey.reset(); // cycling owns the gesture; code stays valid
    overlayShowVariants(*variants, static_cast<int>(startIndex));
    overlay_.freezeProgress();
}

// ---------------------------------------------------------------- overlay

void Engine::overlayShowVariants(const std::vector<std::string> &variants,
                                 int index) {
    if (!config_.overlay.enabled) {
        return;
    }
    cancelTimer(profileFlashTimer_);
    overlay_.show(variants, index);
    overlayVisible_ = true;
}

void Engine::overlayHideAll() {
    if (!config_.overlay.enabled || !overlayVisible_) {
        return;
    }
    overlay_.hide();
    overlayVisible_ = false;
}

void Engine::startProgressOverlay(const std::string &keyChar) {
    if (!config_.overlay.enabled || !config_.overlay.progressBar) {
        return;
    }
    const auto *variants = variantsFor(keyChar);
    if (variants == nullptr) {
        return;
    }
    const AccentWindow w = effectiveWindow(config_.delay, keyChar);
    // Unlimited mode has no expiring window, so no countdown is sent (a
    // counting-down bar that never expires would mislead). windowMs = 0 makes
    // the daemon hide the bar entirely; the lead-in still gates the panel
    // reveal through the shared timeline.
    const int windowMs =
        w.unlimited ? 0 : (w.maxMs > w.minMs ? w.maxMs - w.minMs : 0);
    // Long-press auto-select point on the same timeline (0 = off), so the
    // bar can mark from where a plain release commits the first variant.
    const int holdMs =
        config_.behavior.autoSelect ? config_.behavior.autoSelectMs : 0;
    overlay_.setProgress(w.minMs, windowMs, holdMs, state_.startUsec);
    overlayShowVariants(*variants, kNoHighlightIndex);
}

void Engine::scheduleTriggerOverlay(const std::string &keyChar) {
    if (!config_.overlay.enabled || !config_.overlay.showOnTrigger) {
        return;
    }
    const auto *variants = variantsFor(keyChar);
    if (variants == nullptr) {
        return;
    }
    cancelTimer(state_.overlayHideTimer);
    const AccentWindow w = effectiveWindow(config_.delay, keyChar);
    if (w.minMs <= 0) {
        overlayShowVariants(*variants, kNoHighlightIndex);
        return;
    }
    const std::vector<std::string> copy = *variants;
    cancelTimer(state_.overlayShowTimer);
    state_.overlayShowTimer = timers_.schedule(
        static_cast<uint64_t>(w.minMs) * kUsecPerMs, [this, keyChar, copy] {
            state_.overlayShowTimer = TimerPort::kInvalidTimer;
            if (state_.waitingKey && *state_.waitingKey == keyChar &&
                !state_.cyclingInput) {
                overlayShowVariants(copy, kNoHighlightIndex);
            }
        });
}

void Engine::flashCommitOverlay(const std::vector<std::string> &variants) {
    cancelTimer(state_.overlayShowTimer);
    overlayShowVariants(variants, 0);
    cancelTimer(state_.overlayHideTimer);
    state_.overlayHideTimer = timers_.schedule(
        static_cast<uint64_t>(kCommitFlashMs) * kUsecPerMs, [this] {
            state_.overlayHideTimer = TimerPort::kInvalidTimer;
            overlayHideAll();
        });
}

void Engine::hideTriggerOverlay() {
    cancelTimer(state_.overlayShowTimer);
    if (config_.overlay.enabled &&
        (config_.overlay.showOnTrigger || config_.overlay.progressBar)) {
        overlayHideAll();
    }
}

// -------------------------------------------------------------- base char

void Engine::learnBaseChar(const KeyEvent &event) {
    if (event.code == 0 || event.text.empty()) {
        return;
    }
    if (hasCharChangingModifiers(event.modifiers)) {
        return;
    }
    baseCharByCode_[event.code] = event.text;
}

std::string Engine::baseCharFor(uint32_t code) const {
    const auto it = baseCharByCode_.find(code);
    return it == baseCharByCode_.end() ? std::string() : it->second;
}

// ------------------------------------------------------------------ focus

void Engine::focusChanged(const std::string &appId) {
    cancelTimer(state_.windowTimer);
    cancelTimer(state_.overlayShowTimer);
    cancelTimer(state_.overlayHideTimer);
    cancelTimer(state_.autoSelectTimer);
    state_.resetWaitingGesture();
    state_.resetCycling();
    state_.clearCommitted();
    state_.consumedAltCode = 0;
    overlayHideAll();
    flushUsage();
    currentApp_ = appId;
}

// -------------------------------------------------------------- key event

Engine::Decision Engine::onKeyEvent(const KeyEvent &event) {
    if (isFilteredApp(config_.appFilter, currentApp_)) {
        return Decision::Forward;
    }
    switch (event.action) {
    case KeyAction::Release:
        return handleRelease(event);
    case KeyAction::Repeat:
        return handleRepeat(event);
    case KeyAction::Press:
        break;
    }

    learnBaseChar(event);

    // Profile-switch shortcuts intercept before gesture handling.
    if (matchProfileShortcuts(event)) {
        return Decision::Consume;
    }

    // Pure modifier presses pass through, except Alt/AltGr acting as leader
    // during an active gesture.
    bool altAsLeader = false;
    if (isModifierSym(event.keysym)) {
        altAsLeader = ((config_.leader.alt && isAltSym(event.keysym)) ||
                       (config_.leader.altGr && isAltGrSym(event.keysym))) &&
                      state_.gestureActive();
        if (!altAsLeader) {
            return Decision::Forward;
        }
    }

    // Window elapsed but this key arrived before the timer fired: commit
    // the pending char first, then continue as a normal key (the leader
    // can no longer trigger).
    if (state_.waitingKey &&
        isWindowExpired(effectiveWindow(config_.delay, *state_.waitingKey),
                        elapsedUsec())) {
        commitPendingKey();
    }

    std::string keyChar = event.text;
    bool altBypass = false;
    if (!altAsLeader && hasShortcutModifiers(event.modifiers)) {
        // Alt-only during an active gesture is the leader bypass: resolve
        // the physical key's base character (the real layout, observed).
        const bool altOnly = (event.modifiers & KeyModifier::Alt) != 0 &&
                             (event.modifiers & KeyModifier::Ctrl) == 0 &&
                             (event.modifiers & KeyModifier::Super) == 0;
        const bool bypass =
            (config_.leader.alt || config_.leader.altGr) && altOnly &&
            (state_.gestureActive() || state_.consumedAltCode != 0);
        if (!bypass) {
            // Shortcut: commit pending state, then let it through.
            commitPendingKey();
            commitCyclingValue();
            state_.inputKeyPressed = false;
            return Decision::Forward;
        }
        const std::string base = baseCharFor(event.code);
        if (!base.empty()) {
            keyChar = base;
        }
        altBypass = true;
    }

    return handlePress(event, keyChar, altBypass);
}

Engine::Decision Engine::handlePress(const KeyEvent &event,
                                     const std::string &keyChar,
                                     bool altBypass) {
    // Leader handling.
    LeaderType leaderType =
        classifyLeader(config_.leader, leaderSetup_, event.keysym,
                       static_cast<int>(event.code));
    if (leaderType != LeaderType::None && state_.gestureActive() &&
        !isDualCustomAllowed(leaderSetup_, leaderType,
                             static_cast<int>(state_.waitingKeyCode))) {
        leaderType = LeaderType::None;
    }
    if (leaderType != LeaderType::None) {
        return handleLeader(event, leaderType);
    }

    // Mapped input key: start (or restart) the gesture.
    if (!keyChar.empty() && umlautMap_.find(keyChar) != umlautMap_.end()) {
        // A fresh press of the key already waiting/cycling cannot happen on
        // the raw signal (that would be a Repeat), so no repeat guards are
        // needed here; they live in handleRepeat.
        commitPendingKey();
        commitCyclingValue();
        return startGesture(event, keyChar);
    }

    // Any other key ends the gesture: commit pending state first (the
    // rollover that keeps fast typing fast), then pass the key through.
    commitPendingKey();
    commitCyclingValue();
    state_.consumedAltCode = 0;
    if (altBypass && !keyChar.empty() &&
        (keyChar.size() > 1 || static_cast<unsigned char>(keyChar[0]) >= ' ')) {
        // The key carries an Alt modifier that would fire app shortcuts:
        // deliver its character instead and consume the event.
        commitText(keyChar);
        return Decision::Consume;
    }
    return Decision::Forward;
}

Engine::Decision Engine::startGesture(const KeyEvent &event,
                                      const std::string &keyChar) {
    state_.waitingKey = keyChar;
    state_.waitingKeyCode = event.code;
    state_.inputKeyPressed = true;
    state_.startUsec = timers_.nowUsec();
    scheduleWindowTimeout();
    scheduleAutoSelect();
    if (config_.overlay.progressBar) {
        startProgressOverlay(keyChar);
    } else {
        scheduleTriggerOverlay(keyChar);
    }
    return Decision::Consume; // withhold the press until the gesture decides
}

Engine::Decision Engine::handleLeader(const KeyEvent &event, LeaderType type) {
    (void)type;
    const bool isAlt = isAltLeaderSym(event.keysym);
    cancelTimer(state_.overlayShowTimer);

    // Min-hold dead zone: a leader below the lower bound is plain typing.
    if (!state_.cyclingInput && state_.waitingKey &&
        isBeforeMinHold(effectiveWindow(config_.delay, *state_.waitingKey),
                        elapsedUsec())) {
        const uint32_t code = state_.waitingKeyCode;
        state_.armCommitted(code, /*suppressRepeats=*/true);
        commitPendingKey();
        return Decision::Forward; // the leader acts as a normal key
    }

    // Cycling: step to the next variant.
    if (state_.cyclingInput) {
        if (!state_.inputKeyPressed) {
            state_.resetCycling();
            overlayHideAll();
            return Decision::Forward;
        }
        const auto *variants = variantsFor(*state_.cyclingInput);
        if (variants != nullptr) {
            if (variants->size() > 1) {
                const int n = static_cast<int>(variants->size());
                const int next =
                    (static_cast<int>(state_.cyclingIndex) +
                     leaderStep(config_.leader, leaderSetup_, event.keysym,
                                static_cast<int>(event.code)) +
                     n) %
                    n;
                state_.cyclingIndex = static_cast<size_t>(next);
                overlayShowVariants(*variants,
                                    static_cast<int>(state_.cyclingIndex));
            }
            // Single-output: nothing to step; the press stays consumed.
            if (isAlt) {
                state_.consumedAltCode = event.code;
            }
            return Decision::Consume;
        }
    }

    // First leader inside the window: start cycling or commit the single
    // output immediately.
    if (state_.waitingKey &&
        !isWindowExpired(effectiveWindow(config_.delay, *state_.waitingKey),
                         elapsedUsec())) {
        const auto *variants = variantsFor(*state_.waitingKey);
        if (variants != nullptr) {
            cancelTimer(state_.autoSelectTimer);
            if (variants->size() > 1) {
                const size_t startIdx =
                    leaderStep(config_.leader, leaderSetup_, event.keysym,
                               static_cast<int>(event.code)) < 0
                        ? variants->size() - 1
                        : 0;
                enterCycling(*state_.waitingKey, startIdx);
            } else {
                // Single output: commit on the leader press (historic).
                if (overlayVisible_) {
                    flashCommitOverlay(*variants);
                } else {
                    hideTriggerOverlay();
                }
                const uint32_t code = state_.waitingKeyCode;
                commitVariant(*state_.waitingKey, (*variants)[0]);
                state_.armCommitted(code, /*suppressRepeats=*/true);
                state_.resetWaitingGesture();
            }
            cancelTimer(state_.windowTimer);
            if (isAlt) {
                state_.consumedAltCode = event.code;
            }
            return Decision::Consume;
        }
    }

    // Not in a gesture: clear stale Alt state and let the leader through.
    state_.consumedAltCode = 0;
    return Decision::Forward;
}

Engine::Decision Engine::handleRepeat(const KeyEvent &event) {
    // Auto-repeat of the gesture key while waiting/cycling: swallow.
    if (state_.gestureActive() && event.code == state_.waitingKeyCode) {
        return Decision::Consume;
    }
    // Opt-in legacy hold-to-cycle: a held leader's auto-repeat steps the
    // selection like a fresh press. Off (default) falls through to the
    // suppression below, so each deliberate press steps exactly once.
    if (config_.behavior.leaderAutoRepeat && state_.cyclingInput) {
        LeaderType type =
            classifyLeader(config_.leader, leaderSetup_, event.keysym,
                           static_cast<int>(event.code));
        if (type != LeaderType::None &&
            isDualCustomAllowed(leaderSetup_, type,
                                static_cast<int>(state_.waitingKeyCode))) {
            return handleLeader(event, type);
        }
    }
    // Committed-key repeats: suppressed after a single-output commit (the
    // "üu" guard); after a window-timeout commit they restart a gesture per
    // window (historic behavior).
    if (state_.committedCode != 0 && event.code == state_.committedCode) {
        if (state_.committedSuppressRepeats) {
            return Decision::Consume;
        }
        state_.clearCommitted();
        if (!event.text.empty() &&
            umlautMap_.find(event.text) != umlautMap_.end()) {
            return startGesture(event, event.text);
        }
        return Decision::Forward;
    }
    // Other keys' repeats during an active gesture: suppressed so a held
    // second key cannot interfere (legacy guard).
    if (state_.gestureActive()) {
        return Decision::Consume;
    }
    return Decision::Forward;
}

Engine::Decision Engine::handleRelease(const KeyEvent &event) {
    // Release of an Alt press consumed as leader: consume it too.
    if (state_.consumedAltCode != 0 && event.code == state_.consumedAltCode) {
        state_.consumedAltCode = 0;
        return Decision::Consume;
    }
    // Release of a committed key whose press was consumed: consume.
    if (state_.committedCode != 0 && event.code == state_.committedCode) {
        state_.clearCommitted();
        return Decision::Consume;
    }
    // Releasing the cycling key commits the selected variant.
    if (state_.cyclingInput && state_.inputKeyPressed &&
        event.code == state_.waitingKeyCode) {
        commitCyclingValue();
        return Decision::Consume;
    }
    // Releasing the waiting key commits the plain char (historic).
    if (state_.waitingKey && state_.inputKeyPressed &&
        event.code == state_.waitingKeyCode) {
        commitPendingKey();
        return Decision::Consume;
    }
    return Decision::Forward;
}

} // namespace schnelle_zeichen
