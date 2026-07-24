<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Installation

## Requirements

- Linux with a Wayland session (the engine also runs under X11 setups whose
  session exposes the Wayland virtual-keyboard protocol; a native X11
  injector is planned)
- Read access to `/dev/input/event*` and write access to `/dev/uinput`
  (input group + udev rule; the installer sets this up)
- Build: CMake, a C++20 compiler, pkg-config, wayland-scanner, libevdev,
  libxkbcommon, wayland, libsystemd, Qt 6 (base, declarative, svg, wayland,
  widgets), layer-shell-qt

## Install script (Arch, Debian/Ubuntu, Fedora, openSUSE)

```bash
./install.sh
```

The script:

1. detects the distribution and installs missing dependencies,
2. builds and installs `schnelle-zeichen`, `schnelle-zeichen-editor`,
   `schnelle-zeichen-overlay` and `schnelle-zeichen-tray`,
3. adds you to the `input` group and installs the uinput udev rule and
   modules-load entry,
4. optionally writes autostart entries for the engine and the tray.

Log out and back in once so the group membership takes effect.

## Nix / NixOS

```bash
nix build            # all four binaries in result/bin, Qt-wrapped
nix run              # engine
nix run .#editor     # editor
```

Device access belongs in the system configuration:

```nix
users.users.<you>.extraGroups = [ "input" ];
boot.kernelModules = [ "uinput" ];
services.udev.extraRules = ''
  KERNEL=="uinput", GROUP="input", MODE="0660"
'';
```

## Manual build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

Then grant device access (as the installer would):

```bash
sudo usermod -aG input "$USER"
echo 'KERNEL=="uinput", GROUP="input", MODE="0660"' | \
  sudo tee /etc/udev/rules.d/99-schnelle-zeichen-uinput.rules
echo uinput | sudo tee /etc/modules-load.d/schnelle-zeichen-uinput.conf
sudo modprobe uinput
sudo udevadm control --reload-rules
```

Log out and back in.

## Uninstallation

```bash
./uninstall.sh
```

Removes the binaries, desktop entry, icon, D-Bus service and presets, the
autostart entries, and optionally the udev rule and your configuration. The
`input` group membership is kept (it is a shared system group); remove it
manually with `sudo gpasswd -d "$USER" input` if desired.
