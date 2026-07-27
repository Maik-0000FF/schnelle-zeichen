<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Architecture

Schnelle Zeichen ships four cooperating binaries. The engine is a plain
C++20 daemon without UI; the editor, overlay and tray are Qt 6 apps. They
share on-disk formats through one header-only core and talk over the
session D-Bus.

```
             evdev grab                    uinput clone
 keyboard ──────────────▶ schnelle-zeichen ──────────────▶ applications
                          (engine daemon)
                            │        ▲ │
              virtual-      │        │ │ D-Bus Engine1
              keyboard      │  config│ │ (pause/quit)
              injection     │  watch │ │
                            ▼        │ ▼
                       compositor    │ schnelle-zeichen-tray
                                     │
        D-Bus Overlay1               │ writes ~/.config/schnelle-zeichen/
      ┌──────────────────────────────┴─────────┐
      ▼                                        │
 schnelle-zeichen-overlay             schnelle-zeichen-editor
 (layer-shell daemon)  ◀── SetTheme/SetRounded ┘
```

## Components

| Binary | Role |
|---|---|
| `schnelle-zeichen` | Grabs every physical keyboard (evdev), runs the gesture engine, forwards unhandled events through a per-device uinput clone, injects committed text via the Wayland virtual-keyboard protocol (or the input-method protocol where the compositor lacks it), watches the config directory and reloads live. Hosts the `de.schnelle_zeichen.Engine1` control interface (pause/resume/toggle/quit). |
| `schnelle-zeichen-editor` | Qt Quick app for mappings, profiles, merge, delays, leaders, overlay, themes and extensions. Writes the config files atomically; the engine notices via inotify. Single-instance via `de.schnelle_zeichen.Editor1`. |
| `schnelle-zeichen-overlay` | Layer-shell daemon rendering the cycling preview, progress bar and profile pill. D-Bus-activated on demand, quits when the overlay is disabled. Interface `de.schnelle_zeichen.Overlay1` with a protocol-version handshake, so a stale daemon left over from an upgrade is detected and restarted. |
| `schnelle-zeichen-tray` | Status icon with pause/resume, open editor, restart engine (with a hang-safe escalation: D-Bus quit, then SIGTERM, then start) and quit. Follows the engine's `PausedChanged` signal. |

## The engine

The gesture logic is an event-loop-free, UI-free library (`src/engine/`)
driven through narrow ports: a key source, a text sink, a timer port and an
overlay port. The evdev backend (`src/backends/evdev/`) implements those
ports; unit tests drive the same engine through fakes, covering the gesture
semantics (windows, cycling, rollover, repeat suppression, extensions)
without hardware.

Key-event flow:

1. Every event from a grabbed keyboard reaches the engine first.
2. The pause shortcut and the panic combo (both Shifts) are checked before
   anything else; while paused, everything is forwarded untouched.
3. A mapped key press is held back and starts the gesture window; leaders
   cycle, release commits, any other key commits the plain character
   instantly and passes through (the fast-typing path).
4. Committed text goes through the sink the compositor supports: a private
   virtual keyboard with its own dynamically grown keymap where
   `zwp_virtual_keyboard_v1` exists, otherwise a `zwp_input_method_v1` client
   that commits the text directly. Everything else replays unchanged through
   the uinput clone, so applications cannot tell the interposer is there.

   The two sinks differ in reach. The virtual keyboard injects below the
   toolkit and reaches every application. The input-method sink only reaches
   native Wayland applications that speak `text-input`, never X11 applications
   (Xwayland does not request the protocol), and is bypassed entirely when an
   input-method framework is configured. Selection happens once at startup and
   is reported as `[sink] ...` in the log.

## Configuration flow

There is exactly one writer per file (the editor for settings, profiles and
mappings; the engine for usage counters and the runtime profile switch) and
the formats live in shared headers (`src/core/`), so both sides always parse
and serialize identically. The engine watches the config directory with
inotify (debounced) and reloads settings, profiles and mappings on every
save, including deletes such as dissolving a merge.

## IPC topology

| From | To | Interface | Purpose |
|---|---|---|---|
| engine | overlay | `de.schnelle_zeichen.Overlay1` | Show/Hide, progress, profile pill, version handshake |
| editor | overlay | `de.schnelle_zeichen.Overlay1` | live theme + corner-style push |
| tray | engine | `de.schnelle_zeichen.Engine1` | pause/resume/toggle, quit, state signal |
| tray | editor | `de.schnelle_zeichen.Editor1` | raise the running editor window |
