{
  lib,
  stdenv,
  src,
  version,
  pkg-config,
  makeWrapper,
  python3,
  libx11,
  libxext,
  libxcursor,
  libxfixes,
  libxinerama,
  libxft,
  libxcb,
  libxkbcommon,
  freetype,
  fontconfig,
  libpng,
  libjpeg,
  libwebp,
  dbus,
  sqlite,
  cairo,
  pango,
  lua5_4,
  mupdf,
  coreutils,
  gnused,
  gnugrep,
  gawk,
  findutils,
  procps,
  util-linux,
  xdpyinfo,
  xprop,
  xsetroot,
  xset,
  xrdb,
  xinit,
  xorg-server,
  feh,
  picom,
  p7zip,
  cabextract,
  gnutar,
  xz,
  zstd,
}:
let
  # pillow: lhde-import's style recipes
  python = python3.withPackages (ps: [ ps.pillow ]);
in
stdenv.mkDerivation {
  pname = "lhde";
  inherit version src;

  nativeBuildInputs = [
    pkg-config
    makeWrapper
    python
  ];

  # python here too, so patchShebangs points the installed tools at it
  buildInputs = [
    libx11
    libxext
    libxcursor
    libxfixes
    libxinerama
    libxft
    libxcb
    libxkbcommon
    freetype
    fontconfig
    libpng
    libjpeg
    libwebp
    dbus
    sqlite
    cairo
    pango
    lua5_4
    mupdf
    python
  ];

  strictDeps = true;
  enableParallelBuilding = true;

  makeFlags = [ "PREFIX=${placeholder "out"}" ];
  # skip the tests and examples that `all` pulls in
  buildFlags = [
    "libs"
    "programs"
  ];

  # make check-bare: fileops_test copies setuid and setgid files, which the build sandbox forbids
  doCheck = false;

  # only scripts: lhde-session finds its processes with pgrep -x, so the binaries keep their names
  postInstall = ''
    wrapProgram $out/bin/lhde-session --prefix PATH : ${
      lib.makeBinPath [
        coreutils
        gnused
        gnugrep
        gawk
        findutils
        procps
        util-linux
        dbus
        xdpyinfo
        xprop
        xsetroot
        xset
        xrdb
        xinit
        xorg-server
        feh
        picom
      ]
    }
    for p in lhde-import lhde-pack; do
      wrapProgram $out/bin/$p --prefix PATH : ${
        lib.makeBinPath [
          p7zip
          cabextract
          gnutar
          xz
          zstd
        ]
      }
    done
  '';

  passthru.providedSessions = [ "lhde" ];

  meta = {
    description = "Longhorn desktop for Linux/X11";
    homepage = "https://github.com/aap/lhde";
    license = with lib.licenses; [
      gpl2Only
      lgpl21Plus
    ];
    platforms = lib.platforms.linux;
    mainProgram = "lhde-session";
  };
}
