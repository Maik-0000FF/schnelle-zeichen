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
for f in "${CANDIDATES[@]}"; do
    [ -f "$f" ] && FOUND+=("$f")
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
    read -p "Remove these files? [Y/n] " -r
    echo
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        [ ${#FOUND[@]} -gt 0 ] && sudo rm -f "${FOUND[@]}"
        [ ${#FOUND_DIRS[@]} -gt 0 ] && sudo rm -rf "${FOUND_DIRS[@]}"
        echo -e "${GREEN}✓ Files removed${NC}"
    fi
    echo
fi

# --- Autostart entries ---

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
    read -p "Remove them? [y/N] " -r
    echo
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
    read -p "Delete it too? [y/N] " -r
    echo
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
