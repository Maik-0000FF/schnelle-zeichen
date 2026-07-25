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
    // Fields ordered pointer-first so the struct packs without padding
    // (clang-analyzer-optin.performance.Padding).
    const struct {
        const char *name;
        int qtKey;
        uint32_t keysym;
    } kNamed[] = {
        {"space", Qt::Key_Space, XKB_KEY_space},
        {"Return", Qt::Key_Return, XKB_KEY_Return},
        {"Return", Qt::Key_Enter, XKB_KEY_Return},
        {"Tab", Qt::Key_Tab, XKB_KEY_Tab},
        {"BackSpace", Qt::Key_Backspace, XKB_KEY_BackSpace},
        {"period", Qt::Key_Period, XKB_KEY_period},
        {"comma", Qt::Key_Comma, XKB_KEY_comma},
        {"slash", Qt::Key_Slash, XKB_KEY_slash},
        {"backslash", Qt::Key_Backslash, XKB_KEY_backslash},
        {"semicolon", Qt::Key_Semicolon, XKB_KEY_semicolon},
        {"apostrophe", Qt::Key_Apostrophe, XKB_KEY_apostrophe},
        {"bracketleft", Qt::Key_BracketLeft, XKB_KEY_bracketleft},
        {"bracketright", Qt::Key_BracketRight, XKB_KEY_bracketright},
        {"minus", Qt::Key_Minus, XKB_KEY_minus},
        {"equal", Qt::Key_Equal, XKB_KEY_equal},
        {"grave", Qt::Key_QuoteLeft, XKB_KEY_grave},
        {"Left", Qt::Key_Left, XKB_KEY_Left},
        {"Right", Qt::Key_Right, XKB_KEY_Right},
        {"Up", Qt::Key_Up, XKB_KEY_Up},
        {"Down", Qt::Key_Down, XKB_KEY_Down},
        {"Home", Qt::Key_Home, XKB_KEY_Home},
        {"End", Qt::Key_End, XKB_KEY_End},
        {"Prior", Qt::Key_PageUp, XKB_KEY_Prior},
        {"Next", Qt::Key_PageDown, XKB_KEY_Next},
        {"Insert", Qt::Key_Insert, XKB_KEY_Insert},
        {"Delete", Qt::Key_Delete, XKB_KEY_Delete},
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
