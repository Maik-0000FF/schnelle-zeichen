// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SchnelleZeichen

// Single-value slider with the same readout layout as LabeledRangeSlider:
// the set value floats above the filled segment in the role colour, and the
// neutral end-of-range value sits at the right, so every slider in the app
// reads identically.
RowLayout {
    id: root
    Layout.fillWidth: true
    spacing: Theme.spacingMd

    property string labelText: ""
    property int minValue: 50
    property int maxValue: 2000
    property int stepSize: 25
    property int value: 400
    property string suffix: "ms"
    // Role colours for the fill and handle; the hold-time slider passes the
    // hold role so it matches the overlay bar's marker.
    property color fillColor: Theme.accent
    property color fillColorActive: Theme.accentHover
    signal valueEdited(int v)

    Text {
        text: root.labelText
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        Layout.preferredWidth: 120
    }

    Item {
        Layout.fillWidth: true
        // Top band carries the value label, the lower band the track, the
        // same split as LabeledRangeSlider.
        implicitHeight: 36
        readonly property int rowY: 16

        // The set value, centred above the filled segment in the role
        // colour, clamped to the track so it never clips at the edges.
        Text {
            readonly property real mid:
                slider.leftPadding + slider.visualPosition
                * slider.availableWidth / 2
            x: Math.max(0, Math.min(parent.width - width, mid - width / 2))
            y: 0
            text: slider.value + " " + root.suffix
            color: root.fillColor
            font.family: Theme.fontFamilyMono
            font.pixelSize: Theme.fontBody
        }

        Slider {
            id: slider
            anchors.left: parent.left
            anchors.right: parent.right
            y: parent.rowY
            from: root.minValue
            to: root.maxValue
            stepSize: root.stepSize
            snapMode: Slider.SnapAlways

            // A Binding object instead of a plain `value:` binding: the
            // user's first drag writes slider.value internally, which would
            // silently break the plain binding, and a later external model
            // change (reset, reload) would no longer move the slider. The
            // Binding object survives those writes and keeps following
            // root.value.
            Binding {
                target: slider
                property: "value"
                value: root.value
            }

            // Pin geometry to the handle so the control height is independent
            // of the active Quick Controls style / Qt version (whose default
            // Slider padding differs and otherwise shifts the whole row).
            padding: 0
            implicitHeight: Theme.sliderHandleSize

            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: 4
                radius: 2
                color: Theme.border

                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    color: root.fillColor
                    radius: 2
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition *
                   (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: Theme.sliderHandleSize
                height: Theme.sliderHandleSize
                // Square in the flat look, round in the rounded look.
                radius: Theme.rounded ? width / 2 : 0
                color: slider.pressed ? root.fillColorActive : root.fillColor
                // Keyboard focus shows as an accent handle border (the slider
                // also takes focus on click, where the drag colour already
                // signals it).
                border.color: (slider.activeFocus
                               && slider.focusReason !== Qt.MouseFocusReason)
                              ? Theme.accent : Theme.background
                border.width: 2
                Behavior on border.color { ColorAnimation { duration: Theme.animShort } }
            }

            onValueChanged: {
                if (value !== root.value) root.valueEdited(value);
            }
        }
    }

    // End of range, neutral like the range sliders' right readout.
    Text {
        text: root.maxValue + " " + root.suffix
        color: Theme.textMuted
        font.family: Theme.fontFamilyMono
        font.pixelSize: Theme.fontBody
        Layout.preferredWidth: 60
        horizontalAlignment: Text.AlignRight
    }
}
