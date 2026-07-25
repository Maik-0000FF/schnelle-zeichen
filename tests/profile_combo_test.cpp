// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Shortcut-collision rules of ProfileListModel: SelectKeys, the cycle slots
// and the reserved (pause-toggle) combo share one global shortcut space, so
// the duplicate check must reject collisions in both directions, compare
// canonically (modifier order, case) and always let the empty combo pass.
// Runs against a temporary XDG_CONFIG_HOME so no real config is touched.

#include "ProfileListModel.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

int main(int argc, char *argv[]) {
    // Isolate the config root BEFORE the model resolves it.
    QTemporaryDir tempDir;
    CHECK(tempDir.isValid());
    qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
    QCoreApplication app(argc, argv);

    ProfileListModel profiles;
    int reportedErrors = 0;
    QObject::connect(&profiles, &ProfileListModel::errorOccurred,
                     [&reportedErrors](const QString &) { ++reportedErrors; });

    // A fresh config root seeds the protected Standard profile.
    CHECK(profiles.rowCount() >= 1);

    // Claim a SelectKey; the same combo in another spelling must be refused
    // on a cycle slot (canonical comparison: modifier order and case).
    CHECK(profiles.setSelectKey(0, QStringLiteral("Ctrl+J")));
    CHECK(reportedErrors == 0);
    profiles.setCycleNext(QStringLiteral("Control+j"));
    CHECK(profiles.cycleNext().isEmpty());
    CHECK(reportedErrors == 1);
    profiles.setCycleNext(QStringLiteral("Ctrl+K"));
    CHECK(profiles.cycleNext() == QStringLiteral("Ctrl+K"));
    CHECK(reportedErrors == 1);

    // The reserved combo (pause toggle) blocks profile shortcuts.
    profiles.setReservedCombo(QStringLiteral("Ctrl+P"));
    CHECK(!profiles.setSelectKey(0, QStringLiteral("Ctrl+P")));
    CHECK(reportedErrors == 2);
    CHECK(!profiles.setSelectKey(0, QStringLiteral("Control+p")));
    CHECK(reportedErrors == 3);

    // The pause-toggle pre-check rejects profile combos (canonically), but
    // NOT the reserved combo itself, so re-capturing the current pause
    // toggle stays valid.
    CHECK(!profiles.checkComboAvailable(QStringLiteral("Ctrl+J")));
    CHECK(reportedErrors == 4);
    // Canonical collision with the cycle slot ("Ctrl+K" claimed above).
    CHECK(!profiles.checkComboAvailable(QStringLiteral("Control+k")));
    CHECK(reportedErrors == 5);
    CHECK(profiles.checkComboAvailable(QStringLiteral("Ctrl+P")));
    CHECK(profiles.checkComboAvailable(QStringLiteral("Ctrl+X")));

    // The empty combo (clearing a shortcut) is always free.
    CHECK(profiles.checkComboAvailable(QString()));
    CHECK(profiles.setSelectKey(0, QString()));

    // With the SelectKey cleared, its old combo is free again.
    CHECK(profiles.checkComboAvailable(QStringLiteral("Ctrl+J")));

    if (failures == 0) {
        std::printf("profile_combo_test: all checks passed\n");
        return 0;
    }
    std::printf("profile_combo_test: %d check(s) failed\n", failures);
    return 1;
}
