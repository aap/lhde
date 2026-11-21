{
  description = "lhde, the Longhorn desktop for X11: package, NixOS module, demo VM";

  inputs = {
    nixpkgs.url = "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.zst";
  };

  outputs =
    inputs:
    let
      vmSystem =
        system:
        lib.nixosSystem {
          modules = [
            inputs.self.nixosModules.default
            ./nix/vm.nix
            { nixpkgs.hostPlatform = system; }
          ];
        };
      inherit (inputs.nixpkgs) lib;
      d = inputs.self.lastModifiedDate;
      # the repository minus the nix files, so editing them does not rebuild lhde
      lhdeArgs = {
        src = lib.fileset.toSource {
          root = ./.;
          fileset = lib.fileset.difference ./. (
            lib.fileset.unions [
              ./nix
              ./flake.nix
              ./flake.lock
            ]
          );
        };
        version = "0-unstable-${builtins.substring 0 4 d}-${builtins.substring 4 2 d}-${builtins.substring 6 2 d}";
      };
    in
    {
      packages = builtins.mapAttrs (
        system: pkgs:
        let
          lhde = pkgs.callPackage ./nix/package.nix lhdeArgs;
        in
        {
          inherit lhde;
          lhde-resources-free = pkgs.callPackage ./nix/windows-tree.nix { inherit lhde; };
          default = lhde;
        }
      ) inputs.nixpkgs.legacyPackages;

      apps = builtins.mapAttrs (
        system: pkgs:
        let
          vm = {
            type = "app";
            program = "${(vmSystem system).config.system.build.vm}/bin/run-lhde-vm";
            meta.description = "lhde in a QEMU VM, logged in as demo";
          };
        in
        {
          inherit vm;
          default = vm;
        }
      ) inputs.nixpkgs.legacyPackages;

      # lhde built with the machine's own nixpkgs, overlays and config
      nixosModules.default =
        { pkgs, ... }:
        {
          imports = [ ./nix/module.nix ];
          services.xserver.desktopManager.lhde.package = lib.mkDefault (
            pkgs.callPackage ./nix/package.nix lhdeArgs
          );
        };

      nixosConfigurations.lhde = vmSystem "x86_64-linux";
    };
}
