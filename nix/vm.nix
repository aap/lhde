{ modulesPath, pkgs, ... }:
{
  imports = [ "${modulesPath}/virtualisation/qemu-vm.nix" ];

  # run script bin/run-lhde-vm, disk ./lhde.qcow2
  networking.hostName = "lhde";
  system.stateVersion = "26.05";

  services.xserver.desktopManager.lhde.enable = true;
  services.xserver.displayManager.lightdm.enable = true;
  services.displayManager = {
    defaultSession = "lhde";
    autoLogin = {
      enable = true;
      user = "demo";
    };
  };

  users.users.demo = {
    isNormalUser = true;
    password = "demo";
    extraGroups = [
      "wheel"
      "video"
    ];
  };
  security.sudo.wheelNeedsPassword = false;

  fonts.enableDefaultPackages = true;
  # a terminal for lhde-theme's list: cmd, urxvt, xterm, …
  environment.systemPackages = [ pkgs.xterm ];
  # picom's glx backend stops repainting on qemu's software opengl
  environment.sessionVariables.LHDE_PICOM_BACKEND = "xrender";

  # the upstream default theme (aero4074) needs Microsoft files; lunar is complete with the free pack.
  # f: written once, so a theme picked inside the vm survives reboots
  systemd.tmpfiles.settings."10-lhde-demo" = {
    "/home/demo/.config".d = {
      user = "demo";
      group = "users";
      mode = "0755";
    };
    "/home/demo/.config/lhde".d = {
      user = "demo";
      group = "users";
      mode = "0755";
    };
    "/home/demo/.config/lhde/settings.ini".f = {
      user = "demo";
      group = "users";
      mode = "0644";
      argument = "[theme]\nname = lunar\n";
    };
  };

  virtualisation = {
    memorySize = 4096;
    cores = 4;
    resolution = {
      x = 1280;
      y = 800;
    };
  };
}
