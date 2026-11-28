{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.services.xserver.desktopManager.lhde;
  res = cfg.resources;
in
{
  options.services.xserver.desktopManager.lhde = {
    enable = lib.mkEnableOption "lhde, the Longhorn desktop for X11";

    package = lib.mkOption {
      type = lib.types.package;
      description = "The lhde package.";
    };

    resources = {
      free = lib.mkOption {
        type = lib.types.bool;
        default = true;
        description = ''
          Import ReactOS's free files and photos (lhde's free pack), so the free themes
          (classic, lunar, modern, mizu, lautus) are complete without Microsoft files.
        '';
      };

      packs = lib.mkOption {
        type = lib.types.listOf lib.types.path;
        default = [ ];
        example = lib.literalExpression "[ ./lhde_winthemes.zip ]";
        description = ''
          Resource packs made by {command}`lhde-pack` (zip, tarball or directory): Microsoft's files,
          which lhde does not distribute. They are imported after the free pack, so their files win.
        '';
      };

      styles = lib.mkOption {
        type = lib.types.attrsOf lib.types.path;
        default = { };
        example = lib.literalExpression "{ realaero = ./aero.msstyles; }";
        description = ''
          Visual styles dropped in by hand, imported as {command}`lhde-import --style FILE NAME`:
          `realaero`, the RealAero patch, is what the themes aero4074 and aerovision are drawn with.
        '';
      };

      package = lib.mkOption {
        type = lib.types.package;
        default = pkgs.callPackage ./windows-tree.nix {
          lhde = cfg.package;
          inherit (res) free packs styles;
        };
        defaultText = lib.literalMD "a Windows tree built from `free`, `packs` and `styles`";
        description = "The system Windows tree ({file}`share/lhde-windows`).";
      };
    };
  };

  config = lib.mkIf cfg.enable {
    services.xserver.enable = true;
    services.displayManager.sessionPackages = [ cfg.package ];
    # flxp's FileManager1 and portal services
    services.dbus.packages = [ cfg.package ];

    environment.systemPackages = [
      cfg.package
    ]
    ++ lib.optional (res.free || res.packs != [ ] || res.styles != { }) res.package;
    environment.pathsToLink = [ "/share/lhde-windows" ];
  };
}
