// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import SchnelleZeichen

ColumnLayout {
    spacing: Theme.spacingMd

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        width: 64
        height: 64
        radius: 32
        color: Theme.accentSoft
        Text {
            anchors.centerIn: parent
            text: Theme.iconEdit
            color: Theme.accent
            font.pixelSize: Theme.fontHero
        }
    }

    Text {
        Layout.alignment: Qt.AlignHCenter
        text: qsTr("No mappings yet")
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontStrong
        font.weight: Font.Medium
    }

    Text {
        Layout.alignment: Qt.AlignHCenter
        text: qsTr("Add your first mapping above")
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }
}
