# luxnav: the Longhorn navigation bar library

The Explorer header of the Longhorn concept art ("Aero vision"): back/forward orb, breadcrumb with drop-downs, search box, toolbar,
drawn from the visual style through libwuxtheme, all text as GDI text; plain C, no cairo or pango (docs/NAVBAR.md).

    make                    build/lib/libluxnav.a, libluxnav.so.1 (+ .so), build/lib/pkgconfig/luxnav.pc
    make install            lib/libluxnav.so*, include/luxnav/lhnav.h, lib/pkgconfig/luxnav.pc (Requires: wuxtheme)

The C API keeps its `lhnav_` prefix and the header its name `lhnav.h` (as wuxtheme keeps `ut_`), so flxp's header.cpp and
examples/navdemo.c use it unchanged. flxp links libluxnav.a: the explorer's vision header is always built in. Tuning: nav.ini of the frame set (share/frames/aerovision/nav.ini); its `icon_dir` is
relative to lhde's data directories (`resources/4074`), `art_dir` to the frame set.
`art_style` names the style the travel/favorites/breadcrumb art comes from (`auto` = the theme's; `aero-patch` =
`styles/aero-patch.msstyles` through lhde_data_find). The travel art is TravelBack/TravelFwd::AnimationButton (4051-4074, aero
patch), Vista's TravelBand (5048, 5112) or drawn orbs; `LHNAV_DEBUG=1` / `lhnav_art_sources()` list each piece's source
(docs/NAVBAR.md "Where the art comes from").
