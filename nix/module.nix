# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# NixOS module: the package on the PATH plus the device access the
# evdev/uinput interposer needs (uinput kernel module, udev rule, optionally
# the input-group membership for one user). The engine autostart lives in
# the Home Manager module (nix/home-module.nix); system and session concerns
# stay separated.
{ self }:
{ config, lib, pkgs, ... }:
let
  cfg = config.programs.schnelle-zeichen;
in
{
  options.programs.schnelle-zeichen = {
    enable = lib.mkEnableOption "schnelle-zeichen (fast accent and special-character input)";

    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      defaultText = lib.literalExpression
        "schnelle-zeichen.packages.\${system}.default";
      description = "The schnelle-zeichen package (engine, editor, overlay, tray).";
    };

    user = lib.mkOption {
      type = lib.types.nullOr lib.types.str;
      default = null;
      example = "alice";
      description = ''
        User to add to the `input` group (read access to
        /dev/input/event* and, via the udev rule, /dev/uinput). Leave
        null if the membership is managed elsewhere. Takes effect after
        the user's next login.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    # The engine injects its uinput passthrough clone through /dev/uinput;
    # the udev rule extends the input group to that node.
    boot.kernelModules = [ "uinput" ];
    services.udev.extraRules = ''
      KERNEL=="uinput", GROUP="input", MODE="0660"
    '';

    users.users = lib.mkIf (cfg.user != null) {
      ${cfg.user}.extraGroups = [ "input" ];
    };
  };
}
