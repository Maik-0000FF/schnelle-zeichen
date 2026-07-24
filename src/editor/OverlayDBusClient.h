// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_EDITOR_OVERLAY_DBUS_CLIENT_H
#define SCHNELLE_ZEICHEN_EDITOR_OVERLAY_DBUS_CLIENT_H

#include <QObject>
#include <QString>

class OverlayDBusClient : public QObject {
    Q_OBJECT
public:
    explicit OverlayDBusClient(QObject *parent = nullptr);

    // Sends SetTheme to the overlay daemon iff it is already on the bus.
    // The call is skipped if the service is not registered so a theme switch
    // never silently activates the daemon for a user who never enabled
    // the overlay feature.
    void sendTheme(const QString &theme);
    // Same gating for the live corner-style push (SetRounded, protocol v2).
    void sendRounded(bool rounded);
};

#endif // SCHNELLE_ZEICHEN_EDITOR_OVERLAY_DBUS_CLIENT_H
