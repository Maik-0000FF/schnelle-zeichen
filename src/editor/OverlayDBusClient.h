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
    // We skip the call if the service is not registered so a theme switch
    // never silently activates the daemon for a user who never enabled
    // the overlay feature.
    void sendTheme(const QString &theme);
};

#endif // SCHNELLE_ZEICHEN_EDITOR_OVERLAY_DBUS_CLIENT_H
