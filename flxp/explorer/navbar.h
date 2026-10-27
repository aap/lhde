// The Longhorn 4074/4051 navigation bar without composition: what browseui's
// CNavBar draws from the visual style (~/fun/longhorn/COMPOSITION.md 16).
// Its background is the theme part Navbar::Rebar.BACKGROUND (Inactive-,
// Max-, MaxInactiveNavbar by focus and maximise state) over the client
// width, ending in its own dark last row (4074's Rebar.SPLITTER over the
// last 4 rows is not drawn: the clean 4051 edge, header.cpp), the travel
// (Back/Forward) and Favorites AnimationButtons, the NavbarBase gripper
// before the search/favorites buttons, the breadcrumb arrow glyphs, and the
// Navbar.TITLE / breadcrumb fonts and colours.
//
// Everything drawn comes from the msstyles through libuxtheme (src/uxtheme/
// ut.h, both formats): the parts by class/part/state, so any Longhorn style
// with these classes works. Only the placements are measurements (one layout
// per family, picked from the theme's own sizes: the Back image's height and
// whether there is a splitter), see navbar.cpp.
#pragma once
#include <string>
#include "w32/w32.h"

namespace navbar {

enum Variant { ACTIVE, INACTIVE, MAX, MAX_INACTIVE };
// AnimationButton states, in the order of the theme's images
enum { DISABLED, NORMAL, HOT, PRESSED, SPLITPRESSED };

struct Img { HBITMAP bmp = nullptr; int w = 0, h = 0; };   // premultiplied 32bpp top-down DIB

struct Skin {
    bool valid = false;                  // the whole navigation bar (background and buttons)
    bool buttons = false;                // the travel and favorites buttons at least (the glass header uses them)
    const char* name = "";               // the layout family: "Slate 4074", "Jade 4074", "Slate 4051"
    std::string path;                    // the msstyles the images come from
    int height = 0;                      // the Medium band: 53 Slate 4074 (49-row art), else the art's 54 (Jade, aero, Slate 4051)
    Img bg[4];                           // by Variant
    RECT bg_margins[4] = {};             // SIZINGMARGINS as left, top, right, bottom
    Img splitter;                        // 3 states of 4 rows (normal first), or none; only tells Jade 4074 from Slate 4051
    Img gripper;                         // NavbarBase::Rebar.GRIPPER (3 states, stretched) or Rebar.GRIPPER (tiled)
    bool gripper_tile = false;
    int grip_right = 0, grip_y0 = 0, grip_y1 = 0;   // x = width - grip_right
    Img back[4][5], fwd[4][5];           // [Variant][state]
    Img fav[4][4];                       // DISABLED NORMAL HOT PRESSED
    int fwd_x = 38;                      // TravelFwd's ANIMATIONBUTTONRECT left (the back button's width)
    // 4074's small travel and favorites buttons (Small<variant>TravelBack/
    // TravelFwd/Favorites::AnimationButton; Toolbar Size = Small and the Large
    // size's favorites): Slate 24x32, Jade 27x36; none in 4051 and aero 5048
    bool small = false;
    Img sback[4][5], sfwd[4][5], sfav[4][4];
    int sfwd_x = 24, ssplit_top = 0;
    int travel_x = 2, split_top = 0;     // the travel band's left; the drop-down's first row
    int fav_right = 46, fav_y = 0;
    Img arrow[4];                        // breadcrumb separator glyphs, 6 states (normal first)
    // the breadcrumb's segment buttons where the style has them (aero.msstyles:
    // BreadcrumbBarToolbar::Toolbar SPLITBUTTON / SPLITBUTTONDROPDOWN and its
    // glyph, 6 states NORMAL HOT PRESSED DISABLED CHECKED HOTCHECKED); valid
    // without the rest of the bar, else the header draws 4074's own look
    Img crumb_btn, crumb_drop, crumb_down;
    RECT crumb_btn_margins = {}, crumb_drop_margins = {};
    int arrow_gap_l = 0, arrow_gap_r = 0;
    wchar_t title_face[32] = L"Tahoma";
    int title_px = 17, title_weight = 400;
    COLORREF title_color[4] = {};
    int title_x = 0, title_cap = 0;      // ink left, the capitals' top row
    wchar_t crumb_face[32] = L"Tahoma";
    int crumb_px = 11;
    COLORREF crumb_color[4] = {};
    int crumb_x = 0, crumb_cap = 0;
    bool colour_magnifier = false;       // 4051's blue lens, 4074's grey glass
    int mag_right = 62, mag_cy = 19;     // the lens centre, x = width - mag_right
};

// The skin for the session theme: navbar_msstyles (flxp.ini, or the theme's
// flxp_navbar_msstyles), else the theme's msstyles. Not valid when there is
// no session theme or the style has no Navbar/TravelBack classes.
const Skin& skin();
void reload();                       // after a theme switch

// AlphaBlends state `state` of an image made of `states` equal rows of states
void draw(HDC dc, const Img& img, int x, int y, int state = 0, int states = 1);
// One row of an image, stretched over dst (one row high) with its left and
// right margins kept (the navigation bar's dark last row)
void draw_row(HDC dc, const Img& img, int row, const RECT& dst, int left, int right);
// The image's premultiplied pixels (top-down), null if it has none
const uint32_t* pixels(const Img& img);
void free_image(Img& img);
// A travel button at a smaller height T (the concepts' uneven small pair,
// image9.jpeg, lh_winhec_proto_10.jpg): the rows above the drop-down (split
// = CUSTOMSPLITRECT's top) area-averaged (box filter) by T / h, the
// drop-down's rows 1:1 at the bottom over them, box-filtered across only,
// except the glyph's columns [g0, g1], copied unscaled. split = h: the whole
// image scaled (the star).
Img shrink(const Img& src, int T, int split, int g0, int g1);
// The drop-down glyph's columns in a hot image: its dark opaque pixels in
// rows [split, h); false when there is none
bool glyph_columns(const Img& hot, int split, int* g0, int* g1);
// The image (or one of its vertical states) nine-gridded into dst (margins as
// left, top, right, bottom), stretched; tile = repeat the middle instead
void draw_nine(HDC dc, const Img& img, const RECT& dst, const RECT& margins, int state = 0, int states = 1);
// The alpha of an image's pixel (0 outside)
int alpha_at(const Img& img, int x, int y, int state = 0, int states = 1);

}
