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

# Prompt into REPLY. Under set -e a bare `read` aborts the whole script on
# EOF (non-interactive run, stdin from /dev/null); this guard leaves REPLY
# empty instead, which every prompt below treats as its default answer.
# PROMPT_EOF flags the EOF case so destructive prompts can refuse their
# default (consumed in uninstall.sh; unused here, every install.sh prompt
# is additive).
# Deliberately duplicated in uninstall.sh so that script stays standalone;
# keep the two in sync.
# shellcheck disable=SC2034  # PROMPT_EOF is consumed by uninstall.sh's copy
prompt() {
    REPLY=""
    PROMPT_EOF=0
    read -rp "$1" || PROMPT_EOF=1
    echo
}

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
    prompt "Install missing dependencies? [Y/n] "
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
# Dedicated install build dir, never the developer build/: a build/ configured
# in the Nix devshell pins compiler and library paths to /nix/store in its
# CMakeCache, and reusing it would link store paths into /usr/local (broken
# after nix-collect-garbage) or fail cryptically. uninstall.sh finds this
# dir's install_manifest.txt via its build*/ glob; any rename must keep
# matching that pattern.
BUILD_DIR=build-install
# Force the Ninja generator (installed as a dependency above) instead of the
# CMake default (Unix Makefiles): faster, and it removes the reliance on 'make'
# being present, which minimal systems may lack.
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"
echo -e "${GREEN}✓ Build successful${NC}"
echo

# --- Stop running instances ---

# The D-Bus names are single-owner and an EVIOCGRAB is exclusive, so a
# previous instance must exit before the new binaries take over. pkill
# without -f matches comm (truncated to 15 chars, shorter than these
# names), so match argv[0] in the full command line instead: an optional
# path prefix, the name, then end of word (autostart entries launch the
# bare name, terminals may launch a full path).
# The four binary names come from the qt_add_executable / add_executable targets
# in src/*/CMakeLists.txt (engine in src/app); keep this list in sync with them.
for proc in schnelle-zeichen-tray schnelle-zeichen-overlay \
            schnelle-zeichen-editor schnelle-zeichen; do
    pkill -u "$USER" -f "^([^ ]*/)?$proc(\$| )" 2>/dev/null || true
done

# --- Install ---

# cmake's default install prefix (no -DCMAKE_INSTALL_PREFIX passed above), so
# GNUInstallDirs puts the binaries here. Kept as a variable because the systemd
# unit's ExecStart needs the absolute path, and resolving it via `command -v`
# would depend on the caller's PATH (unreliable right after install).
INSTALL_BINDIR=/usr/local/bin

echo -e "${BLUE}Installing...${NC}"
sudo cmake --install "$BUILD_DIR"
echo -e "${GREEN}✓ Installed${NC}"
echo

# --- Device access: input group + uinput ---

# The engine reads /dev/input/event* (grab) and injects the passthrough via
# /dev/uinput. Both are group-guarded: membership in 'input' covers the event
# nodes on standard distros; the udev rule below extends the same group to
# /dev/uinput, and the modules-load entry makes sure uinput exists at boot.
# Canonical device-access paths + rule. The removal side (uninstall.sh), the
# manual setup in docs/INSTALLATION.md and the Nix path (services.udev.extraRules
# in nix/module.nix) mirror the same rule/paths; keep them in sync (nothing
# checks that they match).
UDEV_RULE=/etc/udev/rules.d/99-schnelle-zeichen-uinput.rules
MODULES_CONF=/etc/modules-load.d/schnelle-zeichen-uinput.conf

echo -e "${BLUE}Setting up device access...${NC}"
if id -nG "$USER" | grep -qw input; then
    echo -e "  ${GREEN}✓${NC} $USER is in the 'input' group"
    NEED_RELOGIN=0
else
    # Standard distros ship the 'input' group via udev, but minimal images
    # (containers) can lack it, which would make usermod fail hard.
    getent group input >/dev/null || sudo groupadd input
    sudo usermod -aG input "$USER"
    echo -e "  ${GREEN}✓${NC} $USER added to the 'input' group"
    NEED_RELOGIN=1
fi

# Minimal systems (containers) can ship without udev, so the target dirs may
# not exist yet; create them so the writes below don't fail. No-op elsewhere.
sudo mkdir -p "$(dirname "$UDEV_RULE")" "$(dirname "$MODULES_CONF")"

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
# Tolerate a missing/inactive udev daemon (e.g. inside a container): the rule
# is written either way and applies on the next boot or replug.
sudo udevadm control --reload-rules 2>/dev/null || true
sudo udevadm trigger --name-match=uinput 2>/dev/null || true
echo

# --- Optional autostart ---

# Two mechanisms, and only ever one active at a time:
#   XDG autostart (~/.config/autostart/*.desktop) is the portable default:
#     every XDG-compliant desktop (GNOME, KDE, XFCE, MATE, LXQt, Cinnamon)
#     honours it.
#   systemd user services are an opt-in for sessions that actually start
#     graphical-session.target (systemd-managed GNOME / KDE Plasma, uwsm).
# A systemd user instance merely existing does NOT prove that target is ever
# reached (XFCE/MATE/LXQt have the instance but often do not drive it), and
# that is not detectable at install time, so systemd stays an explicit opt-in
# rather than an auto-detected default.
AUTOSTART_DIR="$HOME/.config/autostart"
ENGINE_DESKTOP="$AUTOSTART_DIR/schnelle-zeichen.desktop"
TRAY_DESKTOP="$AUTOSTART_DIR/schnelle-zeichen-tray.desktop"
# systemd user-unit location + names, and the systemd-user check. Deliberately
# duplicated in uninstall.sh so that script stays standalone; keep the two in
# sync (nothing else checks that they match).
USER_UNIT_DIR="$HOME/.config/systemd/user"
ENGINE_UNIT_NAME=schnelle-zeichen.service
TRAY_UNIT_NAME=schnelle-zeichen-tray.service
ENGINE_UNIT="$USER_UNIT_DIR/$ENGINE_UNIT_NAME"
TRAY_UNIT="$USER_UNIT_DIR/$TRAY_UNIT_NAME"

# A reachable systemd user instance. Necessary but not sufficient: it does not
# prove graphical-session.target is ever started (XFCE/MATE/LXQt have the
# instance but often do not drive it), which is why systemd stays an explicit
# opt-in and XDG autostart the portable default.
have_systemd_user() {
    command -v systemctl >/dev/null 2>&1 &&
        systemctl --user show-environment >/dev/null 2>&1
}

write_desktop_autostart() {
    mkdir -p "$AUTOSTART_DIR"
    cat > "$ENGINE_DESKTOP" << 'EOF'
[Desktop Entry]
Type=Application
Name=Schnelle Zeichen Engine
Exec=schnelle-zeichen
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
    cat > "$TRAY_DESKTOP" << 'EOF'
[Desktop Entry]
Type=Application
Name=Schnelle Zeichen Tray
Exec=schnelle-zeichen-tray
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
    echo -e "${GREEN}✓ XDG autostart entries written to $AUTOSTART_DIR${NC}"
}

remove_desktop_autostart() {
    local removed=0
    for f in "$ENGINE_DESKTOP" "$TRAY_DESKTOP"; do
        if [ -f "$f" ]; then
            rm -f "$f"
            removed=1
        fi
    done
    if [ "$removed" = 1 ]; then
        echo -e "  ${GREEN}✓${NC} removed stale XDG autostart entries"
    fi
}

write_systemd_units() {
    mkdir -p "$USER_UNIT_DIR"
    # Mirrors nix/home-module.nix. on-failure (not always): quitting via the
    # tray or the panic combo (both Shifts) exits 0 and must stay quit; only
    # real errors (missing display, missing device access) restart, capped so
    # a permanent problem lands in failed instead of looping forever.
    cat > "$ENGINE_UNIT" << EOF
[Unit]
Description=schnelle-zeichen engine (evdev grab + uinput passthrough)
After=graphical-session.target
PartOf=graphical-session.target
StartLimitIntervalSec=60
StartLimitBurst=5

[Service]
ExecStart=$INSTALL_BINDIR/schnelle-zeichen
Restart=on-failure
RestartSec=3

[Install]
WantedBy=graphical-session.target
EOF
    cat > "$TRAY_UNIT" << EOF
[Unit]
Description=schnelle-zeichen tray (pause/resume, restart, editor)
After=graphical-session.target schnelle-zeichen.service
PartOf=graphical-session.target
StartLimitIntervalSec=60
StartLimitBurst=5

[Service]
ExecStart=$INSTALL_BINDIR/schnelle-zeichen-tray
Restart=on-failure
RestartSec=3

[Install]
WantedBy=graphical-session.target
EOF
    # Guard both calls: autostart is optional, so a transient user-manager
    # failure must not abort the whole (already completed) installation.
    if ! systemctl --user daemon-reload; then
        echo -e "${YELLOW}  systemd daemon-reload failed; units written but not enabled${NC}"
    elif ! systemctl --user enable "$ENGINE_UNIT_NAME" "$TRAY_UNIT_NAME"; then
        echo -e "${YELLOW}  systemctl --user enable failed; enable manually after login${NC}"
    else
        echo -e "${GREEN}✓ systemd user services enabled (engine + tray)${NC}"
    fi
}

remove_systemd_units() {
    # Disable first so the graphical-session.target.wants symlinks go too, then
    # remove the unit files, then reload.
    if have_systemd_user; then
        systemctl --user disable --now \
            "$ENGINE_UNIT_NAME" "$TRAY_UNIT_NAME" 2>/dev/null || true
    fi
    local removed=0
    for f in "$ENGINE_UNIT" "$TRAY_UNIT"; do
        if [ -f "$f" ]; then
            rm -f "$f"
            removed=1
        fi
    done
    if [ "$removed" = 1 ]; then
        if have_systemd_user; then
            systemctl --user daemon-reload 2>/dev/null || true
        fi
        echo -e "  ${GREEN}✓${NC} removed stale systemd user units"
    fi
}

prompt "Autostart the engine and tray on login? [Y/n] "
if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    use_systemd=0
    if have_systemd_user; then
        prompt "Use systemd user services instead of XDG autostart? [y/N] "
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            use_systemd=1
        fi
    fi
    if [ "$use_systemd" = 1 ]; then
        remove_desktop_autostart # keep exactly one mechanism active
        write_systemd_units
        echo -e "${YELLOW}  Starts on next login. Do not start the engine${NC}"
        echo -e "${YELLOW}  manually before then, a second grab would fail.${NC}"
    else
        remove_systemd_units # keep exactly one mechanism active
        write_desktop_autostart
    fi
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
