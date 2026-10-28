# luxnav: the Longhorn navigation bar library (was lhnav)

The Explorer header of the Longhorn concept art ("Aero vision"): back/forward orb, breadcrumb with drop-downs, search box, toolbar,
drawn from the visual style through libwuxtheme, all text as GDI text; plain C, no cairo or pango (docs/NAVBAR.md).

    make                    build/lib/libluxnav.a, libluxnav.so.1 (+ .so), build/lib/pkgconfig/luxnav.pc
    make install            lib/libluxnav.so*, include/luxnav/lhnav.h, lib/pkgconfig/luxnav.pc (Requires: wuxtheme)

The C API keeps its `lhnav_` prefix and the header its name `lhnav.h` (as wuxtheme keeps `ut_`), so flxp's header.cpp and
examples/navdemo.c use it unchanged. flxp links libluxnav.a: the explorer's vision header is now always built in (the
`LUXNAV_DIR` hook of step 2 is gone). Tuning: nav.ini of the frame set (data/frames/aerovision/nav.ini); its `icon_dir` is
relative to lhde's data directories (`resources/4074`), `art_dir` to the frame set.
