// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import SchnelleZeichen

// Crisp infinity symbol drawn as two tangent circle outlines, used where the
// delay window has no upper bound (unlimited mode). A Canvas like the
// DropdownIndicator chevron, so the size and colour are free of font-glyph
// metrics (the mono face renders U+221E small and thin).
Canvas {
    id: icon

    property color color: Theme.textMuted
    // Icon height; the two tangent loops make the width derive from it.
    property int size: 12
    readonly property real strokeWidth: 2

    readonly property real loopRadius: (size - 2 * strokeWidth) / 2
    width: 4 * loopRadius + 2 * strokeWidth
    height: size
    contextType: "2d"

    onColorChanged: requestPaint()
    onSizeChanged: requestPaint()
    // Colour tokens re-resolve on a palette switch, but Canvas only repaints
    // on request; nudge it like the dropdown chevron does.
    Connections {
        target: Theme
        function onCurrentChanged() { icon.requestPaint() }
    }

    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        const r = icon.loopRadius;
        const cy = icon.height / 2;
        ctx.lineWidth = icon.strokeWidth;
        ctx.strokeStyle = icon.color;
        ctx.beginPath();
        ctx.arc(icon.strokeWidth + r, cy, r, 0, 2 * Math.PI);
        ctx.stroke();
        ctx.beginPath();
        ctx.arc(icon.strokeWidth + 3 * r, cy, r, 0, 2 * Math.PI);
        ctx.stroke();
    }
}
