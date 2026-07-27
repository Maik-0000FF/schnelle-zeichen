# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# Nix build of schnelle-zeichen: the evdev engine daemon, the Qt/QML editor,
# the Wayland overlay daemon and the tray companion, each Qt-wrapped with its
# runtime paths (QML imports, platform plugins, SVG image format).
#
# Runtime device access (the input group membership and the uinput udev
# rule/module) is system configuration, not part of this package; see the
# repo README / NixOS snippet.
{
  stdenv,
  lib,
  cmake,
  ninja,
  pkg-config,
  wayland-scanner,
  wayland-protocols,
  kdePackages,
  libevdev,
  libxkbcommon,
  wayland,
  systemdLibs,
  qt6,
  src,
}:

let
  # Single source of truth for the version: parse it from the CMake project()
  # line instead of duplicating it here. Per-line matching avoids
  # builtins.match's newline limitation.
  cmakeLines = lib.splitString "\n" (builtins.readFile (src + "/CMakeLists.txt"));
  projectLine = lib.findFirst (
    l: lib.hasInfix "project(" l && lib.hasInfix "VERSION" l
  ) null cmakeLines;
  versionMatch = if projectLine == null then null else builtins.match ".*VERSION ([0-9.]+).*" projectLine;
  version =
    if versionMatch == null then
      throw "schnelle-zeichen: could not parse VERSION from CMakeLists.txt"
    else
      builtins.head versionMatch;
in

stdenv.mkDerivation {
  pname = "schnelle-zeichen";
  inherit version src;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    wayland-scanner # generates the protocol client code
    # Ships input-method-unstable-v1.xml, which the input-method sink is
    # generated from. Declared explicitly rather than relying on layer-shell-qt
    # to propagate it: that would break on an engine-only build without Qt.
    wayland-protocols
    qt6.wrapQtAppsHook # wraps editor/overlay/tray with QML + plugin paths
  ];

  buildInputs = [
    libevdev
    libxkbcommon
    wayland
    systemdLibs # sd-bus (overlay client + engine control service)
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtsvg # SVG image format for the qrc app logo
    qt6.qtwayland
    kdePackages.layer-shell-qt
  ];

  # Engine + combo contract tests; hardware-free (fake ports).
  doCheck = true;

  meta = {
    description = "Framework-independent fast accent and special-character input (hold + cycle)";
    homepage = "https://github.com/Maik-0000FF/schnelle-zeichen";
    license = lib.licenses.gpl3Plus;
    platforms = lib.platforms.linux;
    mainProgram = "schnelle-zeichen";
  };
}
