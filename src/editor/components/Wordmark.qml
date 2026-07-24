// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import SchnelleZeichen

// The two-colour "Schnelle Zeichen" wordmark, shared by the header and the
// About dialog so the name (its text, colours and word gap) is defined once.
RowLayout {
    spacing: 6 // gap between the two words of the wordmark
    Text {
        text: "Schnelle"
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontStrong
        font.weight: Font.Medium
    }
    Text {
        text: "Zeichen"
        color: Theme.brand
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontStrong
        font.weight: Font.Medium
    }
}
