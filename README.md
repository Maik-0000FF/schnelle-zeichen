<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Schnelle Zeichen | Fast Accent and Special-Character Input for Linux

<p align="center"><img height="112" src="assets/schnelle-zeichen-logo.svg" alt="Schnelle Zeichen"></p>

[![License: GPL-3.0-or-later](https://img.shields.io/badge/License-GPL--3.0--or--later-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![CI](https://github.com/Maik-0000FF/schnelle-zeichen/actions/workflows/ci.yml/badge.svg)](https://github.com/Maik-0000FF/schnelle-zeichen/actions/workflows/ci.yml)
[![Arch Linux](https://img.shields.io/badge/Arch_Linux-1793D1?logo=arch-linux&logoColor=white)](#quick-start)
![Ubuntu/Debian](https://img.shields.io/badge/Ubuntu%2FDebian-E95420?logo=ubuntu&logoColor=white)
![Linux Mint](https://img.shields.io/badge/Linux%20Mint-87CF3E?logo=linuxmint&logoColor=white)
![Fedora](https://img.shields.io/badge/Fedora-294172?logo=fedora&logoColor=white)
![openSUSE](https://img.shields.io/badge/openSUSE-73BA25?logo=opensuse&logoColor=white)
[![NixOS](https://img.shields.io/badge/NixOS-Flake-5277C3?logo=nixos&logoColor=white)](#nix--nixos)
[![Sponsor](https://img.shields.io/badge/Sponsor-%E2%9D%A4-pink?logo=github)](https://github.com/sponsors/Maik-0000FF)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-ff5e5b?logo=ko-fi&logoColor=white)](https://ko-fi.com/maik0000ff)

Type accents, umlauts, symbols, emoji and text snippets with hold + leader
gestures: hold <kbd>a</kbd>, tap <kbd>Space</kbd>, get `ä`. Tap again to
cycle (`ä` → `á` → `à`). Release to commit. Built for touch typists: the
gesture is the same finger-overlap you already produce when typing fast, so
there is no mode switch, no layout switch and no popup to wait for.

The engine works below the display server (evdev grab, uinput passthrough,
Wayland virtual-keyboard injection). No input-method framework, no clipboard
tricks, no root process; applications see ordinary typing.

**Features**

- Hold + leader gestures with accent cycling, forward or backward per leader
- Any Unicode output: accents, emoji, Greek, math, Braille, multi-line snippets
- Leaders: Space, arrows, Alt, AltGr, or one or two custom physical keys
  (with an opposite-hand split), each separately switchable
- Profiles: named mapping sets, switch hotkeys, favorites cycling, and a
  merge that composes several profiles into one
- Library of 47 bundled presets (languages, symbols, math, emoji, IPA, ...)
- Usage sorting: most-used variant first, learned from your typing
- On-screen overlay while cycling (Wayland layer-shell): 14 themes, position
  grid, mouse-cursor placement, timing progress bar
- Editor app for everything above; the engine reloads changes live
- Tray with pause/resume, engine restart and a pause shortcut (handy in games)
- Works with any keyboard layout and multiple keyboards, hotplug included

## Documentation

- [How It Works](docs/HOW-IT-WORKS.md)
- [Installation](docs/INSTALLATION.md)
- [Configuration](docs/CONFIGURATION.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Contributing](CONTRIBUTING.md)

## Quick Start

### Arch, Debian/Ubuntu, Fedora, openSUSE

```bash
git clone https://github.com/Maik-0000FF/schnelle-zeichen.git
cd schnelle-zeichen
./install.sh
```

The script builds and installs the binaries and sets up device access
(input group + uinput udev rule). Log out and back in once, then start
`schnelle-zeichen`. Remove everything with `./uninstall.sh`.

### Nix / NixOS

```bash
nix build    # result/bin/{schnelle-zeichen,-editor,-overlay,-tray}
```

Device access on NixOS belongs in the system configuration:

```nix
users.users.<you>.extraGroups = [ "input" ];
boot.kernelModules = [ "uinput" ];
services.udev.extraRules = ''
  KERNEL=="uinput", GROUP="input", MODE="0660"
'';
```

## Usage

| Command | Purpose |
|---|---|
| `schnelle-zeichen` | the engine (grabs the keyboard) |
| `schnelle-zeichen-editor` | mappings, profiles, delays, leaders, overlay, theme |
| `schnelle-zeichen-tray` | pause/resume, restart engine, open editor |

Hold a mapped key, tap the leader inside the time window, release to commit.
Typing normally is unaffected: a quick tap gives the plain character, and
rolling into the next key commits instantly.

**Defaults:** <kbd>a</kbd> <kbd>o</kbd> <kbd>u</kbd> <kbd>s</kbd> (and
<kbd>A</kbd> <kbd>O</kbd> <kbd>U</kbd>) map to `ä ö ü ß Ä Ö Ü`, Space is the
leader. Everything is configurable.

**Safety:** holding **both Shift keys** exits the engine and releases the
keyboard grab.

> [!NOTE]
> The on-screen overlay needs the Wayland `wlr-layer-shell` protocol (KDE
> Plasma, sway, Hyprland, river, wayfire, Mango, ...). On GNOME/Mutter and
> X11 the overlay is unavailable; typing, cycling and everything else works
> there too.

## Feedback

Found a bug or have a feature idea? Open an
[issue](https://github.com/Maik-0000FF/schnelle-zeichen/issues).

## Support

> If you find Schnelle Zeichen useful, you can support its development:
>
> <a href="https://github.com/sponsors/Maik-0000FF"><img src="https://img.shields.io/badge/Sponsors-Support_this_project-ea4aaa?style=for-the-badge&logo=github" alt="GitHub Sponsors"></a>
> &nbsp;
> <a href="https://ko-fi.com/maik0000ff"><img src="https://img.shields.io/badge/Ko--fi-Buy_me_a_coffee-ff5e5b?style=for-the-badge&logo=ko-fi&logoColor=white" alt="Ko-fi"></a>
>
> A star also helps, it makes this project easier to discover.

## License

GPL-3.0-or-later. Created by [Maik-0000FF](https://github.com/Maik-0000FF).

## Credits

Inspired by [PowerAccent](https://github.com/damienleroy/PowerAccent) by
Damien Leroy, later integrated into Windows PowerToys as Quick Accent.
