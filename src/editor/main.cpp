// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>

#include "ClickOutsideDefocus.h"
#include "SingleInstance.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(
        QStringLiteral("schnelle-zeichen-editor"));
    QGuiApplication::setOrganizationName(QStringLiteral("schnelle-zeichen"));
    QGuiApplication::setWindowIcon(
        QIcon::fromTheme(QStringLiteral("schnelle-zeichen-editor")));
    QGuiApplication::setApplicationVersion(
        QStringLiteral(SCHNELLE_ZEICHEN_VERSION));

    // Handle --version / --help before any window or single-instance work, so
    // both print and exit immediately regardless of a running editor instance.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Schnelle Zeichen editor"));
    parser.addHelpOption();    // -h / --help
    parser.addVersionOption(); // -v / --version
    parser.process(app);

    // Single-instance check before any UI work. Two editor windows
    // editing the same on-disk config would race on save and silently
    // overwrite each other; raise the existing window instead.
    if (!SingleInstance::acquireOrRaise()) {
        return 0;
    }

    // Pin the Basic style: a platform style would substitute system-palette
    // colours into the controls and break the themed look (the Linux Mint
    // rendering bug class; see also the RESOURCE_PREFIX pin in CMake).
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("appVersion"), QCoreApplication::applicationVersion());
    // loadFromModule is Qt 6.5+; fall back to a direct URL on older Qt
    // (Ubuntu 24.04 / Linux Mint 22 still ship Qt 6.4). Loaded by URL,
    // Main.qml is not associated with its module, so `import SchnelleZeichen`
    // (which provides the Theme singleton) must find the embedded qmldir on an
    // import path. The build tree exposes it via the filesystem, but the
    // installed binary relies purely on the qrc, so add /qt/qml (the pinned
    // RESOURCE_PREFIX) explicitly. Without this the singleton is unresolved
    // once installed and every Theme.* binding is undefined (the controls
    // collapse onto one position).
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("SchnelleZeichen", "Main");
#else
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/SchnelleZeichen/Main.qml")));
#endif
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (window) {
        window->installEventFilter(new ClickOutsideDefocus(window));
        SingleInstance::registerOnWindow(window);
    }
    return app.exec();
}
