<!--
SPDX-FileCopyrightText: 2026 Maik-0000FF
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing

Thanks for your interest in improving Schnelle Zeichen. One of the easiest
and most valuable contributions is improving the **character presets**.

## Improving a preset (native speakers welcome)

The bundled presets in [`presets/`](presets/) are plain text files, quick to
edit, no build required. If you are a native speaker, help making your
language's preset complete and correct is especially appreciated.

Each preset is one file, for example [`presets/francais.txt`](presets/francais.txt)
(shortened here; the real file also covers `i`, `o`, `u` and `y`):

```
# Name: Français
# Description: Accents français (à â æ ç é è ê ë î ï ô œ ù û ü ÿ)
# Category: language

a=à,â,æ
c=ç
e=é,è,ê,ë
A=À,Â,Æ
C=Ç
E=É,È,Ê,Ë
```

### Format

- Header comments: `# Name:` (shown in the library), `# Description:`, and
  `# Category:` (`language`, `symbols`, or `emoji`), then a blank line.
- One mapping per line: `key=variant1,variant2,...`
  - The **key** is the single character held before the leader. Uppercase is
    a separate key (`A=À,Â`), so include the uppercase forms too.
  - The **variants** are the outputs cycled through, most common first.
- Escapes inside an output: `\,` literal comma, `\n` line break, `\t` tab,
  `\\` backslash.
- To map `#` or `\` as the input key, prefix a backslash: `\#=...`, `\\=...`

### Guidelines for language presets

- Cover the characters the language's standard orthography uses in native
  words, lowercase and uppercase.
- Order variants by frequency, most common first.
- Keep to standard orthography; skip loanword-only or purely stylistic
  diacritics unless they are part of everyday writing.

## Submitting a change

1. Fork the repository and create a branch.
2. Edit or add the preset file under `presets/`.
3. Open a pull request against the `dev` branch, describing the language and
   the change. A source for the orthography is helpful.

## Other contributions

Bug reports and feature ideas are welcome as
[issues](https://github.com/Maik-0000FF/schnelle-zeichen/issues). For code
changes, please open a pull request against `dev`. Lints and tests run in
CI (`nix build`, clang-format, REUSE); running them locally first via
`nix develop` is appreciated.
