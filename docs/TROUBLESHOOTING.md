<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Troubleshooting

## The engine prints "no keyboard found" or "open failed"

Device permissions. The engine needs read access to `/dev/input/event*`
(input group) and write access to `/dev/uinput` (udev rule). Check:

```bash
id -nG | grep -w input          # group membership active in this session?
ls -l /dev/uinput               # group "input", mode 0660?
```

Group membership takes effect at login: log out and back in after
installation. If `/dev/uinput` is missing, load the module
(`sudo modprobe uinput`) and verify the modules-load entry exists.

## Typing works, but no accents appear

- Is the engine running? Start `schnelle-zeichen` in a terminal and watch
  its log; `[config]` shows the loaded profile and mapping count.
- Is it paused? The tray icon tooltip shows "(paused)"; resume from the
  tray menu or the pause shortcut.
- Is the held key mapped in the **active** profile? The editor's profile
  dropdown marks the active profile with a checkmark; editing a different
  profile does not change what the engine uses.
- Is the leader enabled, and does it arrive inside the `[min, max]` window?

## The engine exits immediately

Holding both Shift keys is the panic exit. Also check the startup log:
a failed virtual-keyboard init means the session does not expose
`zwp_virtual_keyboard_v1` (it needs a Wayland session).

## The overlay does not appear

- The overlay needs `wlr-layer-shell`; GNOME/Mutter and X11 cannot host it.
  The editor's Settings page names the reason for the current session.
- Enable it: Settings → Overlay → Show overlay.
- The engine's startup log shows `[overlay] bus=connected` when the daemon
  is reachable; `UNAVAILABLE` means no session bus connection.
- After an update, restart the engine, the overlay daemon and the editor
  together, so all three speak the same protocol version (the engine
  restarts a stale daemon automatically when it can activate it via D-Bus).

## Accidental accents when typing fast

Space as the leader can consume a word boundary when the previous letter is
still held ("une pomme" becoming "une pommé"). Options, in the editor:

- raise the minimum hold (e.g. 80 ms), so only deliberate holds arm,
- move the leader to an arrow key, Alt, or a custom key, keeping Space free.

## The tray shows "engine not running" although it runs

The tray talks to the engine over the session D-Bus. Both must run in the
same user session; check the engine log for
`[control] de.schnelle_zeichen.Engine on session bus`. Without that line,
another instance may already own the name or the session bus is
unavailable.

## The editor shows no presets in the library

The presets are read from the installed data directory
(`<prefix>/share/schnelle-zeichen/presets`). For a source checkout without
an install, point the editor at the repo:
`SCHNELLE_ZEICHEN_PRESETS_DIR=/path/to/repo/presets schnelle-zeichen-editor`.

## Where do I find logs?

The engine, overlay and editor log to stderr. Run them from a terminal to
see `[dev]`, `[config]`, `[overlay]`, `[control]` and `[pause]` lines. An
engine started from the tray runs detached without a terminal; start it
manually when you need its output.
