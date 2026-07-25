// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SingleInstance.h"

#include "core/editor_protocol.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QQuickWindow>

using schnelle_zeichen::kEditorInterface;
using schnelle_zeichen::kEditorPath;
using schnelle_zeichen::kEditorService;

SingleInstanceAdaptor::SingleInstanceAdaptor(QQuickWindow *window)
    : QDBusAbstractAdaptor(window), window_(window) {}

void SingleInstanceAdaptor::Raise() {
    if (!window_)
        return;
    window_->show();
    window_->raise();
    window_->requestActivate();
}

namespace SingleInstance {

bool acquireOrRaise() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        // No session bus: run unguarded. Better to have a usable
        // editor in headless edge cases than to refuse to start.
        return true;
    }
    if (bus.registerService(QString::fromLatin1(kEditorService))) {
        // Race won: we are the canonical editor.
        return true;
    }
    // Race lost: another editor owns the name. Ask it to surface and
    // tell the caller to bail. Fire-and-forget: we do not wait for a
    // reply, the running instance might be on a slow event loop and
    // we have nothing useful to do with the reply anyway.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kEditorService), QString::fromLatin1(kEditorPath),
        QString::fromLatin1(kEditorInterface), QStringLiteral("Raise"));
    bus.send(msg);
    return false;
}

void registerOnWindow(QQuickWindow *window) {
    if (!window)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    // Adaptor parented to the window so its lifetime tracks the UI.
    // registerObject takes a non-owning pointer to the window; the
    // adaptor's QObject child relationship handles cleanup.
    new SingleInstanceAdaptor(window);
    bus.registerObject(QString::fromLatin1(kEditorPath), window);
}

} // namespace SingleInstance
