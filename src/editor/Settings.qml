// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SchnelleZeichen

Item {
    id: root

    property var settingsModel: null
    property var mappingsModel: null

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ScrollBar.horizontal.policy: ScrollBar.AsNeeded

        readonly property int minContentWidth: 484
        contentWidth: Math.max(root.width, scroll.minContentWidth)

        ColumnLayout {
            width: scroll.contentWidth
            spacing: Theme.spacingMd

            Item { implicitHeight: Theme.spacingLg }

            ColumnLayout {
                Layout.leftMargin: Theme.spacingLg
                Layout.rightMargin: Theme.spacingLg
                Layout.fillWidth: true
                spacing: Theme.spacingMd

                SettingsCard {
                    titleText: qsTr("Theme")

                    ThemeSelector {
                        Layout.fillWidth: true
                        settingsModel: root.settingsModel
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingXs
                        text: qsTr("Applies to the editor and the cycle overlay. \"Contrast\" meets WCAG AAA (7:1).")
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }

                    LabeledSwitch {
                        labelText: qsTr("Rounded corners")
                        tooltipText: qsTr("Round the corners of the editor and overlay. Off = the flat, sharp-cornered default.")
                        checked: root.settingsModel ? root.settingsModel.rounded : false
                        onToggled: (v) => root.settingsModel.rounded = v
                    }
                }

                SettingsCard {
                    titleText: qsTr("Delay")

                    LabeledRangeSlider {
                        labelText: qsTr("Lowercase")
                        from: 0
                        to: 2000
                        step: 10
                        // Mirrors kDelayMin: the engine floors the window's max at 50ms.
                        upperMin: 50
                        lowerValue: root.settingsModel ? root.settingsModel.delayLowercaseMin : 0
                        upperValue: root.settingsModel ? root.settingsModel.delayLowercase : 400
                        onLowerEdited: (v) => root.settingsModel.delayLowercaseMin = v
                        onUpperEdited: (v) => root.settingsModel.delayLowercase = v
                    }
                    LabeledRangeSlider {
                        labelText: qsTr("Uppercase")
                        from: 0
                        to: 2000
                        step: 10
                        // Mirrors kDelayMin: the engine floors the window's max at 50ms.
                        upperMin: 50
                        lowerValue: root.settingsModel ? root.settingsModel.delayUppercaseMin : 0
                        upperValue: root.settingsModel ? root.settingsModel.delayUppercase : 700
                        onLowerEdited: (v) => root.settingsModel.delayUppercaseMin = v
                        onUpperEdited: (v) => root.settingsModel.delayUppercase = v
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingXs
                        text: qsTr("The accent fires only while the mapped key is held and the leader (e.g. Space) arrives inside the window. Raise the minimum to avoid accidental accents when typing fast; lower it to 0 for the classic timeout-only behavior.")
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }
                }

                SettingsCard {
                    id: leaderCard
                    titleText: qsTr("Leader Keys")

                    // Reactive note for the "at least one leader" guard: the
                    // model refuses to turn off the last effective leader and
                    // the switch snaps back, so surface why. Cleared once a
                    // leader is added again (effective count rises above one).
                    property bool guardHint: false
                    Connections {
                        target: root.settingsModel
                        function onLeaderRemovalBlocked() { leaderCard.guardHint = true; }
                        function onLeadersChanged() {
                            if (root.settingsModel && root.settingsModel.effectiveLeaderCount > 1)
                                leaderCard.guardHint = false;
                        }
                    }

                    DirectionalLeaderRow {
                        labelText: qsTr("Space")
                        tooltipText: qsTr("Use Space to trigger and cycle accents.")
                        enabledValue: root.settingsModel ? root.settingsModel.leaderSpace : false
                        reverseValue: root.settingsModel ? root.settingsModel.leaderSpaceReverse : false
                        onEnabledToggled: (v) => root.settingsModel.leaderSpace = v
                        onReverseToggled: (v) => root.settingsModel.leaderSpaceReverse = v
                    }
                    // Arrows carry a direction: the toggle left of the enable
                    // switch flips the cycle direction, and the arrow marker
                    // (→ forward, ← reverse) shows it. Any arrow can go either
                    // way, independently.
                    DirectionalLeaderRow {
                        labelText: qsTr("Left Arrow")
                        tooltipText: qsTr("Use the Left arrow to trigger and cycle accents.")
                        enabledValue: root.settingsModel ? root.settingsModel.leaderLeft : false
                        reverseValue: root.settingsModel ? root.settingsModel.leaderLeftReverse : false
                        onEnabledToggled: (v) => root.settingsModel.leaderLeft = v
                        onReverseToggled: (v) => root.settingsModel.leaderLeftReverse = v
                    }
                    DirectionalLeaderRow {
                        labelText: qsTr("Right Arrow")
                        tooltipText: qsTr("Use the Right arrow to trigger and cycle accents.")
                        enabledValue: root.settingsModel ? root.settingsModel.leaderRight : false
                        reverseValue: root.settingsModel ? root.settingsModel.leaderRightReverse : false
                        onEnabledToggled: (v) => root.settingsModel.leaderRight = v
                        onReverseToggled: (v) => root.settingsModel.leaderRightReverse = v
                    }
                    DirectionalLeaderRow {
                        labelText: qsTr("Up Arrow")
                        tooltipText: qsTr("Use the Up arrow to trigger and cycle accents.")
                        enabledValue: root.settingsModel ? root.settingsModel.leaderUp : false
                        reverseValue: root.settingsModel ? root.settingsModel.leaderUpReverse : false
                        onEnabledToggled: (v) => root.settingsModel.leaderUp = v
                        onReverseToggled: (v) => root.settingsModel.leaderUpReverse = v
                    }
                    DirectionalLeaderRow {
                        labelText: qsTr("Down Arrow")
                        tooltipText: qsTr("Use the Down arrow to trigger and cycle accents.")
                        enabledValue: root.settingsModel ? root.settingsModel.leaderDown : false
                        reverseValue: root.settingsModel ? root.settingsModel.leaderDownReverse : false
                        onEnabledToggled: (v) => root.settingsModel.leaderDown = v
                        onReverseToggled: (v) => root.settingsModel.leaderDownReverse = v
                    }
                    DirectionalLeaderRow {
                        labelText: qsTr("Alt")
                        tooltipText: qsTr("Use the left Alt key to trigger and cycle accents.")
                        enabledValue: root.settingsModel ? root.settingsModel.leaderAlt : false
                        reverseValue: root.settingsModel ? root.settingsModel.leaderAltReverse : false
                        onEnabledToggled: (v) => root.settingsModel.leaderAlt = v
                        onReverseToggled: (v) => root.settingsModel.leaderAltReverse = v
                    }
                    DirectionalLeaderRow {
                        labelText: qsTr("AltGr")
                        tooltipText: qsTr("Use AltGr (the right Alt) to trigger and cycle accents.")
                        enabledValue: root.settingsModel ? root.settingsModel.leaderAltGr : false
                        reverseValue: root.settingsModel ? root.settingsModel.leaderAltGrReverse : false
                        onEnabledToggled: (v) => root.settingsModel.leaderAltGr = v
                        onReverseToggled: (v) => root.settingsModel.leaderAltGrReverse = v
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.border
                    }

                    CustomLeaderRow {
                        labelText: qsTr("Custom Leader 1")
                        tooltipText: qsTr("Use a custom physical key to trigger and cycle accents.")
                        enabledValue: root.settingsModel ? root.settingsModel.customKey1Enabled : false
                        reverseValue: root.settingsModel ? root.settingsModel.customKey1Reverse : false
                        keyValue: root.settingsModel ? root.settingsModel.customKey1 : ""
                        keyValueCode: root.settingsModel ? root.settingsModel.customKey1Code : -1
                        keyAssigned: root.settingsModel ? root.settingsModel.customKey1HasKey : false
                        mappingsModel: root.mappingsModel
                        settingsModel: root.settingsModel
                        onEnabledEdited: (v) => root.settingsModel.customKey1Enabled = v
                        onReverseEdited: (v) => root.settingsModel.customKey1Reverse = v
                        onKeyCaptured: (ch, code) => root.settingsModel.captureCustomKey1(ch, code)
                        onKeyCleared: () => root.settingsModel.clearCustomKey1()
                    }

                    CustomLeaderRow {
                        labelText: qsTr("Custom Leader 2 (hand-split)")
                        tooltipText: qsTr("Use a second custom key on the opposite hand to trigger and cycle accents.")
                        enabledValue: root.settingsModel ? root.settingsModel.customKey2Enabled : false
                        reverseValue: root.settingsModel ? root.settingsModel.customKey2Reverse : false
                        keyValue: root.settingsModel ? root.settingsModel.customKey2 : ""
                        keyValueCode: root.settingsModel ? root.settingsModel.customKey2Code : -1
                        keyAssigned: root.settingsModel ? root.settingsModel.customKey2HasKey : false
                        mappingsModel: root.mappingsModel
                        settingsModel: root.settingsModel
                        onEnabledEdited: (v) => root.settingsModel.customKey2Enabled = v
                        onReverseEdited: (v) => root.settingsModel.customKey2Reverse = v
                        onKeyCaptured: (ch, code) => root.settingsModel.captureCustomKey2(ch, code)
                        onKeyCleared: () => root.settingsModel.clearCustomKey2()
                    }

                    Text {
                        visible: leaderCard.guardHint
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingXs
                        text: qsTr("At least one leader must stay active, so this one can't be turned off.")
                        color: Theme.warning
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }
                }

                SettingsCard {
                    titleText: qsTr("Overlay")

                    LabeledSwitch {
                        // Governs every overlay feature (cycling picker, trigger
                        // preview, progress bar, caret window), so the label is
                        // the plain master switch, not "while cycling" which
                        // undersold its scope.
                        labelText: qsTr("Show overlay")
                        tooltipText: qsTr("Show the on-screen overlay during the accent gesture.")
                        enabled: root.settingsModel
                        checked: root.settingsModel ? root.settingsModel.overlayEnabled : false
                        onToggled: (v) => root.settingsModel.overlayEnabled = v
                    }

                    // Placement is the structural choice, so it comes first,
                    // right under the master switch. The sub-options below adapt
                    // to whichever placement is selected.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMd
                        visible: root.settingsModel
                            && root.settingsModel.overlayEnabled

                        Text {
                            text: qsTr("Placement")
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            Layout.preferredWidth: 120
                        }

                        ThemedComboBox {
                            id: placementBox
                            Layout.fillWidth: true
                            // "Fixed position" and "At mouse cursor" drive the
                            // overlay daemon, which needs wlr-layer-shell.
                            // Where that is missing, those two are flagged
                            // "(needs layer-shell)" and dimmed inline so the
                            // constraint shows at the point of choice, not
                            // only in the note below. They stay selectable as
                            // an escape hatch if the capability check is wrong.
                            // No `enabled` gate needed: the parent row's
                            // `visible` already carries the same condition, so
                            // the combo only ever renders when it is usable.
                            textRole: "label"
                            valueRole: "key"
                            readonly property bool noLayerShell:
                                root.settingsModel
                                && !root.settingsModel.layerShellAvailable
                            model: [
                                { key: "Grid",
                                  label: placementBox.noLayerShell
                                      ? qsTr("Fixed position (needs layer-shell)")
                                      : qsTr("Fixed position"),
                                  unavailable: placementBox.noLayerShell },
                                { key: "MouseCursor",
                                  label: placementBox.noLayerShell
                                      ? qsTr("At mouse cursor (needs layer-shell)")
                                      : qsTr("At mouse cursor"),
                                  unavailable: placementBox.noLayerShell },
                                { key: "TextCaret",
                                  // Deferred: the caret-position source (the
                                  // accessibility caret chain) is not wired up
                                  // yet; the engine falls back to the grid
                                  // position meanwhile.
                                  label: qsTr("At text cursor (planned)"),
                                  unavailable: true }
                            ]
                            currentIndex: {
                                if (!root.settingsModel) return 0;
                                for (var i = 0; i < model.length; ++i) {
                                    if (model[i].key === root.settingsModel.overlayPlacement) return i;
                                }
                                return 0;
                            }
                            onActivated: {
                                if (!root.settingsModel)
                                    return;
                                root.settingsModel.overlayPlacement = model[currentIndex].key;
                            }
                        }
                    }

                    Text {
                        // Sits right under the placement combo it explains:
                        // Grid/MouseCursor need wlr-layer-shell for the
                        // overlay daemon's layer surface.
                        visible: root.settingsModel
                            && root.settingsModel.overlayEnabled
                            && !root.settingsModel.layerShellAvailable
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        text: root.settingsModel
                            ? qsTr("\"Fixed position\" and \"At mouse cursor\" need wlr-layer-shell, unavailable on %1.\nLayer-shell is supported on KDE Plasma Wayland, sway, Hyprland, river, wayfire, Mango.")
                              .arg(root.settingsModel.layerShellSession)
                            : ""
                    }

                    LabeledSwitch {
                        labelText: qsTr("Show timing progress bar")
                        tooltipText: qsTr("Show a bar counting down the accent gesture timing.")
                        // Daemon-only visual (needs layer-shell, no effect in
                        // caret placement). Hide it there like the position
                        // picker instead of leaving a dead disabled switch.
                        visible: root.settingsModel
                            && root.settingsModel.layerShellAvailable
                            && root.settingsModel.overlayEnabled
                            && root.settingsModel.overlayPlacement !== "TextCaret"
                        checked: root.settingsModel ? root.settingsModel.overlayProgressBar : false
                        onToggled: (v) => root.settingsModel.overlayProgressBar = v
                    }

                    PositionPicker {
                        // The grid only matters for Grid/MouseCursor placement;
                        // in TextCaret mode the caret decides the position, so
                        // hide the picker entirely.
                        visible: root.settingsModel
                            && root.settingsModel.layerShellAvailable
                            && root.settingsModel.overlayEnabled
                            && root.settingsModel.overlayPlacement !== "TextCaret"
                        value: root.settingsModel ? root.settingsModel.overlayPosition : "TopCenter"
                        // In mouse-cursor mode the grid is only the fallback: it
                        // stays marked but dimmed, and a pointer marker shows the
                        // menu follows the mouse.
                        atCursorMode: root.settingsModel
                            ? root.settingsModel.overlayPlacement === "MouseCursor"
                            : false
                        onEdited: (v) => root.settingsModel.overlayPosition = v
                    }

                    LabeledSwitch {
                        labelText: qsTr("Preview in the trigger window")
                        tooltipText: qsTr("Show the accent preview the moment the gesture fires.")
                        // Applies to every placement (the caret path shows the
                        // same preview), so it only depends on the overlay
                        // being enabled, not on layer-shell.
                        enabled: root.settingsModel
                            && root.settingsModel.overlayEnabled
                        checked: root.settingsModel ? root.settingsModel.overlayShowOnTrigger : false
                        onToggled: (v) => root.settingsModel.overlayShowOnTrigger = v
                    }

                    Text {
                        // One combined note: what the preview does plus the
                        // single-accent caveat (those keys never cycle, so the
                        // preview is the only way they ever get an overlay).
                        // Merged from two stacked paragraphs to thin the
                        // muted-text wall.
                        visible: root.settingsModel
                            && root.settingsModel.overlayEnabled
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        text: qsTr("Shows the available accents as soon as you hold a mapped key, before pressing a leader. It is also the only way single-accent keys, which never cycle, get an overlay.")
                    }
                }

                // Opt-in extensions beyond the classic behavior. Each is
                // separately switchable and, while off, changes nothing about
                // the legacy gesture semantics.
                SettingsCard {
                    titleText: qsTr("Extensions (opt-in)")

                    LabeledSwitch {
                        labelText: qsTr("Keep the window open while the key is held")
                        tooltipText: qsTr("No upper time bound: the accent window stays open as long as the key is held (popup feel). Off = the classic timed window.")
                        checked: root.settingsModel ? root.settingsModel.delayUnlimited : false
                        onToggled: (v) => root.settingsModel.delayUnlimited = v
                    }

                    LabeledSwitch {
                        labelText: qsTr("Long press pre-selects the first variant")
                        tooltipText: qsTr("Holding a mapped key past the hold time pre-selects its first variant without a leader; releasing commits it.")
                        checked: root.settingsModel ? root.settingsModel.autoSelect : false
                        onToggled: (v) => root.settingsModel.autoSelect = v
                    }

                    LabeledSlider {
                        visible: root.settingsModel && root.settingsModel.autoSelect
                        labelText: qsTr("Hold time")
                        minValue: 50
                        maxValue: 2000
                        stepSize: 10
                        value: root.settingsModel ? root.settingsModel.autoSelectMs : 500
                        onValueEdited: (v) => root.settingsModel.autoSelectMs = v
                    }

                    LabeledSwitch {
                        labelText: qsTr("Held leader keeps cycling (auto-repeat)")
                        tooltipText: qsTr("A held leader steps through the variants via key auto-repeat, the legacy behavior. Off = each deliberate press steps once.")
                        checked: root.settingsModel ? root.settingsModel.leaderAutoRepeat : false
                        onToggled: (v) => root.settingsModel.leaderAutoRepeat = v
                    }
                }

                SettingsCard {
                    titleText: qsTr("App Filter")

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMd

                        Text {
                            text: qsTr("Mode")
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            Layout.preferredWidth: 120
                        }

                        ThemedComboBox {
                            id: modeBox
                            Layout.fillWidth: true
                            model: ["Disabled", "Blacklist", "Whitelist"]
                            currentIndex: root.settingsModel
                                ? model.indexOf(root.settingsModel.appFilterMode)
                                : 0
                            onActivated: {
                                if (root.settingsModel) {
                                    root.settingsModel.appFilterMode = model[currentIndex];
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.border
                        visible: modeBox.currentIndex !== 0
                    }

                    AppListEditor {
                        visible: modeBox.currentIndex === 1
                        labelText: qsTr("Blacklist")
                        items: root.settingsModel ? root.settingsModel.blacklist : []
                        onAddRequested: (e) => root.settingsModel.addBlacklistEntry(e)
                        onRemoveRequested: (i) => root.settingsModel.removeBlacklistEntry(i)
                    }

                    AppListEditor {
                        visible: modeBox.currentIndex === 2
                        labelText: qsTr("Whitelist")
                        items: root.settingsModel ? root.settingsModel.whitelist : []
                        onAddRequested: (e) => root.settingsModel.addWhitelistEntry(e)
                        onRemoveRequested: (i) => root.settingsModel.removeWhitelistEntry(i)
                    }

                    Text {
                        visible: modeBox.currentIndex !== 0
                        Layout.fillWidth: true
                        text: qsTr("Case-sensitive substring match against the focused application's identifier (app id / window class). Some apps report their GUI library instead of their name (e.g. Kitty → GLFW_Application).")
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }
                }

                Item { implicitHeight: Theme.spacingLg }
            }
        }
    }

    // Click anywhere empty to drop keyboard focus, disarming an armed
    // custom-leader capture field. Topmost so it sees every press first, but
    // passes them through so the ScrollView and controls keep working.
    FocusSink {}
}
