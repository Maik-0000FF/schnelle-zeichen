// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_ENGINE_ENGINE_H
#define SCHNELLE_ZEICHEN_ENGINE_ENGINE_H

// The cycling engine: the legacy keyEvent orchestration ported onto the
// clean three-state KeyEvent signal and factored over the pure units
// (accent_window, leader_classify, app_filter, gesture_state). The engine
// is event-loop-free (TimerPort) and UI-free (OverlayPort, TextSink), so
// every behavior is unit-testable with fakes.
//
// Commit contract: TextSink::commit calls are serialized by the backend
// BEFORE any subsequently forwarded key event reaches the app (the spike's
// two-channel ordering requirement).
//
// Ported semantics (1:1 unless named): tap commits the plain char on
// release; a leader inside the accent window starts cycling (multi-variant,
// commit on release) or commits the single output immediately; a leader
// below min-hold commits the plain char and passes through (rollover dead
// zone); any other key press ends the gesture by committing pending state
// first (the rollover that keeps fast typing fast); window timeout commits
// the plain char (repeats restart per window); committed-key repeats are
// suppressed after single-output commits (the "üu" guard).
//
// Opt-in extensions (default off = exact legacy): [Delay]/Unlimited (no
// upper bound, popup feel) and [Behavior]/AutoSelect (long press
// pre-selects variant 1; release commits).

#include "TextSink.h"
#include "combo_parse.h"
#include "engine_config.h"
#include "gesture_state.h"
#include "leader_classify.h"
#include "mappings_loader.h" // UmlautMap, ProfilesData, UsageCounts
#include "overlay_port.h"
#include "timer_port.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace schnelle_zeichen {

class Engine {
public:
    enum class Decision { Forward, Consume };

    Engine(TextSink &sink, OverlayPort &overlay, TimerPort &timers);

    void setConfig(const EngineConfig &config);
    // Runtime map (merge-composed, frequency-sorted) plus the stored-order
    // map the live usage re-sort ties against.
    void setMappings(UmlautMap runtime, UmlautMap stored);
    void setProfiles(ProfilesData profiles);
    void setUsageCounts(UsageCounts counts);

    Decision onKeyEvent(const KeyEvent &event);

    // Focus moved to another app/window: clears the gesture (legacy
    // deactivate) and flushes pending usage counts.
    void focusChanged(const std::string &appId);

    // Wiring hooks (set by the application shell; all optional).
    // Rebuild runtime+stored maps for the given profile File field.
    std::function<std::pair<UmlautMap, UmlautMap>(const std::string &file)>
        rebuildMaps;
    // Persist the profiles data after an Active switch.
    std::function<void(const ProfilesData &)> persistProfiles;
    // Persist the usage counters (batched: focus change, periodic, off).
    std::function<void(const UsageCounts &)> persistUsage;

    const UsageCounts &usageCounts() const { return usageCounts_; }

private:
    Decision handlePress(const KeyEvent &event, const std::string &keyChar,
                         bool altBypass);
    Decision handleRepeat(const KeyEvent &event);
    Decision handleRelease(const KeyEvent &event);
    Decision handleLeader(const KeyEvent &event, LeaderType type);
    Decision startGesture(const KeyEvent &event, const std::string &keyChar);

    bool matchProfileShortcuts(const KeyEvent &event);
    void switchToProfileName(const std::string &name);
    void cycleActiveProfile(int delta);

    // The single commit choke points.
    void commitText(const std::string &text);
    void commitVariant(const std::string &base, const std::string &variant);
    void commitPendingKey(); // waiting phase -> plain char
    void commitCyclingValue();

    void recordUsage(const std::string &base, const std::string &variant);
    void flushUsage();
    void applyUsageTracking();

    void scheduleWindowTimeout();
    void scheduleAutoSelect();
    void enterCycling(const std::string &input, size_t startIndex);

    // Overlay choreography (all no-ops when the overlay is disabled).
    void overlayShowVariants(const std::vector<std::string> &variants,
                             int index);
    void overlayHideAll();
    void scheduleTriggerOverlay(const std::string &keyChar);
    void startProgressOverlay(const std::string &keyChar);
    void flashCommitOverlay(const std::vector<std::string> &variants);
    void hideTriggerOverlay();
    void cancelTimer(TimerPort::TimerId &id);

    void learnBaseChar(const KeyEvent &event);
    std::string baseCharFor(uint32_t code) const;

    const std::vector<std::string> *variantsFor(const std::string &input) const;
    uint64_t elapsedUsec() const;

    TextSink &sink_;
    OverlayPort &overlay_;
    TimerPort &timers_;

    EngineConfig config_;
    LeaderSetup leaderSetup_;
    GestureState state_;

    UmlautMap umlautMap_;
    UmlautMap storedMap_;
    ProfilesData profiles_;
    UsageCounts usageCounts_;
    bool usageDirty_ = false;
    TimerPort::TimerId usageFlushTimer_ = TimerPort::kInvalidTimer;
    TimerPort::TimerId profileFlashTimer_ = TimerPort::kInvalidTimer;

    struct ProfileShortcut {
        ShortcutCombo combo;
        std::string name;
    };
    std::vector<ProfileShortcut> profileSelectShortcuts_;
    ShortcutCombo cycleNextCombo_;
    ShortcutCombo cyclePrevCombo_;

    std::unordered_map<uint32_t, std::string> baseCharByCode_;
    std::string currentApp_;
    bool overlayVisible_ = false;
};

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_ENGINE_ENGINE_H
