# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# Home Manager module: the engine (and by default the tray) as systemd user
# services bound to the graphical session. Desktops that start
# graphical-session.target (KDE Plasma, GNOME, uwsm-launched compositors)
# autostart both; compositors that do not (plain Hyprland, mango, sway) need
# one session-autostart line, see the Installation docs:
#   systemctl --user import-environment WAYLAND_DISPLAY DISPLAY
#   systemctl --user start --no-block schnelle-zeichen.service
{ self }:
{ config, lib, pkgs, ... }:
let
  cfg = config.services.schnelle-zeichen;
in
{
  options.services.schnelle-zeichen = {
    enable = lib.mkEnableOption "the schnelle-zeichen engine as a user service";

    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      defaultText = lib.literalExpression
        "schnelle-zeichen.packages.\${system}.default";
      description = "The schnelle-zeichen package (engine, editor, overlay, tray).";
    };

    tray.enable = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = ''
        Also run the tray (pause/resume, restart engine, open editor) as a
        user service. Needs a StatusNotifier host in the bar.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    systemd.user.services.schnelle-zeichen = {
      Unit = {
        Description = "schnelle-zeichen engine (evdev grab + uinput passthrough)";
        After = [ "graphical-session.target" ];
        PartOf = [ "graphical-session.target" ];
        # on-failure, not always: quitting via the tray or the panic combo
        # exits 0 and must stay quit; only real errors (missing display,
        # missing device access) restart, capped so a permanent problem
        # lands in failed instead of looping forever.
        StartLimitIntervalSec = 60;
        StartLimitBurst = 5;
      };
      Service = {
        ExecStart = "${cfg.package}/bin/schnelle-zeichen";
        Restart = "on-failure";
        RestartSec = 3;
      };
      Install.WantedBy = [ "graphical-session.target" ];
    };

    systemd.user.services.schnelle-zeichen-tray = lib.mkIf cfg.tray.enable {
      Unit = {
        Description = "schnelle-zeichen tray (pause/resume, restart, editor)";
        After = [ "graphical-session.target" "schnelle-zeichen.service" ];
        PartOf = [ "graphical-session.target" ];
        StartLimitIntervalSec = 60;
        StartLimitBurst = 5;
      };
      Service = {
        ExecStart = "${cfg.package}/bin/schnelle-zeichen-tray";
        Restart = "on-failure";
        RestartSec = 3;
      };
      Install.WantedBy = [ "graphical-session.target" ];
    };
  };
}
