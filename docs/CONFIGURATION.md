<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Configuration

Configure through the editor (`schnelle-zeichen-editor`) or by editing the
files in `~/.config/schnelle-zeichen/` directly. Either way, the running
engine picks up changes automatically (it watches the config directory);
no restart, no reload command.

| File | Content |
|---|---|
| `mappings.txt` | the Standard profile's mappings |
| `profiles/*.txt` | additional profiles (drop-in files are auto-registered) |
| `profiles.conf` | profile list, active profile, cycle hotkeys |
| `settings.conf` | delays, leaders, app filter, overlay, behavior, theme |
| `merge.conf` | the profile merge (base + appended sources) |
| `usage.conf` | learned usage counters (engine-written) |

## Delays

Each gesture has a time window `[min, max]`: the leader triggers only after
the minimum hold and before the maximum. The editor shows both as one range
slider per case; drag the line between the handles to move the whole window.

| Setting | Default | Meaning |
|---|---|---|
| Lowercase | 400 ms | window upper bound, lowercase |
| Uppercase | 700 ms | upper bound for Shifted gestures (more coordination) |
| LowercaseMin / UppercaseMin | 0 ms | minimum hold; 0 = the leader may arrive instantly |

```ini
[Delay]
Lowercase=400
Uppercase=700
LowercaseMin=0
UppercaseMin=0
Unlimited=False
```

Raise the minimum (e.g. 80 ms) if fast Space taps trigger accents by
accident. `Unlimited=True` removes the upper bound: the window stays open
while the key is held (opt-in, see [Extensions](#extensions)).

## Leaders

Any combination of Space, the four arrows, Alt, AltGr and up to two custom
physical keys. Every leader has its own direction toggle: forward steps to
the next variant, reverse to the previous (a reverse leader entering a cycle
starts at the last variant). At least one leader always stays active; the
editor refuses to disable the last one.

A **custom leader** is captured as a physical key press: the key's position
triggers the leader, on every layout, even with Shift held. Navigation keys
without a character (Home, End, Page Up/Down, Insert, Menu) work too. When
the two custom leaders sit on opposite keyboard halves, each one triggers
only the mappings of the other hand (opposite-hand split). A custom leader
must not itself be a mapped input key; the editor warns on conflicts.

## Mappings

One mapping per line, `key=output`. The key is a single character (uppercase
is a separate key). The output is one variant or a comma-separated cycle:

```
a=ä,á,à
e=é,è,ê,ë
s=ß
g=Guten Tag
```

Escapes inside an output:

| Escape | Meaning |
|---|---|
| `\,` | literal comma |
| `\n` | line break (multi-line snippets) |
| `\t` | tab |
| `\\` | backslash |

To map `#` or `\` as the input key, prefix a backslash: `\#=...`, `\\=...`.
The editor writes all escapes automatically; you need them only when editing
files by hand. Snippets can be whole phrases, signatures or code fragments;
`h=Hello\, World` commits `Hello, World`.

## Profiles, library, merge

A **profile** is a named, switchable mapping set. The protected **Standard**
profile is `mappings.txt`; every other profile is a file under `profiles/`.
Manage them from the dropdown on the Mappings page: add, rename, set active
(checkmark), favorite (star), and assign per-profile switch hotkeys plus
global cycle-next/previous hotkeys. Favorites are what the cycle hotkeys
step through.

**Library:** the Library button lists the bundled presets (languages,
symbols, math, emoji, ...). Adding one copies it into your `profiles/`, so
later edits are yours and survive updates. Any mapping `.txt` dropped into
`profiles/` is auto-registered; optional `# Name:` / `# Description:` /
`# Category:` header comments set its display data.

**Merge:** the merge control (⧉) composes profiles into one view. Click
profiles in order: the first is the base (badge 1), the rest are appended.
The merged result applies while the **base** profile is active; each chip in
the base view is tinted by its source profile, deleting a chip cascades into
that profile (with confirmation), and reordering is stored as a
non-destructive override.

## Usage sorting

**Sort variants by usage** (Mappings page) orders each cycle by how often
you commit each variant, most-used first, both at runtime and in the editor
preview. Non-destructive: the stored order returns when off. **Reset usage
data** clears the learned counters.

## Theme and look

14 themes, shared by the editor and the overlay. The flat, sharp-cornered
look is the default; **Rounded corners** switches both apps to a rounded
look, live.

```ini
[Theme]
Theme=schnelle-zeichen
```

## Overlay

An on-screen panel that mirrors the variants while you cycle, provided by
`schnelle-zeichen-overlay` (started on demand, stopped when disabled).

| Setting | Default | Meaning |
|---|---|---|
| Enabled | False | master switch |
| ShowOnTrigger | False | preview the variants as soon as a mapped key is held |
| Placement | Grid | `Grid` (fixed cell), `MouseCursor` (at the pointer, grid as fallback), or `TextCaret` (at the focused text caret, then pointer, then grid) |
| ProgressBar | False | timing bar: min-hold lead-in, then the window countdown |
| Row / Column | Top / Col4 | grid cell (3 rows × 7 columns) |

The overlay needs the Wayland `wlr-layer-shell` protocol (KDE Plasma, sway,
Hyprland, river, wayfire, Mango, ...). GNOME/Mutter and X11 sessions cannot
host it; everything else keeps working there. Compositors that animate
layer surfaces can exempt the overlay via its stable namespace
`schnelle-zeichen-overlay` (e.g. a `noanim`/`no_anim` layer rule).

`TextCaret` reads the caret position from the accessibility bus (AT-SPI), so it
needs accessibility enabled (`busctl --user get-property org.a11y.Bus /org/a11y/bus org.a11y.Status IsEnabled`; apps started before it was enabled must be
restarted). Coverage is partial by nature: toolkits that expose no caret over
AT-SPI (many terminals, some QML apps) fall back to the pointer, then the grid.

## Extensions

Opt-in behaviors, each separately switchable, all off by default:

| Setting | Meaning |
|---|---|
| Keep the window open while the key is held | no upper time bound (popup feel) |
| Long press pre-selects the first variant | hold past the hold time, release commits, no leader needed |
| Held leader keeps cycling | leader auto-repeat steps the cycle while held |

```ini
[Behavior]
SortByFrequency=False
AutoSelect=False
AutoSelectMs=500
LeaderAutoRepeat=False
PauseToggle=
```

### Recipe: popup feel (Quick Accent style)

For the hold-and-pick feel known from mobile long-press and Windows
PowerToys Quick Accent:

1. **Keep the window open while the key is held** (`Unlimited=True`): no
   deadline, the gesture waits as long as you hold the key.
2. **Show overlay** + **Preview in the trigger window**
   (`ShowOnTrigger=True`): the variants appear on screen as soon as you
   hold a mapped key, before any leader.
3. Optional: **Long press pre-selects the first variant**
   (`AutoSelect=True`): after the hold time the first variant arms by
   itself, so hold + release commits it without touching a leader; tap the
   leader while holding to step to another variant.

Hold <kbd>e</kbd>, watch the variants pop up, tap <kbd>Space</kbd> until the
right one is highlighted, release: done. The classic timed gesture stays
available on top; quick taps and fast rollover typing behave exactly as
before.

## Pause

A runtime switch that suspends all gestures: every key passes through
untouched, only the pause shortcut is still recognized. Toggle it from the
tray menu or the `PauseToggle` shortcut (captured in the editor's Pause
card). Useful in games, where held movement keys would otherwise arm
gestures. Pause is never saved; the engine always starts active.

## App filter

Disable gestures in specific applications (games, password managers):

```ini
[AppFilter]
Mode=Blacklist

[AppFilter/Blacklist]
0=steam
1=keepassxc
```

Modes: `Disabled` (default), `Blacklist` (active everywhere except listed),
`Whitelist` (active only in listed). Matching is a case-sensitive substring
match against the focused application's identifier. Note: the focus source
feeding this filter is still being wired up; until it lands, the filter has
no runtime effect.
