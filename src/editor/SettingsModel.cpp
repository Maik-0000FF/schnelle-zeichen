// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsModel.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <cstdio>

#include "core/config_dir.h"
#include "core/engine_config.h" // detail::indexedList (index-keyed list sections)
#include "core/ini_io.h"
#include "core/layer_shell_capability.h"
#include "core/profile_paths.h"
#include "core/themes.h"

namespace {

QString settingsFilePath() {
    return QString::fromStdString(
        schnelle_zeichen::configFilePath(schnelle_zeichen::kSettingsFile));
}

QString toBool(bool v) {
    return v ? QStringLiteral("True") : QStringLiteral("False");
}

// Escape a free-text value the way the shared INI layer expects, so the
// engine's parseIni (which unescapes) round-trips it byte-identically.
QString escaped(const QString &s) {
    return QString::fromStdString(
        schnelle_zeichen::escapeIniValue(s.toStdString()));
}

QString iniString(const schnelle_zeichen::IniSection *s, const char *key,
                  const QString &def = QString()) {
    if (s == nullptr)
        return def;
    const std::string *v = schnelle_zeichen::findIniValue(*s, key);
    return v != nullptr ? QString::fromStdString(*v) : def;
}

// A leader's captured physical key. Anything that cannot name a pressable key,
// including an absent or unparseable value, reads back as kNoKeyCode and leaves
// the leader unassigned. Never fabricate a keycode from a malformed value: a
// wrong physical key would trigger the wrong leader and mis-classify its
// keyboard half.
int keyCodeValue(const schnelle_zeichen::IniSection *s, const char *key) {
    const int code = schnelle_zeichen::parseIniInt(s, key, kNoKeyCode);
    return schnelle_zeichen::isUsableKeyCode(code) ? code : kNoKeyCode;
}

} // namespace

SettingsModel::SettingsModel(QObject *parent) : QObject(parent) {
    const auto cap = schnelle_zeichen::detectLayerShellCapability();
    layerShellAvailable_ = cap.supported;
    layerShellSession_ = QString::fromStdString(cap.session);
    layerShellReason_ = QString::fromStdString(cap.reason);

    // Forward all leader-related changes to a single umbrella signal so QML
    // can refresh isActiveLeaderKey-based bindings with one Connections hook.
    connect(this, &SettingsModel::leaderSpaceChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderSpaceReverseChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderLeftChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderRightChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderUpChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderDownChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderAltChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderAltReverseChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderAltGrChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderAltGrReverseChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderLeftReverseChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderRightReverseChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderUpReverseChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::leaderDownReverseChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::customKey1EnabledChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::customKey1Changed, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::customKey1ReverseChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::customKey2EnabledChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::customKey2Changed, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::customKey2ReverseChanged, this,
            &SettingsModel::leadersChanged);
    // A captured (or cleared) key flips whether a custom leader counts as
    // effective, so it must refresh the leader summary and the effective count.
    connect(this, &SettingsModel::customKey1CodeChanged, this,
            &SettingsModel::leadersChanged);
    connect(this, &SettingsModel::customKey2CodeChanged, this,
            &SettingsModel::leadersChanged);

    load();
}

void SettingsModel::setDelayLowercase(int v) {
    if (delayLowercase_ == v)
        return;
    delayLowercase_ = v;
    Q_EMIT delayLowercaseChanged();
    save();
}
void SettingsModel::setDelayUppercase(int v) {
    if (delayUppercase_ == v)
        return;
    delayUppercase_ = v;
    Q_EMIT delayUppercaseChanged();
    save();
}
void SettingsModel::setDelayLowercaseMin(int v) {
    if (delayLowercaseMin_ == v)
        return;
    delayLowercaseMin_ = v;
    Q_EMIT delayLowercaseMinChanged();
    save();
}
void SettingsModel::setDelayUppercaseMin(int v) {
    if (delayUppercaseMin_ == v)
        return;
    delayUppercaseMin_ = v;
    Q_EMIT delayUppercaseMinChanged();
    save();
}
void SettingsModel::setDelayUnlimited(bool v) {
    if (delayUnlimited_ == v)
        return;
    delayUnlimited_ = v;
    Q_EMIT delayUnlimitedChanged();
    save();
}
int SettingsModel::effectiveLeaderCount() const {
    int n = 0;
    if (leaderSpace_)
        ++n;
    if (leaderLeft_)
        ++n;
    if (leaderRight_)
        ++n;
    if (leaderUp_)
        ++n;
    if (leaderDown_)
        ++n;
    if (leaderAlt_)
        ++n;
    if (leaderAltGr_)
        ++n;
    // A custom leader counts only when it is enabled AND has a key captured; an
    // enabled-but-unassigned one cannot trigger anything.
    if (customKey1Enabled_ && customKey1HasKey())
        ++n;
    if (customKey2Enabled_ && customKey2HasKey())
        ++n;
    return n;
}
bool SettingsModel::allowLeaderOff(bool stillEffective) {
    if (stillEffective && effectiveLeaderCount() == 1) {
        Q_EMIT leaderRemovalBlocked();
        return false;
    }
    return true;
}
void SettingsModel::setLeaderSpace(bool v) {
    if (leaderSpace_ == v)
        return;
    if (!v && !allowLeaderOff(leaderSpace_))
        return;
    leaderSpace_ = v;
    Q_EMIT leaderSpaceChanged();
    save();
}
void SettingsModel::setLeaderSpaceReverse(bool v) {
    if (leaderSpaceReverse_ == v)
        return;
    leaderSpaceReverse_ = v;
    Q_EMIT leaderSpaceReverseChanged();
    save();
}
void SettingsModel::setLeaderLeft(bool v) {
    if (leaderLeft_ == v)
        return;
    if (!v && !allowLeaderOff(leaderLeft_))
        return;
    leaderLeft_ = v;
    Q_EMIT leaderLeftChanged();
    save();
}
void SettingsModel::setLeaderRight(bool v) {
    if (leaderRight_ == v)
        return;
    if (!v && !allowLeaderOff(leaderRight_))
        return;
    leaderRight_ = v;
    Q_EMIT leaderRightChanged();
    save();
}
void SettingsModel::setLeaderUp(bool v) {
    if (leaderUp_ == v)
        return;
    if (!v && !allowLeaderOff(leaderUp_))
        return;
    leaderUp_ = v;
    Q_EMIT leaderUpChanged();
    save();
}
void SettingsModel::setLeaderDown(bool v) {
    if (leaderDown_ == v)
        return;
    if (!v && !allowLeaderOff(leaderDown_))
        return;
    leaderDown_ = v;
    Q_EMIT leaderDownChanged();
    save();
}
void SettingsModel::setLeaderAlt(bool v) {
    if (leaderAlt_ == v)
        return;
    if (!v && !allowLeaderOff(leaderAlt_))
        return;
    leaderAlt_ = v;
    Q_EMIT leaderAltChanged();
    save();
}
void SettingsModel::setLeaderAltReverse(bool v) {
    if (leaderAltReverse_ == v)
        return;
    leaderAltReverse_ = v;
    Q_EMIT leaderAltReverseChanged();
    save();
}
void SettingsModel::setLeaderAltGr(bool v) {
    if (leaderAltGr_ == v)
        return;
    if (!v && !allowLeaderOff(leaderAltGr_))
        return;
    leaderAltGr_ = v;
    Q_EMIT leaderAltGrChanged();
    save();
}
void SettingsModel::setLeaderAltGrReverse(bool v) {
    if (leaderAltGrReverse_ == v)
        return;
    leaderAltGrReverse_ = v;
    Q_EMIT leaderAltGrReverseChanged();
    save();
}
void SettingsModel::setLeaderLeftReverse(bool v) {
    if (leaderLeftReverse_ == v)
        return;
    leaderLeftReverse_ = v;
    Q_EMIT leaderLeftReverseChanged();
    save();
}
void SettingsModel::setLeaderRightReverse(bool v) {
    if (leaderRightReverse_ == v)
        return;
    leaderRightReverse_ = v;
    Q_EMIT leaderRightReverseChanged();
    save();
}
void SettingsModel::setLeaderUpReverse(bool v) {
    if (leaderUpReverse_ == v)
        return;
    leaderUpReverse_ = v;
    Q_EMIT leaderUpReverseChanged();
    save();
}
void SettingsModel::setLeaderDownReverse(bool v) {
    if (leaderDownReverse_ == v)
        return;
    leaderDownReverse_ = v;
    Q_EMIT leaderDownReverseChanged();
    save();
}
void SettingsModel::setCustomKey1Enabled(bool v) {
    if (customKey1Enabled_ == v)
        return;
    if (!v && !allowLeaderOff(customKey1Enabled_ && customKey1HasKey()))
        return;
    customKey1Enabled_ = v;
    Q_EMIT customKey1EnabledChanged();
    save();
}
void SettingsModel::setCustomKey1(const QString &v) {
    if (customKey1_ == v)
        return;
    customKey1_ = v;
    Q_EMIT customKey1Changed();
    if (isValidLeaderKey(v))
        save();
}
void SettingsModel::setCustomKey1Reverse(bool v) {
    if (customKey1Reverse_ == v)
        return;
    customKey1Reverse_ = v;
    Q_EMIT customKey1ReverseChanged();
    save();
}
void SettingsModel::setCustomKey2Enabled(bool v) {
    if (customKey2Enabled_ == v)
        return;
    if (!v && !allowLeaderOff(customKey2Enabled_ && customKey2HasKey()))
        return;
    customKey2Enabled_ = v;
    Q_EMIT customKey2EnabledChanged();
    save();
}
void SettingsModel::setCustomKey2(const QString &v) {
    if (customKey2_ == v)
        return;
    customKey2_ = v;
    Q_EMIT customKey2Changed();
    if (isValidLeaderKey(v))
        save();
}
void SettingsModel::setCustomKey2Reverse(bool v) {
    if (customKey2Reverse_ == v)
        return;
    customKey2Reverse_ = v;
    Q_EMIT customKey2ReverseChanged();
    save();
}
// The capture rejects a held modifier, but it cannot reject CapsLock: Qt does
// not report it in the event's modifiers at all. So a press under CapsLock
// still arrives as 'A' rather than 'a'. Fold the character down here, where
// Qt's full Unicode case mapping is available and handles 'Ä' as readily as
// 'A'. The keycode is unaffected either way, so only the label and the
// mapped-input collision check depend on getting this right.
static QString leaderChar(const QString &raw) {
    if (!SettingsModel::isValidLeaderKey(raw))
        return QString();
    const QString folded = raw.toLower();
    // Full case mapping can turn one codepoint into two: Turkish 'İ' (U+0130)
    // folds to 'i' plus a combining dot. That would break the single-codepoint
    // invariant the label and the collision check rely on, and the field would
    // then flag a perfectly good leader as invalid. Keep the unfolded character
    // in that case; it is still exactly one codepoint.
    return SettingsModel::isValidLeaderKey(folded) ? folded : raw;
}

// The only way a keycode enters the config: both halves land together, then one
// save. A code that cannot name a pressable key is stored as kNoKeyCode, which
// reads as "no key assigned" rather than as a leader that can never fire.
void SettingsModel::captureCustomKey1(const QString &ch, int code) {
    const int newCode =
        schnelle_zeichen::isUsableKeyCode(code) ? code : kNoKeyCode;
    // Unassigning the key (kNoKeyCode) drops this leader's effectiveness, so
    // guard it exactly like the enable-off case: refuse when it is the sole
    // effective leader. Assigning a real key only adds effectiveness, never
    // guarded.
    if (newCode == kNoKeyCode &&
        !allowLeaderOff(customKey1Enabled_ && customKey1HasKey()))
        return;
    customKey1Code_ = newCode;
    customKey1_ = leaderChar(ch);
    Q_EMIT customKey1CodeChanged();
    Q_EMIT customKey1Changed();
    save();
}
void SettingsModel::captureCustomKey2(const QString &ch, int code) {
    const int newCode =
        schnelle_zeichen::isUsableKeyCode(code) ? code : kNoKeyCode;
    if (newCode == kNoKeyCode &&
        !allowLeaderOff(customKey2Enabled_ && customKey2HasKey()))
        return;
    customKey2Code_ = newCode;
    customKey2_ = leaderChar(ch);
    Q_EMIT customKey2CodeChanged();
    Q_EMIT customKey2Changed();
    save();
}
namespace {
// X keycodes (evdev code + 8) of the no-character navigation keys offered as
// custom leaders. These are the values a key event carries in the shared
// evdev+8 convention (see core/hand_classifier.h); on Linux they are stable.
constexpr int kKeyHome = 110;
constexpr int kKeyEnd = 115;
constexpr int kKeyPageUp = 112;
constexpr int kKeyPageDown = 117;
constexpr int kKeyInsert = 118;
constexpr int kKeyMenu = 135;
} // namespace
QString SettingsModel::specialLeaderName(int keyCode) const {
    switch (keyCode) {
    case kKeyHome:
        return QStringLiteral("Home");
    case kKeyEnd:
        return QStringLiteral("End");
    case kKeyPageUp:
        return QStringLiteral("Page Up");
    case kKeyPageDown:
        return QStringLiteral("Page Down");
    case kKeyInsert:
        return QStringLiteral("Insert");
    case kKeyMenu:
        return QStringLiteral("Menu");
    default:
        return QString();
    }
}
void SettingsModel::clearCustomKey1() {
    if (customKey1Code_ == kNoKeyCode && customKey1_.isEmpty())
        return; // already clear
    if (!allowLeaderOff(customKey1Enabled_ && customKey1HasKey()))
        return; // would remove the last effective leader
    customKey1Code_ = kNoKeyCode;
    customKey1_.clear();
    Q_EMIT customKey1CodeChanged();
    Q_EMIT customKey1Changed();
    save();
}
void SettingsModel::clearCustomKey2() {
    if (customKey2Code_ == kNoKeyCode && customKey2_.isEmpty())
        return; // already clear
    if (!allowLeaderOff(customKey2Enabled_ && customKey2HasKey()))
        return; // would remove the last effective leader
    customKey2Code_ = kNoKeyCode;
    customKey2_.clear();
    Q_EMIT customKey2CodeChanged();
    Q_EMIT customKey2Changed();
    save();
}

void SettingsModel::setAppFilterMode(const QString &v) {
    if (appFilterMode_ == v)
        return;
    appFilterMode_ = v;
    Q_EMIT appFilterModeChanged();
    save();
}
void SettingsModel::setOverlayEnabled(bool v) {
    if (overlayEnabled_ == v)
        return;
    overlayEnabled_ = v;
    Q_EMIT overlayEnabledChanged();
    save();
}
void SettingsModel::setOverlayShowOnTrigger(bool v) {
    if (overlayShowOnTrigger_ == v)
        return;
    overlayShowOnTrigger_ = v;
    Q_EMIT overlayShowOnTriggerChanged();
    save();
}
void SettingsModel::setOverlayPlacement(const QString &v) {
    // Reject unknown values so the UI can't persist a placement that load()
    // would then ignore (mirrors setTheme's isValidTheme guard).
    if (!isValidPlacement(v) || overlayPlacement_ == v)
        return;
    overlayPlacement_ = v;
    Q_EMIT overlayPlacementChanged();
    save();
}
void SettingsModel::setOverlayProgressBar(bool v) {
    if (overlayProgressBar_ == v)
        return;
    overlayProgressBar_ = v;
    Q_EMIT overlayProgressBarChanged();
    save();
}

void SettingsModel::setOverlayPosition(const QString &v) {
    if (overlayPosition_ == v)
        return;
    overlayPosition_ = v;
    Q_EMIT overlayPositionChanged();
    save();
}

void SettingsModel::setSortByFrequency(bool v) {
    if (sortByFrequency_ == v)
        return;
    sortByFrequency_ = v;
    Q_EMIT sortByFrequencyChanged();
    save();
}
void SettingsModel::setAutoSelect(bool v) {
    if (autoSelect_ == v)
        return;
    autoSelect_ = v;
    Q_EMIT autoSelectChanged();
    save();
}
void SettingsModel::setAutoSelectMs(int v) {
    if (autoSelectMs_ == v)
        return;
    autoSelectMs_ = v;
    Q_EMIT autoSelectMsChanged();
    save();
}
void SettingsModel::setLeaderAutoRepeat(bool v) {
    if (leaderAutoRepeat_ == v)
        return;
    leaderAutoRepeat_ = v;
    Q_EMIT leaderAutoRepeatChanged();
    save();
}

void SettingsModel::setTheme(const QString &v) {
    if (!isValidTheme(v) || theme_ == v)
        return;
    theme_ = v;
    Q_EMIT themeChanged();
    save();
    // Push to the overlay daemon so it switches palette immediately. The
    // client skips the call if the daemon isn't running; a theme change must
    // not spawn it for users who never enabled the overlay.
    overlayClient_.sendTheme(theme_);
}

void SettingsModel::setRounded(bool v) {
    if (rounded_ == v)
        return;
    rounded_ = v;
    Q_EMIT roundedChanged();
    save();
    // Push to a running overlay daemon so its corners follow immediately;
    // the persisted key covers the next daemon start.
    overlayClient_.sendRounded(rounded_);
}

bool SettingsModel::isValidTheme(const QString &name) {
    return schnelle_zeichen::isValidTheme(name.toStdString());
}

bool SettingsModel::isValidPlacement(const QString &name) {
    // Single source for the editor side; must stay in sync with the
    // OverlayPlacement enum in core/engine_config.h.
    static const QStringList kPlacements = {QStringLiteral("Grid"),
                                            QStringLiteral("MouseCursor"),
                                            QStringLiteral("TextCaret")};
    return kPlacements.contains(name);
}

void SettingsModel::addBlacklistEntry(const QString &entry) {
    auto trimmed = entry.trimmed();
    if (trimmed.isEmpty() || blacklist_.contains(trimmed))
        return;
    blacklist_ << trimmed;
    Q_EMIT blacklistChanged();
    save();
}
void SettingsModel::removeBlacklistEntry(int index) {
    if (index < 0 || index >= blacklist_.size())
        return;
    blacklist_.removeAt(index);
    Q_EMIT blacklistChanged();
    save();
}
void SettingsModel::addWhitelistEntry(const QString &entry) {
    auto trimmed = entry.trimmed();
    if (trimmed.isEmpty() || whitelist_.contains(trimmed))
        return;
    whitelist_ << trimmed;
    Q_EMIT whitelistChanged();
    save();
}
void SettingsModel::removeWhitelistEntry(int index) {
    if (index < 0 || index >= whitelist_.size())
        return;
    whitelist_.removeAt(index);
    Q_EMIT whitelistChanged();
    save();
}

bool SettingsModel::isActiveLeaderKey(const QString &key) const {
    if (key.isEmpty())
        return false;
    if (customKey1Enabled_ && customKey1_ == key)
        return true;
    if (customKey2Enabled_ && customKey2_ == key)
        return true;
    return false;
}

bool SettingsModel::isValidLeaderKey(const QString &s) {
    if (s.isEmpty())
        return true; // empty means "not set yet", not an error
    auto ucs4 = s.toUcs4();
    if (ucs4.size() != 1)
        return false;
    return !QChar::isSpace(ucs4[0]);
}

void SettingsModel::load() {
    FILE *fp = std::fopen(settingsFilePath().toUtf8().constData(), "r");
    if (fp == nullptr)
        return;
    const schnelle_zeichen::IniDocument doc = schnelle_zeichen::parseIni(fp);
    std::fclose(fp);
    using schnelle_zeichen::findIniSection;
    using schnelle_zeichen::parseIniBool;
    using schnelle_zeichen::parseIniInt;

    const auto *delay = findIniSection(doc, "Delay");
    delayLowercase_ = parseIniInt(delay, "Lowercase", delayLowercase_);
    delayUppercase_ = parseIniInt(delay, "Uppercase", delayUppercase_);
    delayLowercaseMin_ = parseIniInt(delay, "LowercaseMin", delayLowercaseMin_);
    delayUppercaseMin_ = parseIniInt(delay, "UppercaseMin", delayUppercaseMin_);
    delayUnlimited_ = parseIniBool(delay, "Unlimited", delayUnlimited_);

    const auto *leader = findIniSection(doc, "Leader");
    leaderSpace_ = parseIniBool(leader, "Space", leaderSpace_);
    leaderSpaceReverse_ =
        parseIniBool(leader, "SpaceReverse", leaderSpaceReverse_);
    leaderLeft_ = parseIniBool(leader, "Left", leaderLeft_);
    leaderRight_ = parseIniBool(leader, "Right", leaderRight_);
    leaderUp_ = parseIniBool(leader, "Up", leaderUp_);
    leaderDown_ = parseIniBool(leader, "Down", leaderDown_);
    leaderAlt_ = parseIniBool(leader, "Alt", leaderAlt_);
    leaderAltReverse_ = parseIniBool(leader, "AltReverse", leaderAltReverse_);
    leaderAltGr_ = parseIniBool(leader, "AltGr", leaderAltGr_);
    leaderAltGrReverse_ =
        parseIniBool(leader, "AltGrReverse", leaderAltGrReverse_);
    leaderLeftReverse_ =
        parseIniBool(leader, "LeftReverse", leaderLeftReverse_);
    leaderRightReverse_ =
        parseIniBool(leader, "RightReverse", leaderRightReverse_);
    leaderUpReverse_ = parseIniBool(leader, "UpReverse", leaderUpReverse_);
    leaderDownReverse_ =
        parseIniBool(leader, "DownReverse", leaderDownReverse_);

    const auto *custom = findIniSection(doc, "Leader/Custom");
    customKey1Enabled_ =
        parseIniBool(custom, "CustomKeyEnabled", customKey1Enabled_);
    customKey1_ = iniString(custom, "CustomKey", customKey1_);
    customKey1Code_ = keyCodeValue(custom, "CustomKeyCode");
    customKey1Reverse_ =
        parseIniBool(custom, "CustomKeyReverse", customKey1Reverse_);
    customKey2Enabled_ =
        parseIniBool(custom, "CustomKey2Enabled", customKey2Enabled_);
    customKey2_ = iniString(custom, "CustomKey2", customKey2_);
    customKey2Code_ = keyCodeValue(custom, "CustomKey2Code");
    customKey2Reverse_ =
        parseIniBool(custom, "CustomKey2Reverse", customKey2Reverse_);

    const auto *filter = findIniSection(doc, "AppFilter");
    appFilterMode_ = iniString(filter, "Mode", appFilterMode_);
    // Index-keyed list sections, read through the same helper the engine
    // uses so both sides always agree on gaps and ordering.
    blacklist_.clear();
    for (const auto &v : schnelle_zeichen::detail::indexedList(
             findIniSection(doc, "AppFilter/Blacklist")))
        blacklist_ << QString::fromStdString(v);
    whitelist_.clear();
    for (const auto &v : schnelle_zeichen::detail::indexedList(
             findIniSection(doc, "AppFilter/Whitelist")))
        whitelist_ << QString::fromStdString(v);

    const auto *overlay = findIniSection(doc, "Overlay");
    overlayEnabled_ = parseIniBool(overlay, "Enabled", overlayEnabled_);
    overlayShowOnTrigger_ =
        parseIniBool(overlay, "ShowOnTrigger", overlayShowOnTrigger_);
    // Ignore an unknown value so a corrupt/hand-edited Placement keeps the
    // in-memory default instead of round-tripping garbage that the engine's
    // enum would silently read as Grid anyway.
    const QString placement = iniString(overlay, "Placement");
    if (isValidPlacement(placement))
        overlayPlacement_ = placement;
    overlayProgressBar_ =
        parseIniBool(overlay, "ProgressBar", overlayProgressBar_);
    rounded_ = parseIniBool(overlay, "Rounded", rounded_);
    // Row + Column join into the editor's single position id ("TopCol4");
    // missing halves fall back to their defaults.
    const QString row = iniString(overlay, "Row");
    const QString col = iniString(overlay, "Column");
    if (!row.isEmpty() || !col.isEmpty()) {
        overlayPosition_ = (row.isEmpty() ? QStringLiteral("Top") : row) +
                           (col.isEmpty() ? QStringLiteral("Col4") : col);
    }

    const auto *behavior = findIniSection(doc, "Behavior");
    sortByFrequency_ =
        parseIniBool(behavior, "SortByFrequency", sortByFrequency_);
    autoSelect_ = parseIniBool(behavior, "AutoSelect", autoSelect_);
    autoSelectMs_ = parseIniInt(behavior, "AutoSelectMs", autoSelectMs_);
    leaderAutoRepeat_ =
        parseIniBool(behavior, "LeaderAutoRepeat", leaderAutoRepeat_);

    const auto *theme = findIniSection(doc, "Theme");
    const QString themeName = iniString(theme, "Theme");
    if (isValidTheme(themeName))
        theme_ = themeName;

    Q_EMIT delayLowercaseChanged();
    Q_EMIT delayUppercaseChanged();
    Q_EMIT delayLowercaseMinChanged();
    Q_EMIT delayUppercaseMinChanged();
    Q_EMIT delayUnlimitedChanged();
    Q_EMIT leaderSpaceChanged();
    Q_EMIT leaderSpaceReverseChanged();
    Q_EMIT leaderLeftChanged();
    Q_EMIT leaderRightChanged();
    Q_EMIT leaderUpChanged();
    Q_EMIT leaderDownChanged();
    Q_EMIT leaderAltChanged();
    Q_EMIT leaderAltReverseChanged();
    Q_EMIT leaderAltGrChanged();
    Q_EMIT leaderAltGrReverseChanged();
    Q_EMIT leaderLeftReverseChanged();
    Q_EMIT leaderRightReverseChanged();
    Q_EMIT leaderUpReverseChanged();
    Q_EMIT leaderDownReverseChanged();
    Q_EMIT customKey1EnabledChanged();
    Q_EMIT customKey1Changed();
    Q_EMIT customKey1CodeChanged();
    Q_EMIT customKey1ReverseChanged();
    Q_EMIT customKey2EnabledChanged();
    Q_EMIT customKey2Changed();
    Q_EMIT customKey2CodeChanged();
    Q_EMIT customKey2ReverseChanged();
    Q_EMIT appFilterModeChanged();
    Q_EMIT blacklistChanged();
    Q_EMIT whitelistChanged();
    Q_EMIT overlayEnabledChanged();
    Q_EMIT overlayShowOnTriggerChanged();
    Q_EMIT overlayPlacementChanged();
    Q_EMIT overlayProgressBarChanged();
    Q_EMIT overlayPositionChanged();
    Q_EMIT themeChanged();
    Q_EMIT roundedChanged();
    Q_EMIT sortByFrequencyChanged();
    Q_EMIT autoSelectChanged();
    Q_EMIT autoSelectMsChanged();
    Q_EMIT leaderAutoRepeatChanged();
}

void SettingsModel::save() {
    const QString path = settingsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QTextStream out(&f);

    out << "[Delay]\n";
    out << "# Lowercase (ms)\n";
    out << "Lowercase=" << delayLowercase_ << "\n";
    out << "# Uppercase (ms)\n";
    out << "Uppercase=" << delayUppercase_ << "\n";
    out << "# Lowercase minimum hold (ms)\n";
    out << "LowercaseMin=" << delayLowercaseMin_ << "\n";
    out << "# Uppercase minimum hold (ms)\n";
    out << "UppercaseMin=" << delayUppercaseMin_ << "\n";
    out << "# Keep the window open while the key is held (no upper bound)\n";
    out << "Unlimited=" << toBool(delayUnlimited_) << "\n";
    out << "\n";
    out << "[Leader]\n";
    out << "# Space (+ reverse direction)\n"
        << "Space=" << toBool(leaderSpace_) << "\n"
        << "SpaceReverse=" << toBool(leaderSpaceReverse_) << "\n";
    out << "# Left Arrow (+ reverse direction)\n"
        << "Left=" << toBool(leaderLeft_) << "\n"
        << "LeftReverse=" << toBool(leaderLeftReverse_) << "\n";
    out << "# Right Arrow (+ reverse direction)\n"
        << "Right=" << toBool(leaderRight_) << "\n"
        << "RightReverse=" << toBool(leaderRightReverse_) << "\n";
    out << "# Up Arrow (+ reverse direction)\n"
        << "Up=" << toBool(leaderUp_) << "\n"
        << "UpReverse=" << toBool(leaderUpReverse_) << "\n";
    out << "# Down Arrow (+ reverse direction)\n"
        << "Down=" << toBool(leaderDown_) << "\n"
        << "DownReverse=" << toBool(leaderDownReverse_) << "\n";
    out << "# Alt (+ reverse direction)\n"
        << "Alt=" << toBool(leaderAlt_) << "\n"
        << "AltReverse=" << toBool(leaderAltReverse_) << "\n";
    out << "# AltGr (+ reverse direction)\n"
        << "AltGr=" << toBool(leaderAltGr_) << "\n"
        << "AltGrReverse=" << toBool(leaderAltGrReverse_) << "\n";
    out << "\n";
    out << "[Leader/Custom]\n";
    out << "# Custom Leader 1\n"
        << "CustomKeyEnabled=" << toBool(customKey1Enabled_) << "\n";
    out << "#   \xe2\x86\xb3 Key\n"
        << "CustomKey=" << escaped(customKey1_) << "\n";
    out << "#   \xe2\x86\xb3 Key code\n"
        << "CustomKeyCode=" << customKey1Code_ << "\n";
    out << "#   \xe2\x86\xb3 reverse direction\n"
        << "CustomKeyReverse=" << toBool(customKey1Reverse_) << "\n";
    out << "# Custom Leader 2 (hand-split)\n"
        << "CustomKey2Enabled=" << toBool(customKey2Enabled_) << "\n";
    out << "#   \xe2\x86\xb3 Key\n"
        << "CustomKey2=" << escaped(customKey2_) << "\n";
    out << "#   \xe2\x86\xb3 Key code\n"
        << "CustomKey2Code=" << customKey2Code_ << "\n";
    out << "#   \xe2\x86\xb3 reverse direction\n"
        << "CustomKey2Reverse=" << toBool(customKey2Reverse_) << "\n";
    out << "\n";
    out << "[AppFilter]\n";
    out << "# Mode\n" << "Mode=" << appFilterMode_ << "\n";
    if (!blacklist_.isEmpty()) {
        out << "\n[AppFilter/Blacklist]\n";
        for (int i = 0; i < blacklist_.size(); ++i) {
            out << i << "=" << escaped(blacklist_[i]) << "\n";
        }
    }
    if (!whitelist_.isEmpty()) {
        out << "\n[AppFilter/Whitelist]\n";
        for (int i = 0; i < whitelist_.size(); ++i) {
            out << i << "=" << escaped(whitelist_[i]) << "\n";
        }
    }
    out << "\n[Overlay]\n";
    out << "# Show overlay while cycling\n"
        << "Enabled=" << toBool(overlayEnabled_) << "\n";
    out << "# Preview in the trigger window (all mapped keys)\n"
        << "ShowOnTrigger=" << toBool(overlayShowOnTrigger_) << "\n";
    out << "# Placement (Grid|MouseCursor|TextCaret)\n"
        << "Placement=" << overlayPlacement_ << "\n";
    out << "# Show timing progress bar\n"
        << "ProgressBar=" << toBool(overlayProgressBar_) << "\n";
    out << "# Rounded corners (off = flat design)\n"
        << "Rounded=" << toBool(rounded_) << "\n";
    // Split "TopCol4" into Row=Top + Column=Col4, the two keys the engine's
    // config reader parses.
    const int splitAt =
        static_cast<int>(overlayPosition_.indexOf(QLatin1String("Col")));
    const QString row =
        splitAt > 0 ? overlayPosition_.left(splitAt) : QStringLiteral("Top");
    const QString col =
        splitAt > 0 ? overlayPosition_.mid(splitAt) : QStringLiteral("Col4");
    out << "# Vertical position\n" << "Row=" << row << "\n";
    out << "# Horizontal position\n" << "Column=" << col << "\n";
    out << "\n[Behavior]\n";
    out << "# Sort each key's variants by how often you use them\n"
        << "SortByFrequency=" << toBool(sortByFrequency_) << "\n";
    out << "# Hold a mapped key to pre-select the first variant (opt-in)\n"
        << "AutoSelect=" << toBool(autoSelect_) << "\n";
    out << "#   \xe2\x86\xb3 hold time (ms)\n"
        << "AutoSelectMs=" << autoSelectMs_ << "\n";
    out << "# A held leader keeps stepping via auto-repeat (opt-in)\n"
        << "LeaderAutoRepeat=" << toBool(leaderAutoRepeat_) << "\n";
    out << "\n[Theme]\n";
    out << "# UI theme\n"
        << "Theme=" << theme_ << "\n";
    out.flush();
    f.commit();
    // No engine-reload call: the engine watches the config dir and reloads
    // itself on the atomic replace.
}
