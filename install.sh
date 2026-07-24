#!/bin/bash
# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build + install schnelle-zeichen: the engine daemon, the editor, the
# overlay daemon and the tray, plus the one-time device-access setup
# (input group membership and the uinput udev rule) the evdev/uinput
# interposer needs.
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  Schnelle Zeichen - Installation${NC}"
echo -e "${BLUE}========================================${NC}"
echo

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"

# --- Distribution detection ---

DISTRO=""
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO_NAME="${PRETTY_NAME:-$ID}"
    for id in $ID $ID_LIKE; do
        case "$id" in
            arch)           DISTRO=arch;   break ;;
            debian|ubuntu)  DISTRO=debian; break ;;
            fedora)         DISTRO=fedora; break ;;
            suse|opensuse*) DISTRO=suse;   break ;;
            nixos)          DISTRO=nixos;  break ;;
        esac
    done
fi

echo -e "${BLUE}Distribution:${NC} ${DISTRO_NAME:-unknown}"
echo

if [ "$DISTRO" = "nixos" ]; then
    echo -e "${YELLOW}NixOS: build via the flake.${NC}"
    echo "  nix build   # result/bin/{schnelle-zeichen,-editor,-overlay,-tray}"
    echo "Device access (input group + uinput rule) belongs in the system"
    echo "configuration; see README.md."
    exit 0
fi

if [ -z "$DISTRO" ]; then
    echo -e "${RED}Error: Unsupported distribution: ${DISTRO_NAME:-unknown}${NC}"
    echo
    echo "Manual build:"
    echo "  1. Install: cmake, ninja/make, pkg-config, g++, wayland-scanner,"
    echo "     libevdev, libxkbcommon, wayland, libsystemd, Qt6 (base,"
    echo "     declarative, svg, wayland, widgets), layer-shell-qt"
    echo "  2. cmake -B build && cmake --build build -j\$(nproc)"
    echo "  3. sudo cmake --install build"
    echo "  4. Add yourself to the 'input' group and install a uinput udev"
    echo "     rule (see the udev section in this script)."
    exit 1
fi

if [ "$EUID" -eq 0 ]; then
    echo -e "${RED}Error: Do not run this script with sudo!${NC}"
    echo "Run as regular user. Sudo will be requested when needed."
    exit 1
fi

echo -e "${YELLOW}Note: This script will require sudo access for:${NC}"
echo "  - Installing dependencies (if missing)"
echo "  - Installing the binaries to system directories"
echo "  - The input-group membership and the uinput udev rule"
echo

# --- Dependencies ---

is_installed() {
    case "$DISTRO" in
        arch)        pacman -Q "$1" >/dev/null 2>&1 ;;
        debian)      dpkg -l "$1" 2>/dev/null | grep -q "^ii" ;;
        fedora|suse) rpm -q "$1" >/dev/null 2>&1 ;;
    esac
}

install_deps() {
    case "$DISTRO" in
        arch)   sudo pacman -S --needed "$@" ;;
        debian) sudo apt update && sudo apt install -y "$@" ;;
        fedora) sudo dnf install -y "$@" ;;
        suse)   sudo zypper install -y "$@" ;;
    esac
}

case "$DISTRO" in
    arch)
        DEPS=(cmake ninja pkgconf gcc wayland wayland-protocols
              libevdev libxkbcommon systemd
              qt6-base qt6-declarative qt6-svg qt6-wayland layer-shell-qt)
        ;;
    debian)
        DEPS=(cmake ninja-build pkg-config g++
              libwayland-dev wayland-protocols libevdev-dev libxkbcommon-dev
              libsystemd-dev
              qt6-base-dev qt6-declarative-dev libqt6svg6-dev
              qt6-wayland qml6-module-qtquick qml6-module-qtquick-controls
              qml6-module-qtquick-layouts qml6-module-qtquick-templates
              qml6-module-qtquick-window qml6-module-qtqml-workerscript
              liblayershellqtinterface-dev)
        ;;
    fedora)
        DEPS=(cmake ninja-build gcc-c++ pkgconf
              wayland-devel wayland-protocols-devel libevdev-devel
              libxkbcommon-devel systemd-devel
              qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel
              qt6-qtwayland layer-shell-qt-devel)
        ;;
    suse)
        DEPS=(cmake ninja gcc-c++ pkgconf-pkg-config
              wayland-devel wayland-protocols-devel libevdev-devel
              libxkbcommon-devel systemd-devel
              qt6-base-devel qt6-declarative-devel qt6-svg-devel
              qt6-wayland layer-shell-qt6-devel)
        ;;
esac

MISSING_DEPS=()
echo -e "${YELLOW}Checking dependencies...${NC}"
for dep in "${DEPS[@]}"; do
    if is_installed "$dep"; then
        echo -e "  ${GREEN}✓${NC} $dep"
    else
        echo -e "  ${RED}✗${NC} $dep (missing)"
        MISSING_DEPS+=("$dep")
    fi
done
echo

if [ ${#MISSING_DEPS[@]} -ne 0 ]; then
    echo -e "${YELLOW}Missing dependencies: ${MISSING_DEPS[*]}${NC}"
    read -p "Install missing dependencies? [Y/n] " -r
    echo
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        install_deps "${MISSING_DEPS[@]}"
        echo -e "${GREEN}✓ Dependencies installed${NC}"
        echo
    else
        echo -e "${RED}Cannot proceed without dependencies.${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}✓ All dependencies already installed${NC}"
    echo
fi

# --- Build ---

echo -e "${BLUE}Building...${NC}"
cd "$PROJECT_ROOT"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
echo -e "${GREEN}✓ Build successful${NC}"
echo

# --- Stop running instances ---

# The D-Bus names are single-owner and an EVIOCGRAB is exclusive, so a
# previous instance must exit before the new binaries take over. -f matches
# the full command line (comm is truncated to 15 chars).
for proc in schnelle-zeichen-tray schnelle-zeichen-overlay \
            schnelle-zeichen-editor schnelle-zeichen; do
    pkill -u "$USER" -x -f ".*/$proc" 2>/dev/null || true
done

# --- Install ---

echo -e "${BLUE}Installing...${NC}"
sudo cmake --install build
echo -e "${GREEN}✓ Installed${NC}"
echo

# --- Device access: input group + uinput ---

# The engine reads /dev/input/event* (grab) and injects the passthrough via
# /dev/uinput. Both are group-guarded: membership in 'input' covers the event
# nodes on standard distros; the udev rule below extends the same group to
# /dev/uinput, and the modules-load entry makes sure uinput exists at boot.
UDEV_RULE=/etc/udev/rules.d/99-schnelle-zeichen-uinput.rules
MODULES_CONF=/etc/modules-load.d/schnelle-zeichen-uinput.conf

echo -e "${BLUE}Setting up device access...${NC}"
if id -nG "$USER" | grep -qw input; then
    echo -e "  ${GREEN}✓${NC} $USER is in the 'input' group"
    NEED_RELOGIN=0
else
    sudo usermod -aG input "$USER"
    echo -e "  ${GREEN}✓${NC} $USER added to the 'input' group"
    NEED_RELOGIN=1
fi

if [ ! -f "$UDEV_RULE" ]; then
    sudo tee "$UDEV_RULE" >/dev/null << 'EOF'
KERNEL=="uinput", GROUP="input", MODE="0660"
EOF
    echo -e "  ${GREEN}✓${NC} udev rule installed: $UDEV_RULE"
else
    echo -e "  ${GREEN}✓${NC} udev rule already present"
fi

if [ ! -f "$MODULES_CONF" ]; then
    sudo tee "$MODULES_CONF" >/dev/null << 'EOF'
uinput
EOF
    echo -e "  ${GREEN}✓${NC} modules-load entry installed: $MODULES_CONF"
else
    echo -e "  ${GREEN}✓${NC} modules-load entry already present"
fi

sudo modprobe uinput 2>/dev/null || true
sudo udevadm control --reload-rules
sudo udevadm trigger --name-match=uinput 2>/dev/null || true
echo

# --- Optional autostart ---

AUTOSTART_DIR="$HOME/.config/autostart"
read -p "Autostart the engine and tray on login? [Y/n] " -r
echo
if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    mkdir -p "$AUTOSTART_DIR"
    cat > "$AUTOSTART_DIR/schnelle-zeichen.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=Schnelle Zeichen Engine
Exec=schnelle-zeichen
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
    cat > "$AUTOSTART_DIR/schnelle-zeichen-tray.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=Schnelle Zeichen Tray
Exec=schnelle-zeichen-tray
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
    echo -e "${GREEN}✓ Autostart entries written to $AUTOSTART_DIR${NC}"
    echo
fi

# --- Final instructions ---

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Installation Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo
if [ "$NEED_RELOGIN" = "1" ]; then
    echo -e "1. ${RED}LOGOUT AND LOGIN${NC} so the 'input' group membership takes effect"
    echo
fi
echo "2. Start the engine (or log back in with autostart enabled):"
echo -e "   ${BLUE}schnelle-zeichen${NC}"
echo
echo "3. Configure mappings, delays, leaders, overlay and theme:"
echo -e "   ${BLUE}schnelle-zeichen-editor${NC}"
echo
echo "4. Tray menu (pause/resume, restart, editor):"
echo -e "   ${BLUE}schnelle-zeichen-tray${NC}"
echo
echo "5. Test it: hold 'a' and tap Space -> ä (defaults; hold longer and tap"
echo "   Space repeatedly to cycle variants)"
echo
echo -e "${YELLOW}Troubleshooting:${NC}"
echo "  - 'no keyboard found / open failed': the group membership needs a"
echo "    fresh login, or the udev rule did not apply (replug or reboot)."
echo "  - Panic exit: hold BOTH Shift keys."
echo "  - See README.md for more help."
