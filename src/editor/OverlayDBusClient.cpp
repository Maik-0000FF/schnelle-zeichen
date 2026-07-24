// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OverlayDBusClient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>

#include "core/overlay_protocol.h"

OverlayDBusClient::OverlayDBusClient(QObject *parent) : QObject(parent) {}

void OverlayDBusClient::sendTheme(const QString &theme) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    auto *iface = bus.interface();
    if (!iface)
        return;
    const QString service =
        QString::fromLatin1(schnelle_zeichen::kOverlayService);
    if (!iface->isServiceRegistered(service)) {
        return;
    }
    auto msg = QDBusMessage::createMethodCall(
        service, QString::fromLatin1(schnelle_zeichen::kOverlayPath),
        QString::fromLatin1(schnelle_zeichen::kOverlayInterface),
        QStringLiteral("SetTheme"));
    msg << theme;
    bus.asyncCall(msg);
}
