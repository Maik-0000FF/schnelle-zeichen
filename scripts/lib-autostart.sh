# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared autostart helpers, sourced by install.sh and uninstall.sh. Single
# source of truth for the systemd user-unit location, the unit names and the
# systemd-user detection, so a unit name can never drift between the install
# and the uninstall side (nothing else checks that the two agree).
# shellcheck shell=bash

USER_UNIT_DIR="$HOME/.config/systemd/user"
ENGINE_UNIT_NAME=schnelle-zeichen.service
TRAY_UNIT_NAME=schnelle-zeichen-tray.service

# A reachable systemd user instance. Necessary but not sufficient: it does not
# prove graphical-session.target is ever started (XFCE/MATE/LXQt have the
# instance but often do not drive it), which is why install.sh keeps systemd an
# explicit opt-in and XDG autostart the portable default.
have_systemd_user() {
    command -v systemctl >/dev/null 2>&1 &&
        systemctl --user show-environment >/dev/null 2>&1
}
