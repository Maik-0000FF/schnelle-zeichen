// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

pragma Singleton
import QtQuick

// Single source of truth for cross-module font resolution, shared by the editor
// (Theme.qml) and the overlay (Overlay.qml). Both live in their own QML module
// and process; before this each kept an identical copy of the monospace
// candidate list AND the resolver, which had to be edited in two places. The
// sans-serif candidate list stays in Theme.qml (editor-only, not duplicated).
QtObject {
    // Preference order for the monospace family: JetBrains Mono first (the
    // metrics the overlay cell sizing and the editor's mono cells are tuned to),
    // as it does not ship by default, then the common system monos, and finally
    // the generic alias fontconfig always resolves.
    readonly property var monoCandidates: ["JetBrains Mono", "Noto Sans Mono", "DejaVu Sans Mono", "Liberation Mono", "monospace"]

    // Resolve to the first installed family from `candidates`, or the last entry
    // (the always-resolvable generic alias) if none is installed. Neither the
    // overlay's fixed cells nor the editor's layout can be broken by a wider
    // fallback: the overlay cell Text uses Text.HorizontalFit. font.family takes
    // a single string (font.families plural is not assignable in Qt 6.4, which
    // the editor still targets), so callers resolve to one name here.
    function pickFamily(candidates) {
        const avail = Qt.fontFamilies()
        for (let i = 0; i < candidates.length; i++)
            if (avail.indexOf(candidates[i]) >= 0)
                return candidates[i]
        return candidates[candidates.length - 1]
    }
}
