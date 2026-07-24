// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_EDITOR_KEY_COMBO_UTIL_H
#define SCHNELLE_ZEICHEN_EDITOR_KEY_COMBO_UTIL_H

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include "keycombo.h"

// QML singleton exposing the Qt-to-portable combo conversion to
// KeyCaptureField. The actual mapping lives in qtKeyComboToPortable()
// (keycombo.h) so it can be unit-tested without QML.
class KeyComboUtil : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit KeyComboUtil(QObject *parent = nullptr) : QObject(parent) {}

    // Returns the portable combo string for a captured Qt key + modifier
    // mask, or "" if the combo is not usable as a shortcut.
    Q_INVOKABLE QString toPortable(int qtKey, int qtModifiers) const {
        return qtKeyComboToPortable(qtKey, qtModifiers);
    }
};

#endif // SCHNELLE_ZEICHEN_EDITOR_KEY_COMBO_UTIL_H
