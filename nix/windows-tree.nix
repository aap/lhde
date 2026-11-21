{
  lib,
  stdenvNoCC,
  fetchurl,
  lhde,
  free ? true,
  packs ? [ ],
  styles ? { },
}:
let
  # pack v1: the Makefile names v3 (FREE_PACK_VERSION), which is not published yet
  freePack = fetchurl {
    url = "https://github.com/aap/lhde/releases/download/resources/lhde-resources-free-1.zip";
    hash = "sha256-MFVvGrjFkBc+FRstZGy1pAdQBE5bDmR/yZ5N5P61xyQ=";
  };
in
# one tree for all packs: their index/ links into their own generated/, so two trees cannot share a profile
stdenvNoCC.mkDerivation {
  name = "lhde-windows-tree";

  dontUnpack = true;

  # --no-annotate: xattrs would not survive into the store
  installPhase = ''
    runHook preInstall
    export HOME=$TMPDIR LHDE_WINDOWS=$out/share/lhde-windows
    mkdir -p $LHDE_WINDOWS
    # free first: a later pack's files win, as in make install-resources-full; interpolation copies local packs into the store
    for p in ${lib.escapeShellArgs (map (p: "${p}") (lib.optional free freePack ++ packs))}; do
      ${lhde}/bin/lhde-import --no-annotate --pack $p
    done
    ${lib.concatStrings (
      lib.mapAttrsToList (name: file: ''
        ${lhde}/bin/lhde-import --no-annotate --style ${file} ${lib.escapeShellArg name}
      '') styles
    )}
    runHook postInstall
  '';

  meta = {
    description = "A Windows tree for lhde: ReactOS's free files and photos, and any packs and styles of your own";
    # packs and styles hold microsoft's art
    license =
      lib.optionals free [
        lib.licenses.gpl2Plus
        lib.licenses.lgpl21Plus
        lib.licenses.free
      ]
      ++ lib.optional (packs != [ ] || styles != { }) lib.licenses.unfree;
    platforms = lib.platforms.linux;
  };
}
