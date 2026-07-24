// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_EDITOR_KEYCOMBO_H
#define SCHNELLE_ZEICHEN_EDITOR_KEYCOMBO_H

#include <QString>

// Convert a captured Qt key combo (a Qt::Key value plus Qt::KeyboardModifiers)
// into the portable combo string the engine matches, e.g. "Control+Alt+J" or
// "Control+Alt+period".
//
// Returns an empty string when the combo is unusable as a shortcut: no real
// (non-Shift) modifier is held, or the base key is one we don't map (the
// engine also rejects modifier-less combos, see parseShortcutCombo). Letters
// are emitted uppercase; the engine resolves the key name through
// xkb_keysym_from_name case-insensitively and compares via
// xkb_keysym_to_lower, so a Ctrl+Alt+j press matches a "Control+Alt+J"
// binding.
//
// Kept free of QML/QObject so it can be unit-tested directly against the
// engine's combo parser (see tests/keycombo_test.cpp); KeyComboUtil wraps it
// for QML.
QString qtKeyComboToPortable(int qtKey, int qtModifiers);

#endif // SCHNELLE_ZEICHEN_EDITOR_KEYCOMBO_H
