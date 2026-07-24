// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OverlayDBusClient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariant>

#include "core/overlay_protocol.h"

namespace {

// Fire-and-forget call to the overlay daemon, only if it is ALREADY on the
// bus: a look change must never D-Bus-activate the daemon for a user who
// never enabled the overlay feature. One gate for every push method.
void callOverlayIfRunning(const QString &method, const QVariant &arg) {
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
        QString::fromLatin1(schnelle_zeichen::kOverlayInterface), method);
    msg << arg;
    bus.asyncCall(msg);
}

} // namespace

OverlayDBusClient::OverlayDBusClient(QObject *parent) : QObject(parent) {}

void OverlayDBusClient::sendTheme(const QString &theme) {
    callOverlayIfRunning(QStringLiteral("SetTheme"), theme);
}

void OverlayDBusClient::sendRounded(bool rounded) {
    callOverlayIfRunning(QStringLiteral("SetRounded"), rounded);
}
