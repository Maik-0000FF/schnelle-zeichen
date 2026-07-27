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

Holding both Shift keys is the panic exit. Also check the startup log. The
engine picks its text injection backend at startup and reports it as
`[sink] wayland virtual-keyboard protocol` or `[sink] wayland input-method
protocol`. Two different startup failures are reported separately, because
they call for opposite reactions:

| Message | Meaning | Exit | Restarts? |
|---|---|---|---|
| `no text-injection backend` | the compositor answered and offers neither `zwp_virtual_keyboard_v1` nor `zwp_input_method_v1` (GNOME/Mutter, native X11) | 69 | no |
| `no compositor reachable` | nothing answered on `WAYLAND_DISPLAY`: the session socket is not up yet, or the compositor is restarting | 1 | yes |

The first cannot heal by restarting, so the unit's `RestartPreventExitStatus`
stops the service after a single attempt: it lands in `failed` right away and
the diagnosis stays the last line in the journal instead of drowning in a
start-limit-hit. See [Session support](../README.md#session-support).

The second is transient and keeps the normal retry, as does every other
failure, such as a keyboard that is not plugged in yet.

The retry is bounded: `StartLimitBurst=10` at `RestartSec=3` gives the
compositor roughly 30 seconds to appear, then the service stops as `failed`.
A login slow enough to exceed that leaves the engine down; start it again
with

```bash
systemctl --user start schnelle-zeichen.service
```

or raise `StartLimitBurst` in `~/.config/systemd/user/schnelle-zeichen.service`.

## Nothing is inserted on KDE Plasma

KDE has no virtual-keyboard protocol, so the engine falls back to the
input-method protocol, and that one has three limits:

- It only reaches native Wayland applications that request `text-input` and
  enable it for the focused input. Measured support:

  | Application | Protocol | Delivery verified |
  |---|---|---|
  | KWrite | `zwp_text_input_manager_v2` | yes |
  | Konsole | `zwp_text_input_manager_v2` | protocol only |
  | kitty | `zwp_text_input_manager_v3` | yes |
  | ghostty, WezTerm | `zwp_text_input_manager_v3` | protocol only |

  "Delivery verified" means text was committed through the sink and read back
  from the receiving application, emoji and other multi-byte characters
  included. "Protocol only" means the application requests `text-input` but
  the full round trip was not measured.

- X11 applications receive nothing. Xwayland never requests `text-input` from
  the compositor, so there is no channel into any application running through
  it, whatever that application itself supports.
- With an input-method framework configured (`QT_IM_MODULE`/`GTK_IM_MODULE`
  set to fcitx or ibus), Qt and GTK applications talk to that framework and
  bypass the protocol entirely. The startup log warns about this by name. This
  affects toolkit-based applications, including terminals built on GTK or Qt
  (ghostty, Konsole). Applications that implement `text-input` themselves
  instead of going through a toolkit (kitty, WezTerm) are unaffected and keep
  working even with fcitx configured.

Check which backend is in use, either from the running service or by asking
the engine directly. `--check-session` performs the same protocol handshake
the daemon does, reports the backend and exits without touching a keyboard
(exit 0 supported, 69 unsupported):

```bash
schnelle-zeichen --check-session
journalctl --user -u schnelle-zeichen.service | grep '\[sink\]'
```

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
