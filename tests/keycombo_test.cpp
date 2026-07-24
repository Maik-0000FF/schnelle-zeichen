// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Contract test between the editor's combo writer (qtKeyComboToPortable) and
// the engine's combo parser (parseShortcutCombo): every combo the capture
// field emits must parse to the intended modifier mask + keysym, and every
// key-name spelling the writer can emit must resolve through
// xkb_keysym_from_name. Replaces the legacy testkeycombo, which validated
// against the legacy framework Key class.

#include "combo_parse.h"
#include "keycombo.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <QtCore/qnamespace.h>

#include <cstdio>
#include <string>

using namespace schnelle_zeichen;

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

namespace {

ShortcutCombo parsed(const QString &combo) {
    return parseShortcutCombo(combo.toStdString());
}

// The emitted string must parse and match an event carrying exactly the
// given modifier mask and keysym (the engine's runtime matching).
void checkEmitsAndMatches(int qtKey, int qtMods, const char *expected,
                          uint32_t eventMods, uint32_t eventKeysym) {
    const QString combo = qtKeyComboToPortable(qtKey, qtMods);
    CHECK(combo == QLatin1String(expected));
    const ShortcutCombo c = parsed(combo);
    CHECK(c.valid());
    CHECK(c.matches(eventMods, eventKeysym));
}

void lettersAndDigits() {
    checkEmitsAndMatches(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier,
                         "Control+Alt+J", KeyModifier::Ctrl | KeyModifier::Alt,
                         XKB_KEY_j);
    // Letters are emitted uppercase but must match either case at runtime
    // (both sides fold through xkb_keysym_to_lower).
    checkEmitsAndMatches(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier,
                         "Control+Alt+J", KeyModifier::Ctrl | KeyModifier::Alt,
                         XKB_KEY_J);
    checkEmitsAndMatches(Qt::Key_1, Qt::ControlModifier, "Control+1",
                         static_cast<uint32_t>(KeyModifier::Ctrl), XKB_KEY_1);
}

void modifierSpellings() {
    checkEmitsAndMatches(Qt::Key_A, Qt::MetaModifier, "Super+A",
                         static_cast<uint32_t>(KeyModifier::Super), XKB_KEY_a);
    // Shift is only valid alongside a real modifier, and it is compared.
    checkEmitsAndMatches(Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier,
                         "Control+Shift+A",
                         KeyModifier::Ctrl | KeyModifier::Shift, XKB_KEY_a);
    const ShortcutCombo shifted = parsed(qtKeyComboToPortable(
        Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier));
    CHECK(
        !shifted.matches(static_cast<uint32_t>(KeyModifier::Ctrl), XKB_KEY_a));
}

void functionKeys() {
    checkEmitsAndMatches(Qt::Key_F5, Qt::ControlModifier, "Control+F5",
                         static_cast<uint32_t>(KeyModifier::Ctrl), XKB_KEY_F5);
    checkEmitsAndMatches(Qt::Key_F35, Qt::AltModifier, "Alt+F35",
                         static_cast<uint32_t>(KeyModifier::Alt), XKB_KEY_F35);
}

void rejectedCombos() {
    // No real (non-Shift) modifier.
    CHECK(qtKeyComboToPortable(Qt::Key_J, 0).isEmpty());
    CHECK(qtKeyComboToPortable(Qt::Key_J, Qt::ShiftModifier).isEmpty());
    // Numpad keys deliver KP_* syms at runtime; a main-row binding would
    // never fire, so the capture refuses them.
    CHECK(qtKeyComboToPortable(Qt::Key_1,
                               Qt::ControlModifier | Qt::KeypadModifier)
              .isEmpty());
    // Unsupported base key.
    CHECK(
        qtKeyComboToPortable(Qt::Key_CapsLock, Qt::ControlModifier).isEmpty());
    // The engine agrees: a modifier-less combo string is invalid.
    CHECK(!parsed(QStringLiteral("J")).valid());
    CHECK(!parsed(QStringLiteral("Shift+J")).valid());
}

void symbolNameSpellings() {
    // Every named key the writer can emit, with the keysym the engine must
    // resolve it to. A spelling xkb_keysym_from_name cannot resolve would be
    // a binding that looks valid but never fires.
    const struct {
        int qtKey;
        const char *name;
        uint32_t keysym;
    } kNamed[] = {
        {Qt::Key_Space, "space", XKB_KEY_space},
        {Qt::Key_Return, "Return", XKB_KEY_Return},
        {Qt::Key_Enter, "Return", XKB_KEY_Return},
        {Qt::Key_Tab, "Tab", XKB_KEY_Tab},
        {Qt::Key_Backspace, "BackSpace", XKB_KEY_BackSpace},
        {Qt::Key_Period, "period", XKB_KEY_period},
        {Qt::Key_Comma, "comma", XKB_KEY_comma},
        {Qt::Key_Slash, "slash", XKB_KEY_slash},
        {Qt::Key_Backslash, "backslash", XKB_KEY_backslash},
        {Qt::Key_Semicolon, "semicolon", XKB_KEY_semicolon},
        {Qt::Key_Apostrophe, "apostrophe", XKB_KEY_apostrophe},
        {Qt::Key_BracketLeft, "bracketleft", XKB_KEY_bracketleft},
        {Qt::Key_BracketRight, "bracketright", XKB_KEY_bracketright},
        {Qt::Key_Minus, "minus", XKB_KEY_minus},
        {Qt::Key_Equal, "equal", XKB_KEY_equal},
        {Qt::Key_QuoteLeft, "grave", XKB_KEY_grave},
        {Qt::Key_Left, "Left", XKB_KEY_Left},
        {Qt::Key_Right, "Right", XKB_KEY_Right},
        {Qt::Key_Up, "Up", XKB_KEY_Up},
        {Qt::Key_Down, "Down", XKB_KEY_Down},
        {Qt::Key_Home, "Home", XKB_KEY_Home},
        {Qt::Key_End, "End", XKB_KEY_End},
        {Qt::Key_PageUp, "Prior", XKB_KEY_Prior},
        {Qt::Key_PageDown, "Next", XKB_KEY_Next},
        {Qt::Key_Insert, "Insert", XKB_KEY_Insert},
        {Qt::Key_Delete, "Delete", XKB_KEY_Delete},
    };
    for (const auto &k : kNamed) {
        const std::string expected = std::string("Control+") + k.name;
        checkEmitsAndMatches(k.qtKey, Qt::ControlModifier, expected.c_str(),
                             static_cast<uint32_t>(KeyModifier::Ctrl),
                             k.keysym);
    }
}

void modifierOrderIsCanonicalizedByParse() {
    // The writer emits one fixed order; the parser accepts any order and
    // reduces both to the same mask, so a duplicate check comparing parsed
    // forms treats them as the same shortcut.
    const ShortcutCombo a = parsed(QStringLiteral("Control+Alt+J"));
    const ShortcutCombo b = parsed(QStringLiteral("Alt+Control+j"));
    CHECK(a.valid() && b.valid());
    CHECK(a.modifiers == b.modifiers);
    CHECK(xkb_keysym_to_lower(a.keysym) == xkb_keysym_to_lower(b.keysym));
}

} // namespace

int main() {
    lettersAndDigits();
    modifierSpellings();
    functionKeys();
    rejectedCombos();
    symbolNameSpellings();
    modifierOrderIsCanonicalizedByParse();
    if (failures == 0) {
        std::printf("keycombo_test: ALL OK\n");
        return 0;
    }
    std::printf("keycombo_test: %d failure(s)\n", failures);
    return 1;
}
