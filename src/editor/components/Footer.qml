// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import SchnelleZeichen

Rectangle {
    color: Theme.background
    implicitHeight: 36

    // Translated display text plus the machine-readable state: the colour
    // derives from the enum, never from the (translator-dependent) text.
    property string saveStatus: ""
    property int saveState: MappingListModel.SaveNone

    readonly property color dotColor:
        saveState === MappingListModel.SaveSaved ? Theme.success :
        saveState === MappingListModel.SaveError ? Theme.error :
        Theme.textMuted

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        spacing: Theme.spacingSm

        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: dotColor
            Behavior on color { ColorAnimation { duration: Theme.animShort } }
        }

        Text {
            text: saveStatus
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }

        Item { Layout.fillWidth: true }

        Text {
            text: qsTr("Changes are saved automatically")
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.italic: true
        }
    }
}
