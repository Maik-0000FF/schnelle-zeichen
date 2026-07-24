// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SchnelleZeichen

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

    Slider {
        id: slider
        Layout.fillWidth: true
        from: root.minValue
        to: root.maxValue
        stepSize: root.stepSize
        value: root.value
        snapMode: Slider.SnapAlways

        // Pin geometry to the handle so the control height is independent of
        // the active Quick Controls style / Qt version (whose default Slider
        // padding differs and otherwise shifts the whole row).
        padding: 0
        implicitHeight: 16

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
            width: 16
            height: 16
            // Square in the flat look, round in the rounded look.
            radius: Theme.rounded ? 8 : 0
            color: slider.pressed ? root.fillColorActive : root.fillColor
            // Keyboard focus shows as an accent handle border (the slider also
            // takes focus on click, where the drag colour already signals it).
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

    Text {
        text: slider.value + " " + root.suffix
        color: Theme.textMuted
        font.family: Theme.fontFamilyMono
        font.pixelSize: Theme.fontBody
        Layout.preferredWidth: 70
        horizontalAlignment: Text.AlignRight
    }
}
