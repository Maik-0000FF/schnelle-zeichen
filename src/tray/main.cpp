// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// The tray companion: a small always-on user-session process exposing the
// engine's runtime controls as a status icon menu (pause/resume, open the
// editor, quit the engine). It is a pure D-Bus client of the engine's
// control interface (core/control_protocol.h) and never touches config
// files itself. QSystemTrayIcon rides the StatusNotifier protocol on
// Wayland, so it works in every bar with an SNI tray (waybar, KDE, ...).

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QThread>

#include <csignal>

#include "core/control_protocol.h"

namespace {

// The editor's single-instance D-Bus identity (see editor/SingleInstance.cpp).
constexpr auto kEditorService = "de.schnelle_zeichen.Editor1";
constexpr auto kEditorPath = "/Editor";
constexpr auto kEditorInterface = "de.schnelle_zeichen.Editor1";
constexpr auto kEditorBinary = "schnelle-zeichen-editor";
// The engine's systemd user unit (as the Home Manager module names it).
// When it exists, lifecycle actions go through systemctl so the manager's
// state stays truthful; a detached spawn would leave the unit "inactive"
// while an unmanaged engine runs.
constexpr auto kEngineUnit = "schnelle-zeichen.service";

// True when the engine's systemd user unit is known to the user manager.
bool engineUnitLoaded() {
    QProcess probe;
    probe.start(QStringLiteral("systemctl"),
                {QStringLiteral("--user"), QStringLiteral("show"),
                 QStringLiteral("-p"), QStringLiteral("LoadState"),
                 QStringLiteral("--value"), QString::fromLatin1(kEngineUnit)});
    if (!probe.waitForFinished(2000)) {
        return false;
    }
    return probe.exitStatus() == QProcess::NormalExit &&
           QString::fromUtf8(probe.readAllStandardOutput()).trimmed() ==
               QStringLiteral("loaded");
}

void systemctlUser(const char *verb) {
    QProcess::startDetached(QStringLiteral("systemctl"),
                            {QStringLiteral("--user"),
                             QString::fromLatin1(verb),
                             QString::fromLatin1(kEngineUnit)});
}

QString engineService() {
    return QString::fromLatin1(schnelle_zeichen::kEngineService);
}

// Raise a running editor via its single-instance service, else start a new
// one: next to this binary (installed layout), the build-tree sibling, or
// PATH as the last resort.
void openEditor() {
    auto bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.interface() &&
        bus.interface()->isServiceRegistered(
            QString::fromLatin1(kEditorService))) {
        bus.asyncCall(QDBusMessage::createMethodCall(
            QString::fromLatin1(kEditorService),
            QString::fromLatin1(kEditorPath),
            QString::fromLatin1(kEditorInterface), QStringLiteral("Raise")));
        return;
    }
    const QString binary = QString::fromLatin1(kEditorBinary);
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/") + binary,
        QCoreApplication::applicationDirPath() + QStringLiteral("/../editor/") +
            binary,
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate) &&
            QProcess::startDetached(candidate, {})) {
            return;
        }
    }
    QProcess::startDetached(binary, {});
}

// Launch the engine binary: next to this binary (installed layout), the
// build-tree sibling, or PATH as the last resort.
void startEngine() {
    const QString binary = QStringLiteral("schnelle-zeichen");
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/") + binary,
        QCoreApplication::applicationDirPath() + QStringLiteral("/../") +
            binary,
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate) &&
            QProcess::startDetached(candidate, {})) {
            return;
        }
    }
    QProcess::startDetached(binary, {});
}

// Restart the engine like the classic input-method tray restart, covering a
// frozen daemon too. With a systemd unit, systemctl does it (and its state
// stays truthful). Without one, escalation: a polite D-Bus Quit first; if
// the name does not leave the bus (the engine hangs), SIGTERM its bus-owner
// PID (the signalfd path still releases the grab); then start a fresh
// instance.
void restartEngine() {
    if (engineUnitLoaded()) {
        systemctlUser("restart");
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    auto *iface = bus.isConnected() ? bus.interface() : nullptr;
    const QString service = engineService();

    const auto engineOnBus = [&] {
        return iface != nullptr && iface->isServiceRegistered(service);
    };
    const auto waitGone = [&](int totalMs) {
        QElapsedTimer timer;
        timer.start();
        while (engineOnBus() && timer.elapsed() < totalMs) {
            QThread::msleep(50);
        }
        return !engineOnBus();
    };

    if (engineOnBus()) {
        // Resolve the owner PID BEFORE asking it to quit, for the escalation.
        const QDBusReply<quint32> pidReply = iface->servicePid(service);
        bus.call(QDBusMessage::createMethodCall(
                     service,
                     QString::fromLatin1(schnelle_zeichen::kEnginePath),
                     QString::fromLatin1(schnelle_zeichen::kEngineInterface),
                     QStringLiteral("Quit")),
                 QDBus::Block, 1000);
        if (!waitGone(2000) && pidReply.isValid()) {
            ::kill(static_cast<pid_t>(pidReply.value()), SIGTERM);
            waitGone(2000);
        }
    }
    startEngine();
}

} // namespace

// QDBusConnection::connect needs a QObject slot; this tiny relay turns the
// engine's PausedChanged signal into a std::function the main scope wires.
class PauseRelay : public QObject {
    Q_OBJECT
public:
    std::function<void(bool)> fn;
public Q_SLOTS:
    void pausedChanged(bool paused) {
        if (fn) {
            fn(paused);
        }
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("schnelle-zeichen-tray"));
    QApplication::setOrganizationName(QStringLiteral("schnelle-zeichen"));
    // The tray lives without any window; closing a transient popup must not
    // exit the app.
    QApplication::setQuitOnLastWindowClosed(false);

    QSystemTrayIcon tray(QIcon::fromTheme(
        QStringLiteral("schnelle-zeichen-editor"),
        QIcon(QStringLiteral(":/tray/schnelle-zeichen-logo.svg"))));
    tray.setToolTip(QStringLiteral("Schnelle Zeichen"));

    QMenu menu;
    // Not checkable on purpose: a check indicator would indent this one
    // entry against the others. The label itself carries the state
    // (Pause <-> Resume), which also reads clearer than a checkbox.
    QAction *pauseAction = menu.addAction(QObject::tr("Pause"));
    QAction *editorAction = menu.addAction(QObject::tr("Open editor"));
    menu.addSeparator();
    // Always enabled: with a running engine it is the recovery path for a
    // hang; with none it simply starts one.
    QAction *restartEngineAction =
        menu.addAction(QObject::tr("Restart engine"));
    QAction *quitEngineAction = menu.addAction(QObject::tr("Quit engine"));
    QAction *quitTrayAction = menu.addAction(QObject::tr("Quit tray"));

    auto bus = QDBusConnection::sessionBus();
    QDBusInterface engine(
        engineService(), QString::fromLatin1(schnelle_zeichen::kEnginePath),
        QString::fromLatin1(schnelle_zeichen::kEngineInterface), bus);

    const auto applyPaused = [&](bool available, bool paused) {
        pauseAction->setEnabled(available);
        quitEngineAction->setEnabled(available);
        pauseAction->setText(paused ? QObject::tr("Resume")
                                    : QObject::tr("Pause"));
        tray.setToolTip(
            !available ? QObject::tr("Schnelle Zeichen (engine not running)")
                       : (paused ? QObject::tr("Schnelle Zeichen (paused)")
                                 : QStringLiteral("Schnelle Zeichen")));
    };
    const auto syncPaused = [&] {
        QDBusReply<bool> reply = engine.call(QStringLiteral("GetPaused"));
        applyPaused(reply.isValid(), reply.isValid() && reply.value());
    };

    QObject::connect(&menu, &QMenu::aboutToShow, syncPaused);
    QObject::connect(pauseAction, &QAction::triggered,
                     [&] { engine.asyncCall(QStringLiteral("Toggle")); });
    QObject::connect(editorAction, &QAction::triggered, [] { openEditor(); });
    QObject::connect(restartEngineAction, &QAction::triggered, [&] {
        restartEngine();
        syncPaused();
    });
    QObject::connect(quitEngineAction, &QAction::triggered, [&] {
        // Through systemctl when a unit manages the engine (a plain D-Bus
        // Quit would leave the unit reporting a stale state).
        if (engineUnitLoaded()) {
            systemctlUser("stop");
        } else {
            engine.asyncCall(QStringLiteral("Quit"));
        }
    });
    QObject::connect(quitTrayAction, &QAction::triggered, &app,
                     &QCoreApplication::quit);

    // Live state: follow the engine's PausedChanged signal (shortcut toggles
    // included), so the label and tooltip are right even without reopening
    // the menu.
    PauseRelay relay;
    relay.fn = [&](bool paused) { applyPaused(true, paused); };
    bus.connect(
        engineService(), QString::fromLatin1(schnelle_zeichen::kEnginePath),
        QString::fromLatin1(schnelle_zeichen::kEngineInterface),
        QStringLiteral("PausedChanged"), &relay, SLOT(pausedChanged(bool)));

    tray.setContextMenu(&menu);
    tray.show();
    syncPaused();

    return QApplication::exec();
}

#include "main.moc"
