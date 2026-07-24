# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

{
  description = "Schnelle Zeichen: framework-independent fast accent/special-character input (hold + cycle), a portable core with swappable input backends";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      # Linux-only for now: the primary backend is the evdev/uinput interposer,
      # which sits below the display server (works on X11 and Wayland). Darwin
      # (CGEventTap/CGEventPost) systems get added together with the macOS
      # backend, whose dependency set differs (no libevdev there).
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      # No package output yet: the tree is still pure interface headers with no
      # buildable target. A nix/package.nix lands once the first binary does,
      # mirroring the schnelle-umlaute layout.
      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            name = "schnelle-zeichen-dev";

            # Build tooling. clang-tools ships clang-format + clang-tidy so the
            # editor and CI agree with the repo's .clang-format / .clang-tidy.
            nativeBuildInputs = [
              pkgs.cmake
              pkgs.ninja
              pkgs.pkg-config
              pkgs.clang-tools
              pkgs.gettext
              pkgs.reuse # REUSE/SPDX compliance lint
              pkgs.wayland-scanner # generates protocol client code
            ];

            # Libraries the portable core and the Linux input backend link
            # against.
            buildInputs = [
              # Input layer: read the raw key stream and resolve keycodes to
              # characters via the active layout in-process, with no IME
              # framework in the loop.
              pkgs.libevdev # evdev read of /dev/input
              pkgs.libxkbcommon # keycode + modifiers -> UTF-8 character
              pkgs.wayland # future virtual-keyboard / input-method backends
              pkgs.wayland-protocols
              pkgs.at-spi2-core # caret rectangle via the accessibility bus
              pkgs.glib # libatspi's event loop

              # Overlay UI (ported from schnelle-umlaute): drives the cycling
              # preview where no in-app pre-edit channel exists.
              pkgs.qt6.qtbase
              pkgs.qt6.qtdeclarative
              pkgs.qt6.qtsvg # SVG image format for the editor's qrc app logo
              pkgs.qt6.qtwayland
              pkgs.kdePackages.layer-shell-qt # wlr-layer-shell for the overlay
              pkgs.systemdLibs # sd-bus for the engine-side overlay client
            ];

            # evdev read needs /dev/input and uinput injection needs /dev/uinput;
            # both require a one-time device-permission grant at runtime (udev
            # rule / input group), never from inside this shell.
            shellHook = ''
              # QML runtime imports for build-tree runs (QtQuick.Controls/
              # Layouts/Window live in qtdeclarative). Installed binaries get
              # this baked in via wrapQtApps at packaging time (phase 8).
              export QML2_IMPORT_PATH="${pkgs.qt6.qtdeclarative}/lib/qt-6/qml''${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
              export QML_IMPORT_PATH="$QML2_IMPORT_PATH"
              # Layer-shell integration plugin, so the overlay window becomes
              # a real layer surface regardless of what the system profile
              # happens to provide.
              export QT_PLUGIN_PATH="${pkgs.kdePackages.layer-shell-qt}/lib/qt-6/plugins''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
              # SVG imageformats plugin, so the editor's qrc logo renders in
              # build-tree runs (installed binaries get wrapQtApps later).
              export QT_PLUGIN_PATH="${pkgs.qt6.qtsvg}/lib/qt-6/plugins:$QT_PLUGIN_PATH"
              echo "schnelle-zeichen dev shell ready (cmake, ninja, Qt6, libevdev, libxkbcommon, clang-tools)"
            '';
          };
        }
      );
    };
}
