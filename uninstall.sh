#!/bin/bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Remove a schnelle-zeichen installation done by install.sh (or a manual
# `cmake --install`): binaries, desktop entry, icon, D-Bus service, bundled
# presets, autostart entries, and optionally the uinput udev rule and the
# user configuration.
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Prompt into REPLY. Under set -e a bare `read` aborts the whole script on
# EOF (non-interactive run, stdin from /dev/null); this guard leaves REPLY
# empty instead, which every prompt below treats as its default answer.
# Deliberately duplicated from install.sh so this script stays standalone;
# keep the two in sync.
prompt() {
    REPLY=""
    read -rp "$1" || true
    echo
}

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  Schnelle Zeichen - Uninstall${NC}"
echo -e "${BLUE}========================================${NC}"
echo

if [ "$EUID" -eq 0 ]; then
    echo -e "${RED}Error: Do not run this script with sudo!${NC}"
    echo "Run as regular user. Sudo will be requested when needed."
    exit 1
fi

# --- Stop running instances ---

echo -e "${BLUE}Stopping running instances...${NC}"
# pkill without -f matches comm (truncated to 15 chars, shorter than these
# names), so match argv[0] in the full command line: an optional path
# prefix, the name, then end of word.
for proc in schnelle-zeichen-tray schnelle-zeichen-overlay \
            schnelle-zeichen-editor schnelle-zeichen; do
    pkill -u "$USER" -f "^([^ ]*/)?$proc(\$| )" 2>/dev/null || true
done
echo -e "${GREEN}✓ Stopped${NC}"
echo

# --- Installed files ---

# Two sources, merged and deduplicated:
#   1. The install_manifest.txt CMake wrote at install time (build-install/
#      from install.sh, build/ from a manual `cmake --install build`): covers
#      every install() target automatically, including ones added after this
#      script was written.
#   2. The static candidate list below: fallback for installs whose build
#      tree is gone, and what keeps this script standalone. New install()
#      targets must still be added here for that case.
PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
MANIFESTS=(
    "$PROJECT_ROOT/build-install/install_manifest.txt"
    "$PROJECT_ROOT/build/install_manifest.txt"
)

# Both common prefixes are checked; missing paths are skipped silently.
CANDIDATES=(
    /usr/bin/schnelle-zeichen
    /usr/bin/schnelle-zeichen-editor
    /usr/bin/schnelle-zeichen-overlay
    /usr/bin/schnelle-zeichen-tray
    /usr/local/bin/schnelle-zeichen
    /usr/local/bin/schnelle-zeichen-editor
    /usr/local/bin/schnelle-zeichen-overlay
    /usr/local/bin/schnelle-zeichen-tray
    /usr/share/applications/schnelle-zeichen-editor.desktop
    /usr/local/share/applications/schnelle-zeichen-editor.desktop
    /usr/share/icons/hicolor/scalable/apps/schnelle-zeichen-editor.svg
    /usr/local/share/icons/hicolor/scalable/apps/schnelle-zeichen-editor.svg
    /usr/share/dbus-1/services/de.schnelle_zeichen.Overlay.service
    /usr/local/share/dbus-1/services/de.schnelle_zeichen.Overlay.service
)
DIR_CANDIDATES=(
    /usr/share/schnelle-zeichen
    /usr/local/share/schnelle-zeichen
)

FOUND=()
declare -A FOUND_SEEN
add_found() {
    [ -f "$1" ] || return 0
    [ -n "${FOUND_SEEN[$1]:-}" ] && return 0
    FOUND_SEEN[$1]=1
    FOUND+=("$1")
}

for m in "${MANIFESTS[@]}"; do
    [ -f "$m" ] || continue
    while IFS= read -r line; do
        add_found "$line"
    done < "$m"
done
for f in "${CANDIDATES[@]}"; do
    add_found "$f"
done
FOUND_DIRS=()
for d in "${DIR_CANDIDATES[@]}"; do
    [ -d "$d" ] && FOUND_DIRS+=("$d")
done

if [ ${#FOUND[@]} -eq 0 ] && [ ${#FOUND_DIRS[@]} -eq 0 ]; then
    echo -e "${YELLOW}No installed files found under /usr or /usr/local.${NC}"
else
    echo -e "${YELLOW}Installed files to remove:${NC}"
    for f in "${FOUND[@]}" "${FOUND_DIRS[@]}"; do
        echo "  - $f"
    done
    echo
    prompt "Remove these files? [Y/n] "
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        [ ${#FOUND[@]} -gt 0 ] && sudo rm -f "${FOUND[@]}"
        [ ${#FOUND_DIRS[@]} -gt 0 ] && sudo rm -rf "${FOUND_DIRS[@]}"
        echo -e "${GREEN}✓ Files removed${NC}"
    fi
    echo
fi

# --- Autostart: systemd user services + XDG entries ---

# install.sh writes one of two mechanisms (systemd user services on opt-in,
# XDG autostart otherwise); remove whichever is present.
# systemd user-unit location + names, and the systemd-user check. Deliberately
# duplicated from install.sh so this script stays standalone; keep the two in
# sync (nothing else checks that they match).
USER_UNIT_DIR="$HOME/.config/systemd/user"
ENGINE_UNIT_NAME=schnelle-zeichen.service
TRAY_UNIT_NAME=schnelle-zeichen-tray.service

have_systemd_user() {
    command -v systemctl >/dev/null 2>&1 &&
        systemctl --user show-environment >/dev/null 2>&1
}

# Disable first so the graphical-session.target.wants symlinks go too, then
# remove the unit files, then reload.
if have_systemd_user; then
    systemctl --user disable --now \
        "$ENGINE_UNIT_NAME" "$TRAY_UNIT_NAME" 2>/dev/null || true
fi
units_removed=0
for f in "$USER_UNIT_DIR/$ENGINE_UNIT_NAME" \
         "$USER_UNIT_DIR/$TRAY_UNIT_NAME"; do
    if [ -f "$f" ]; then
        rm -f "$f"
        units_removed=1
        echo -e "${GREEN}✓ Removed systemd user unit: $f${NC}"
    fi
done
if [ "$units_removed" = 1 ] && have_systemd_user; then
    systemctl --user daemon-reload 2>/dev/null || true
fi

for f in "$HOME/.config/autostart/schnelle-zeichen.desktop" \
         "$HOME/.config/autostart/schnelle-zeichen-tray.desktop"; do
    if [ -f "$f" ]; then
        rm -f "$f"
        echo -e "${GREEN}✓ Removed autostart entry: $f${NC}"
    fi
done
echo

# --- Device-access setup (optional) ---

UDEV_RULE=/etc/udev/rules.d/99-schnelle-zeichen-uinput.rules
MODULES_CONF=/etc/modules-load.d/schnelle-zeichen-uinput.conf

if [ -f "$UDEV_RULE" ] || [ -f "$MODULES_CONF" ]; then
    echo -e "${YELLOW}Device-access setup (uinput udev rule + modules-load):${NC}"
    [ -f "$UDEV_RULE" ] && echo "  - $UDEV_RULE"
    [ -f "$MODULES_CONF" ] && echo "  - $MODULES_CONF"
    echo "Other tools may rely on the same rule; remove only if unsure of none."
    prompt "Remove them? [y/N] "
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        sudo rm -f "$UDEV_RULE" "$MODULES_CONF"
        # Tolerate a missing/inactive udev daemon (e.g. inside a container).
        sudo udevadm control --reload-rules 2>/dev/null || true
        echo -e "${GREEN}✓ Device-access setup removed${NC}"
    fi
    echo
fi

# The 'input' group membership is left in place on purpose: it is a generic
# system group other tools may use. Remove manually if desired:
#   sudo gpasswd -d "$USER" input

# --- User configuration (optional) ---

CONFIG_DIR="$HOME/.config/schnelle-zeichen"
if [ -d "$CONFIG_DIR" ]; then
    echo -e "${YELLOW}User configuration: $CONFIG_DIR${NC}"
    echo "Contains your mappings, profiles, settings and usage data."
    prompt "Delete it too? [y/N] "
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -rf "$CONFIG_DIR"
        echo -e "${GREEN}✓ Configuration removed${NC}"
    else
        echo -e "${GREEN}✓ Configuration kept${NC}"
    fi
    echo
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Uninstall complete${NC}"
echo -e "${GREEN}========================================${NC}"
echo
echo "Note: the 'input' group membership was kept (shared system group)."
