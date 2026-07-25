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
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTimer>

#include <csignal>
#include <functional>
#include <memory>

#include "core/control_protocol.h"
#include "core/editor_protocol.h"

namespace {

using schnelle_zeichen::kEditorInterface;
using schnelle_zeichen::kEditorPath;
using schnelle_zeichen::kEditorService;

constexpr auto kEditorBinary = "schnelle-zeichen-editor";
// The engine's systemd user unit (as the Home Manager module names it).
// When it exists, lifecycle actions go through systemctl so the manager's
// state stays truthful; a detached spawn would leave the unit "inactive"
// while an unmanaged engine runs.
constexpr auto kEngineUnit = "schnelle-zeichen.service";

// Upper bound for one systemctl probe; a user manager that answers slower
// than this is treated as absent.
constexpr int kSystemctlTimeoutMs = 2000;
// Poll interval while waiting for the engine's bus name to disappear.
constexpr int kBusPollIntervalMs = 100;
// Grace period per escalation step (polite Quit, then SIGTERM) before the
// restart moves on.
constexpr int kQuitGraceMs = 2000;

struct EngineUnitState {
    bool loaded = false; // the unit file is known to the user manager
    bool active = false; // the manager currently RUNS the engine
};

// Query LoadState + ActiveState of the engine unit without blocking the GUI
// thread: systemctl runs as a child process, parsed on its finished signal.
// A missing systemctl (FailedToStart) or a hung probe (killed after
// kSystemctlTimeoutMs) reads as "no unit", like the old blocking probe.
// `done` fires exactly once. ActiveState matters, not just LoadState: a
// unit file merely existing next to an unmanaged engine (terminal start,
// pre-unit autostart) must not route lifecycle actions to a systemctl
// no-op.
void probeEngineUnit(QObject *parent,
                     std::function<void(EngineUnitState)> done) {
    auto *probe = new QProcess(parent);
    QObject::connect(probe, &QProcess::errorOccurred, probe,
                     [probe, done](QProcess::ProcessError error) {
                         if (error != QProcess::FailedToStart) {
                             return; // finished() covers every other error
                         }
                         probe->deleteLater();
                         done(EngineUnitState{});
                     });
    QObject::connect(
        probe, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), probe,
        [probe, done](int, QProcess::ExitStatus status) {
            EngineUnitState state;
            // Parse the Key=Value lines by key: systemctl show prints the
            // properties in systemd's own internal order, NOT in request
            // order, so positional parsing (--value) would silently break
            // if a systemd version reorders them.
            if (status == QProcess::NormalExit) {
                QString activeState;
                const QStringList lines =
                    QString::fromUtf8(probe->readAllStandardOutput())
                        .split(QLatin1Char('\n'));
                for (const QString &line : lines) {
                    if (line.startsWith(QLatin1String("LoadState="))) {
                        state.loaded =
                            line.mid(line.indexOf(QLatin1Char('=')) + 1)
                                .trimmed() == QLatin1String("loaded");
                    } else if (line.startsWith(QLatin1String("ActiveState="))) {
                        activeState =
                            line.mid(line.indexOf(QLatin1Char('=')) + 1)
                                .trimmed();
                    }
                }
                state.active = state.loaded &&
                               (activeState == QLatin1String("active") ||
                                activeState == QLatin1String("activating") ||
                                activeState == QLatin1String("reloading"));
            }
            probe->deleteLater();
            done(state);
        });
    QTimer::singleShot(kSystemctlTimeoutMs, probe, [probe] { probe->kill(); });
    probe->start(QStringLiteral("systemctl"),
                 {QStringLiteral("--user"), QStringLiteral("show"),
                  QStringLiteral("-p"), QStringLiteral("LoadState,ActiveState"),
                  QString::fromLatin1(kEngineUnit)});
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
        // Async with a watcher instead of a blocking call: aboutToShow runs
        // synchronously before the menu opens, and a hung engine (exactly
        // the case that needs "Restart engine") would otherwise freeze the
        // menu for the full D-Bus timeout. The menu opens with the last
        // known state and corrects itself when the reply lands.
        auto *watcher = new QDBusPendingCallWatcher(
            engine.asyncCall(QStringLiteral("GetPaused")), &tray);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, &tray,
                         [&](QDBusPendingCallWatcher *w) {
                             const QDBusPendingReply<bool> reply = *w;
                             applyPaused(reply.isValid(),
                                         reply.isValid() && reply.value());
                             w->deleteLater();
                         });
    };

    const auto engineOnBus = [&] {
        auto *iface = bus.isConnected() ? bus.interface() : nullptr;
        return iface != nullptr && iface->isServiceRegistered(engineService());
    };
    // Wait for the engine's bus name to disappear, polling on the event
    // loop (never sleeping the GUI thread); reports the outcome once, true
    // when the name left the bus within totalMs.
    const auto whenEngineGone = [&](int totalMs,
                                    std::function<void(bool)> done) {
        if (!engineOnBus()) {
            done(true);
            return;
        }
        auto *timer = new QTimer(&tray);
        auto deadline = std::make_shared<QElapsedTimer>();
        deadline->start();
        QObject::connect(timer, &QTimer::timeout, timer,
                         [&, timer, deadline, done, totalMs] {
                             if (!engineOnBus()) {
                                 timer->deleteLater();
                                 done(true);
                             } else if (deadline->elapsed() >= totalMs) {
                                 timer->deleteLater();
                                 done(false);
                             }
                         });
        timer->start(kBusPollIntervalMs);
    };

    QObject::connect(&menu, &QMenu::aboutToShow, syncPaused);
    QObject::connect(pauseAction, &QAction::triggered,
                     [&] { engine.asyncCall(QStringLiteral("Toggle")); });
    QObject::connect(editorAction, &QAction::triggered, [] { openEditor(); });
    // Restart like the classic input-method tray restart, covering a frozen
    // daemon too. When the manager RUNS the engine, systemctl restarts it
    // (state stays truthful). Otherwise the escalation runs fully async: a
    // polite D-Bus Quit; if the name stays on the bus (the engine hangs),
    // SIGTERM its bus-owner PID (the signalfd path still releases the
    // grab); then a fresh start, through the unit when a unit file exists
    // so the manager owns the engine from now on. The action disables
    // itself while a restart is in flight so chains cannot overlap.
    QObject::connect(restartEngineAction, &QAction::triggered, [&] {
        restartEngineAction->setEnabled(false);
        const auto finish = [&] {
            restartEngineAction->setEnabled(true);
            syncPaused();
        };
        probeEngineUnit(&tray, [&, finish](EngineUnitState unit) {
            if (unit.active) {
                systemctlUser("restart");
                finish();
                return;
            }
            const auto startFresh = [&, unit, finish] {
                if (unit.loaded) {
                    systemctlUser("start");
                } else {
                    startEngine();
                }
                finish();
            };
            auto *iface = bus.isConnected() ? bus.interface() : nullptr;
            if (iface == nullptr ||
                !iface->isServiceRegistered(engineService())) {
                startFresh();
                return;
            }
            // Resolve the owner PID BEFORE asking it to quit, for the
            // escalation. Deliberately synchronous: the call is served by
            // the bus daemon itself (fast even when the ENGINE hangs), and
            // the PID must be in hand before Quit can race the name off
            // the bus.
            const QDBusReply<quint32> pidReply =
                iface->servicePid(engineService());
            engine.asyncCall(QStringLiteral("Quit"));
            whenEngineGone(kQuitGraceMs, [&, startFresh, pidReply](bool gone) {
                if (gone) {
                    startFresh();
                    return;
                }
                if (pidReply.isValid()) {
                    ::kill(static_cast<pid_t>(pidReply.value()), SIGTERM);
                }
                whenEngineGone(kQuitGraceMs,
                               [startFresh](bool) { startFresh(); });
            });
        });
    });
    QObject::connect(quitEngineAction, &QAction::triggered, [&] {
        // Through systemctl only when the manager actually RUNS the engine:
        // stopping a merely existing, inactive unit next to an unmanaged
        // engine would be a silent no-op while the engine keeps running.
        // Disabled during the probe, like Restart, so a double click cannot
        // fire two chains.
        quitEngineAction->setEnabled(false);
        probeEngineUnit(&tray, [&](EngineUnitState unit) {
            if (unit.active) {
                systemctlUser("stop");
            } else {
                engine.asyncCall(QStringLiteral("Quit"));
            }
            quitEngineAction->setEnabled(true);
        });
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
