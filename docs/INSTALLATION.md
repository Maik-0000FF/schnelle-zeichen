<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Installation

## Requirements

- Linux with a Wayland session that offers a text injection protocol, either
  `zwp_virtual_keyboard_v1` (wlroots compositors) or `zwp_input_method_v1`
  (KDE Plasma). GNOME/Mutter and native X11 sessions offer neither, so the
  engine cannot run there; a native X11 injector is planned. See
  [Session support](../README.md#session-support) for the KDE limits.
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

The flake ships two modules:

- **`nixosModules.default`**: the package plus device access (uinput
  kernel module, udev rule, optionally the input-group membership).
- **`homeModules.default`**: the engine and the tray as systemd user
  services bound to `graphical-session.target` (`services.schnelle-zeichen`;
  `tray.enable = false` drops the tray service).

```nix
# flake inputs
schnelle-zeichen.url = "github:Maik-0000FF/schnelle-zeichen";

# NixOS configuration
imports = [ inputs.schnelle-zeichen.nixosModules.default ];
programs.schnelle-zeichen = { enable = true; user = "<you>"; };

# Home Manager
imports = [ inputs.schnelle-zeichen.homeModules.default ];
services.schnelle-zeichen.enable = true;
```

Log out and back in once for the group membership. Without the modules,
`nix build` puts all four Qt-wrapped binaries into `result/bin` (`nix run`
starts the engine, `nix run .#editor` the editor), and the device-access
snippet from the module can be written by hand:

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

<!-- The udev rule and these paths mirror install.sh (UDEV_RULE / MODULES_CONF)
     and nix/module.nix (services.udev.extraRules); keep the three in sync. -->

```bash
sudo usermod -aG input "$USER"
echo 'KERNEL=="uinput", GROUP="input", MODE="0660"' | \
  sudo tee /etc/udev/rules.d/99-schnelle-zeichen-uinput.rules
echo uinput | sudo tee /etc/modules-load.d/schnelle-zeichen-uinput.conf
sudo modprobe uinput
sudo udevadm control --reload-rules
```

Log out and back in.

## Running, stopping, autostart

Manual run (any setup):

```bash
schnelle-zeichen           # engine: grabs the keyboard, logs to stderr
schnelle-zeichen-tray      # tray: pause/resume, restart, quit
schnelle-zeichen-editor    # configuration UI
```

Stopping the engine, in order of preference:

- tray menu → **Quit engine** (or **Restart engine** after an update),
- hold **both Shift keys** (panic exit, always works, releases the grab),
- `Ctrl+C` in its terminal, or `pkill -x schnelle-zeichen`.

**Autostart on distros:** `install.sh` offers XDG autostart entries
(`~/.config/autostart/schnelle-zeichen.desktop` and
`...-tray.desktop`), which desktop environments start at login. Remove the
files to disable.

**Autostart on NixOS / Home Manager:** `homeModules.default` (above) sets
this up as `schnelle-zeichen.service` and `schnelle-zeichen-tray.service`.
Hand-rolled equivalent for setups without the module:

```nix
systemd.user.services.schnelle-zeichen = {
  Unit = {
    Description = "schnelle-zeichen engine";
    After = [ "graphical-session.target" ];
    PartOf = [ "graphical-session.target" ];
    # on-failure, not always: quitting via tray/panic combo exits 0 and
    # must stay quit; only real errors restart.
    StartLimitIntervalSec = 60;
    StartLimitBurst = 5;
  };
  Service = {
    ExecStart = "${pkg}/bin/schnelle-zeichen";
    Restart = "on-failure";
    RestartSec = 3;
  };
  Install.WantedBy = [ "graphical-session.target" ];
};
```

Then `systemctl --user start|stop|restart schnelle-zeichen.service` and
`journalctl --user -u schnelle-zeichen.service` for the logs. KDE starts
`graphical-session.target` automatically; compositors that do not (plain
Hyprland, mango, sway) need one session-autostart line:

```bash
systemctl --user import-environment WAYLAND_DISPLAY DISPLAY
systemctl --user start --no-block schnelle-zeichen.service
```

The import matters: the engine needs `WAYLAND_DISPLAY` to reach the
compositor for text injection and exits otherwise (the service then retries).

## Uninstallation

```bash
./uninstall.sh
```

Removes the binaries, desktop entry, icon, D-Bus service and presets, the
autostart entries, and optionally the udev rule and your configuration. The
`input` group membership is kept (it is a shared system group); remove it
manually with `sudo gpasswd -d "$USER" input` if desired.
