// Measured placements (client coordinates, cw = client width; COMPOSITION.md
// 16.3 and template matches of the theme bitmaps on the screenshots):
//   Slate 4074 (lh4074-181608.png): Back at (2,0), Favorites at (cw-46,0),
//     title ink x 83 rows 4..18, crumbs ink x 83 rows 31..38, white; the
//     separator ink 15 px after the text; gripper x cw-92 rows 2..47
//   Jade 4074 (lh4074-181415/181440.png): Back at (2,0), Favorites at
//     (cw-46,5), title ink x 77 rows 6..22, crumbs ink x 89 rows 35..43,
//     separator ink 23 px after the text; gripper x cw-91 rows 4..50
//   Slate 4051 (lh4051-225158.png): Back at (2,0), Favorites at (cw-46,5),
//     band 54 rows (the background's last row is its dark line), title ink
//     x 81 rows 3..15, crumbs black, ink x 83 rows 30..40, a dot gripper
//     at x cw-95 (Rebar.GRIPPER tiled: a dot every 7 rows from row 3)
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "navbar.h"
#include <set>
#include <vector>
#include "shell/lhtheme.h"
#include "wuxtheme/ut.h"

namespace navbar {

namespace {

// The placements, measured per family (the images, fonts and colours are the
// theme's): picked by the travel images' height and the splitter
struct Layout {
    const char* name;
    int grip_right, grip_y0, grip_y1;
    int fav_y;
    int arrow_gap_l, arrow_gap_r;
    const wchar_t* title_face; int title_px, title_weight; int title_x, title_cap;     // fallbacks for the font
    const wchar_t* crumb_face; int crumb_px; int crumb_x, crumb_cap;
    bool colour_magnifier; int mag_right, mag_cy;
};

const Layout layouts[] = {
    { "Slate 4074", 92, 2, 48, 0, 13, 13, L"Tahoma", 17, FW_BOLD, 83, 4, L"Tahoma", 11, 83, 32, false, 62, 19 },
    { "Jade 4074", 91, 4, 51, 5, 19, 18, L"Segoe UI", 17, FW_NORMAL, 77, 7, L"Segoe UI", 12, 89, 35, false, 62, 21 },
    { "Slate 4051", 95, 0, 50, 5, 14, 12, L"Tahoma", 17, FW_BOLD, 81, 4, L"Tahoma", 11, 83, 31, true, 61, 20 },
};

const char* const PREFIX[4] = { "", "Inactive", "Max", "MaxInactive" };   // by Variant

Skin g_skin;
bool g_loaded;

// premultiplied ARGB pixels as a top-down DIB section
Img dib(const uint32_t* px, int w, int h)
{
    Img out;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* dst = nullptr;
    out.bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &dst, nullptr, 0);
    if (!out.bmp) return out;
    memcpy(dst, px, (size_t)w * h * 4);
    out.w = w; out.h = h;
    return out;
}

void free_img(Img& i) { if (i.bmp) DeleteObject(i.bmp); i = Img(); }

void clear()
{
    for (Img& i : g_skin.bg) free_img(i);
    free_img(g_skin.splitter); free_img(g_skin.gripper);
    for (auto& v : g_skin.back) for (Img& i : v) free_img(i);
    for (auto& v : g_skin.fwd) for (Img& i : v) free_img(i);
    for (auto& v : g_skin.fav) for (Img& i : v) free_img(i);
    for (auto& v : g_skin.sback) for (Img& i : v) free_img(i);
    for (auto& v : g_skin.sfwd) for (Img& i : v) free_img(i);
    for (auto& v : g_skin.sfav) for (Img& i : v) free_img(i);
    for (Img& i : g_skin.arrow) free_img(i);
    free_img(g_skin.crumb_btn); free_img(g_skin.crumb_drop); free_img(g_skin.crumb_down);
    g_skin = Skin();
}

// The style, opened through libuxtheme, and lookups that do not fall back to
// another class: a sub-app class that is not in the file is "not there"
// (uxtheme would answer from the plain class, e.g. AnimationButton, which has
// no image), so the caller can take the active variant instead.
struct Style {
    ut_theme* t = nullptr;
    std::set<std::string> classes;
    int imagefile, glyphfile, margins, sizingtype, font, textcolor;
    explicit Style(const std::string& path)
    {
        t = ut_open(path.c_str(), nullptr, nullptr);
        if (!t) return;
        int n = ut_list_classes(t, nullptr, 0);
        std::vector<const char*> names((size_t)n);
        ut_list_classes(t, names.data(), n);
        for (const char* c : names) classes.insert(c);
        imagefile = ut_prop(t, "IMAGEFILE"); glyphfile = ut_prop(t, "GLYPHIMAGEFILE");
        margins = ut_prop(t, "SIZINGMARGINS"); sizingtype = ut_prop(t, "SIZINGTYPE");
        font = ut_prop(t, "FONT"); textcolor = ut_prop(t, "TEXTCOLOR");
    }
    ~Style() { if (t) ut_close(t); }
    bool has(const std::string& cls) const { return classes.count(cls) != 0; }
    // the part has properties of its own (else uxtheme answers from the class: Rebar's IMAGEFILE)
    bool own_part(const std::string& cls, int pt) const
    {
        int parts[64], n = ut_list_parts(t, cls.c_str(), parts, 64);
        for (int i = 0; i < n && i < 64; i++) if (parts[i] == pt) return true;
        return false;
    }
    int part(const std::string& cls, const char* p) const
    {
        std::string base = cls.substr(cls.find("::") == std::string::npos ? 0 : cls.find("::") + 2);
        return ut_part(t, base.c_str(), p);
    }
    int state(const std::string& cls, int part, const char* st) const
    {
        if (!st) return 0;
        std::string base = cls.substr(cls.find("::") == std::string::npos ? 0 : cls.find("::") + 2);
        return ut_state(t, base.c_str(), part, st);
    }
    // the part's image (IMAGEFILE, or GLYPHIMAGEFILE), as it is (all its states)
    Img image(const std::string& cls, const char* p, const char* st = nullptr, bool glyph = false) const
    {
        if (!has(cls)) return Img();
        int pt = part(cls, p), w = 0, h = 0;
        if (pt <= 0 || !own_part(cls, pt)) return Img();
        uint32_t* px = ut_get_bitmap(t, cls.c_str(), pt, state(cls, pt, st), glyph ? glyphfile : imagefile, &w, &h);
        if (!px) return Img();
        Img out = w > 0 && h > 0 ? dib(px, w, h) : Img();
        ut_free(px);
        return out;
    }
    RECT sizing(const std::string& cls, const char* p) const
    {
        ut_margins m = {};
        RECT r = {};
        if (has(cls) && ut_get_margins(t, cls.c_str(), part(cls, p), 0, margins, &m)) r = RECT{ m.left, m.top, m.right, m.bottom };
        return r;
    }
};

// the variant's image, else the active one's (Slate has no Inactive/Max buttons)
Img variant_image(const Style& st, int v, const char* app, const char* cls, const char* part, const char* state, bool glyph = false,
                  const char* pre = "")
{
    // pre: "Small" goes before the variant's prefix (SmallInactiveTravelBack)
    std::string a = app;
    if (*pre && a.compare(0, strlen(pre), pre) == 0) a = a.substr(strlen(pre));
    Img i = st.image(std::string(pre) + PREFIX[v] + a + "::" + cls, part, state, glyph);
    if (!i.bmp && v) i = st.image(std::string(app) + "::" + cls, part, state, glyph);
    return i;
}

void set_face(wchar_t* dst, const char* utf8, const wchar_t* fallback)
{
    size_t i = 0;
    if (utf8 && *utf8) for (; utf8[i] && i < 31; i++) dst[i] = (unsigned char)utf8[i];
    else for (; fallback[i] && i < 31; i++) dst[i] = fallback[i];
    dst[i] = 0;
}

void load_skin()
{
    clear();
    std::string path = shell::theme_setting("navbar_msstyles");
    if (path.empty()) path = shell::session_theme().path("msstyles");
    if (path.empty()) return;
    Style st(path);
    if (!st.t) return;
    Skin& s = g_skin;
    static const char* const STATES[5] = { "DISABLED", "NORMAL", "HOT", "PRESSED", "SPLITPRESSED" };
    bool buttons = true;
    for (int v = 0; v < 4; v++) {
        for (int k = 0; k < 5; k++) {
            s.back[v][k] = variant_image(st, v, "TravelBack", "AnimationButton", "ABBACKGROUND", STATES[k]);
            s.fwd[v][k] = variant_image(st, v, "TravelFwd", "AnimationButton", "ABBACKGROUND", STATES[k]);
        }
        for (int k = 0; k < 4; k++) s.fav[v][k] = variant_image(st, v, "Favorites", "AnimationButton", "ABBACKGROUND", STATES[k]);
        buttons = buttons && s.back[v][NORMAL].bmp && s.fwd[v][NORMAL].bmp && s.fav[v][NORMAL].bmp;
    }
    if (!buttons) { clear(); return; }
    s.path = path;
    s.buttons = true;
    // the drop-down: CUSTOMSPLITRECT's top
    ut_rect split = {};
    if (ut_get_rect(st.t, "TravelBack::AnimationButton", st.part("AnimationButton", "ABBACKGROUND"), 0, ut_prop(st.t, "CUSTOMSPLITRECT"), &split))
        s.split_top = split.top;
    if (s.split_top <= 0) s.split_top = s.back[ACTIVE][NORMAL].h - 10;
    ut_rect ab = {};
    int abr = ut_prop(st.t, "ANIMATIONBUTTONRECT"), abp = st.part("AnimationButton", "ABBACKGROUND");
    if (ut_get_rect(st.t, "TravelFwd::AnimationButton", abp, 0, abr, &ab) && ab.left > 0) s.fwd_x = ab.left;

    // the small ones: "Small" before the variant prefix (Jade: SmallInactiveTravelBack)
    bool small = true;
    for (int v = 0; v < 4; v++) {
        for (int k = 0; k < 5; k++) {
            s.sback[v][k] = variant_image(st, v, "SmallTravelBack", "AnimationButton", "ABBACKGROUND", STATES[k], false, "Small");
            s.sfwd[v][k] = variant_image(st, v, "SmallTravelFwd", "AnimationButton", "ABBACKGROUND", STATES[k], false, "Small");
        }
        for (int k = 0; k < 4; k++) s.sfav[v][k] = variant_image(st, v, "SmallFavorites", "AnimationButton", "ABBACKGROUND", STATES[k], false, "Small");
        small = small && s.sback[v][NORMAL].bmp && s.sfwd[v][NORMAL].bmp && s.sfav[v][NORMAL].bmp;
    }
    s.small = small;
    if (small) {
        if (ut_get_rect(st.t, "SmallTravelFwd::AnimationButton", abp, 0, abr, &ab) && ab.left > 0) s.sfwd_x = ab.left;
        else s.sfwd_x = s.sback[ACTIVE][NORMAL].w;
        if (ut_get_rect(st.t, "SmallTravelBack::AnimationButton", abp, 0, ut_prop(st.t, "CUSTOMSPLITRECT"), &split) && split.top > 0)
            s.ssplit_top = split.top;
        else s.ssplit_top = s.sback[ACTIVE][NORMAL].h - 7;
    }

    // the band
    static const char* const NB[4] = { "Navbar::Rebar", "InactiveNavbar::Rebar", "MaxNavbar::Rebar", "MaxInactiveNavbar::Rebar" };
    bool band = true;
    for (int v = 0; v < 4; v++) {
        std::string c = st.has(NB[v]) ? NB[v] : NB[0];
        s.bg[v] = st.image(c, "BACKGROUND");
        s.bg_margins[v] = st.sizing(c, "BACKGROUND");
        band = band && s.bg[v].bmp;
    }
    s.splitter = st.image("Rebar", "SPLITTER");
    s.gripper = st.image("NavbarBase::Rebar", "GRIPPER");
    if (!s.gripper.bmp) {
        s.gripper = st.image("Rebar", "GRIPPER");
        int type = 0;
        s.gripper_tile = s.gripper.bmp && ut_get_enum(st.t, "Rebar", st.part("Rebar", "GRIPPER"), 0, st.sizingtype, &type) && type == 2;   // ST_TILE
    }
    // the Medium band: the travel art's height, its drop-down on the band's
    // dark last row (4051, doc/4051-explorer-research.md section 1); 4074's
    // short Slate art (49 rows) keeps the measured 53 (lh4074-195242.png) with
    // the art at the band's bottom. The splitter is never drawn (section 2).
    int bh = s.back[ACTIVE][NORMAL].h;
    s.height = bh < 52 ? bh + 4 : bh;
    const Layout& L = bh < 52 ? layouts[0] : s.splitter.bmp ? layouts[1] : layouts[2];
    s.name = L.name;
    s.grip_right = L.grip_right; s.grip_y0 = L.grip_y0; s.grip_y1 = L.grip_y1;
    s.fav_y = L.fav_y;
    s.arrow_gap_l = L.arrow_gap_l; s.arrow_gap_r = L.arrow_gap_r;
    s.title_x = L.title_x; s.title_cap = L.title_cap; s.crumb_x = L.crumb_x; s.crumb_cap = L.crumb_cap;
    s.colour_magnifier = L.colour_magnifier; s.mag_right = L.mag_right; s.mag_cy = L.mag_cy;

    // fonts and colours: Navbar.TITLE (and its NavTitle variants), the
    // breadcrumb toolbar BB:: (Jade) or BreadcrumbBarToolbar:: (Slate)
    static const char* const NT[4] = { "Navbar", "InactiveNavTitle::Navbar", "MaxNavTitle::Navbar", "MaxInactiveNavTitle::Navbar" };
    ut_font f = {};
    int tp = st.part("Navbar", "TITLE");
    if (tp > 0 && ut_get_font(st.t, "Navbar", tp, 0, st.font, &f)) {
        // Slate names the bold face ("Tahoma Bold", weight 400): Windows finds
        // the bold Tahoma by that full name, we ask for Tahoma at 700
        size_t n = strlen(f.face);
        if (n > 5 && !strcmp(f.face + n - 5, " Bold")) { f.face[n - 5] = 0; f.weight = FW_BOLD; }
        set_face(s.title_face, f.face, L.title_face);
        s.title_px = f.height < 0 ? -f.height : L.title_px;
        s.title_weight = f.weight ? f.weight : L.title_weight;
    } else {
        set_face(s.title_face, nullptr, L.title_face);
        s.title_px = L.title_px; s.title_weight = L.title_weight;
    }
    set_face(s.crumb_face, nullptr, L.crumb_face);
    s.crumb_px = L.crumb_px;
    bool bb = st.has("BB::Toolbar");
    for (int v = 0; v < 4; v++) {
        uint32_t c = 0xffffff;
        std::string tc = st.has(NT[v]) ? NT[v] : NT[0];
        if (tp > 0) ut_get_color(st.t, tc.c_str(), tp, 0, st.textcolor, &c);
        s.title_color[v] = RGB(c >> 16 & 255, c >> 8 & 255, c & 255);
        std::string cc = bb ? std::string(PREFIX[v]) + "BB::Toolbar" : "BreadcrumbBarToolbar::Toolbar";
        if (!st.has(cc)) cc = bb ? "BB::Toolbar" : "BreadcrumbBarToolbar::Toolbar";
        c = (s.title_color[v] >> 16 & 255) | (s.title_color[v] & 0xff00) | (s.title_color[v] & 255) << 16;
        if (st.has(cc)) ut_get_color(st.t, cc.c_str(), 0, 0, st.textcolor, &c);
        s.crumb_color[v] = RGB(c >> 16 & 255, c >> 8 & 255, c & 255);
        s.arrow[v] = variant_image(st, v, bb ? "BB" : "BreadcrumbBarToolbar", "Toolbar", "SPLITBUTTONHORZARROW", nullptr, true);
    }
    for (const char* cc : { "BreadcrumbBarToolbar::Toolbar", "BB::Toolbar" }) {
        if (!s.crumb_btn.bmp) { s.crumb_btn = st.image(cc, "SPLITBUTTON"); s.crumb_btn_margins = st.sizing(cc, "SPLITBUTTON"); }
        if (!s.crumb_drop.bmp) {
            s.crumb_drop = st.image(cc, "SPLITBUTTONDROPDOWN");
            s.crumb_down = st.image(cc, "SPLITBUTTONDROPDOWN", nullptr, true);
            s.crumb_drop_margins = st.sizing(cc, "SPLITBUTTONDROPDOWN");
        }
    }
    if (!s.crumb_btn.bmp || !s.crumb_drop.bmp) { free_img(s.crumb_btn); free_img(s.crumb_drop); free_img(s.crumb_down); }
    s.valid = band && s.arrow[ACTIVE].bmp;
}

} // namespace

const Skin& skin()
{
    if (!g_loaded) { g_loaded = true; load_skin(); }
    return g_skin;
}

void reload() { g_loaded = false; }

void draw(HDC dc, const Img& img, int x, int y, int state, int states)
{
    if (!img.bmp || states < 1) return;
    int h = img.h / states;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, img.bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, x, y, img.w, h, mem, 0, state * h, img.w, h, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

void draw_nine(HDC dc, const Img& img, const RECT& dst, const RECT& m, int state, int states)
{
    if (!img.bmp || states < 1) return;
    int sh = img.h / states, sy = state * sh, sw = img.w;
    int dw = dst.right - dst.left, dh = dst.bottom - dst.top;
    if (dw <= 0 || dh <= 0) return;
    // source and destination column/row boundaries: margin, middle, margin
    int sx[4] = { 0, (int)m.left, sw - (int)m.right, sw }, sy4[4] = { 0, (int)m.top, sh - (int)m.bottom, sh };
    int dx[4] = { 0, (int)m.left, dw - (int)m.right, dw }, dy[4] = { 0, (int)m.top, dh - (int)m.bottom, dh };
    if (dx[2] < dx[1]) dx[1] = dx[2] = dw / 2;
    if (dy[2] < dy[1]) dy[1] = dy[2] = dh / 2;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, img.bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            int w = dx[i + 1] - dx[i], h = dy[j + 1] - dy[j], ssw = sx[i + 1] - sx[i], ssh = sy4[j + 1] - sy4[j];
            if (w > 0 && h > 0 && ssw > 0 && ssh > 0)
                AlphaBlend(dc, dst.left + dx[i], dst.top + dy[j], w, h, mem, sx[i], sy + sy4[j], ssw, ssh, bf);
        }
    SelectObject(mem, old);
    DeleteDC(mem);
}

void draw_row(HDC dc, const Img& img, int row, const RECT& dst, int left, int right)
{
    if (!img.bmp || row < 0 || row >= img.h) return;
    int dw = dst.right - dst.left, dh = dst.bottom - dst.top;
    if (dw <= 0 || dh <= 0) return;
    if (left + right > dw) left = right = dw / 2;
    if (left + right >= img.w) left = right = 0;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, img.bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (left) AlphaBlend(dc, dst.left, dst.top, left, dh, mem, 0, row, left, 1, bf);
    AlphaBlend(dc, dst.left + left, dst.top, dw - left - right, dh, mem, left, row, img.w - left - right, 1, bf);
    if (right) AlphaBlend(dc, dst.right - right, dst.top, right, dh, mem, img.w - right, row, right, 1, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

const uint32_t* pixels(const Img& img)
{
    DIBSECTION ds = {};
    if (!img.bmp || GetObjectW(img.bmp, sizeof ds, &ds) != sizeof ds) return nullptr;
    return (const uint32_t*)ds.dsBm.bmBits;
}

void free_image(Img& img) { free_img(img); }

namespace {
// premultiplied src over dst
uint32_t over(uint32_t d, uint32_t s)
{
    uint32_t k = 255 - (s >> 24), out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t v = (s >> sh & 255) + ((d >> sh & 255) * k + 127) / 255;
        out |= (v > 255 ? 255 : v) << sh;
    }
    return out;
}
}

Img shrink(const Img& src, int T, int split, int g0, int g1)
{
    const uint32_t* s = pixels(src);
    if (!s || T <= 0 || src.h <= 0) return Img();
    int w = src.w, h = src.h;
    if (split > h) split = h;
    double f = (double)T / h;
    int W = (int)lround(w * f);
    if (W < 1) W = 1;
    std::vector<uint32_t> out((size_t)W * T, 0);
    // above the drop-down: the area average of each target pixel's source cell
    // (rows from split on left out: the cut fades as the cell's share)
    int U = (int)ceil(split * f);
    if (U > T) U = T;
    for (int y = 0; y < U; y++)
        for (int x = 0; x < W; x++) {
            double sy0 = y / f, sy1 = (y + 1) / f, sx0 = x / f, sx1 = (x + 1) / f, acc[4] = {};
            for (int j = (int)sy0; j < split && j < sy1; j++) {
                double wy = (sy1 < j + 1 ? sy1 : j + 1) - (sy0 > j ? sy0 : j);
                if (wy <= 0) continue;
                for (int i = (int)sx0; i < w && i < sx1; i++) {
                    double wx = (sx1 < i + 1 ? sx1 : i + 1) - (sx0 > i ? sx0 : i);
                    if (wx <= 0) continue;
                    uint32_t p = s[(size_t)j * w + i];
                    for (int c = 0; c < 4; c++) acc[c] += (p >> (24 - 8 * c) & 255) * wx * wy;
                }
            }
            double area = (sx1 - sx0) * (sy1 - sy0);
            uint32_t v = 0;
            for (int c = 0; c < 4; c++) {
                long q = lround(acc[c] / area);
                v |= (uint32_t)(q > 255 ? 255 : q < 0 ? 0 : q) << (24 - 8 * c);
            }
            out[(size_t)y * W + x] = v;
        }
    // the drop-down's rows 1:1 at the bottom, over the scaled plate
    int n = h - split, gw = g1 - g0 + 1;
    if (n > 0 && n <= T) {
        int gx = (int)lround((g0 + g1 + 1) / 2.0 * f) - gw / 2;     // the glyph's place
        for (int y = 0; y < n; y++) {
            const uint32_t* row = s + (size_t)(split + y) * w;
            for (int x = 0; x < W; x++) {
                uint32_t v;
                if (gw > 0 && x >= gx && x < gx + gw) {
                    v = row[g0 + (x - gx)];
                } else {
                    // across, the same box filter as the plate above (its side
                    // lines meet the box's), the glyph's columns left out
                    double sx0 = x / f, sx1 = (x + 1) / f, acc[4] = {}, wsum = 0;
                    for (int i = (int)sx0; i < w && i < sx1; i++) {
                        if (gw > 0 && i >= g0 && i <= g1) continue;
                        double wx = (sx1 < i + 1 ? sx1 : i + 1) - (sx0 > i ? sx0 : i);
                        if (wx <= 0) continue;
                        for (int c = 0; c < 4; c++) acc[c] += (row[i] >> (24 - 8 * c) & 255) * wx;
                        wsum += wx;
                    }
                    v = 0;
                    for (int c = 0; c < 4 && wsum > 0; c++) {
                        long q = lround(acc[c] / wsum);
                        v |= (uint32_t)(q > 255 ? 255 : q < 0 ? 0 : q) << (24 - 8 * c);
                    }
                }
                uint32_t& d = out[(size_t)(T - n + y) * W + x];
                d = over(d, v);
            }
        }
    }
    return dib(out.data(), W, T);
}

bool glyph_columns(const Img& hot, int split, int* g0, int* g1)
{
    const uint32_t* s = pixels(hot);
    if (!s) return false;
    int lo = hot.w, hi = -1;
    for (int y = split > 0 ? split : 0; y < hot.h; y++)
        for (int x = 0; x < hot.w; x++) {
            uint32_t p = s[(size_t)y * hot.w + x], a = p >> 24;
            if (a < 160) continue;
            uint32_t lum = ((p >> 16 & 255) * 77 + (p >> 8 & 255) * 151 + (p & 255) * 28) / a;   // unpremultiplied, x256/255
            if (lum >= 110) continue;
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }
    if (hi < lo || hi - lo > hot.w / 2) return false;
    *g0 = lo; *g1 = hi;
    return true;
}

int alpha_at(const Img& img, int x, int y, int state, int states)
{
    if (!img.bmp || states < 1) return 0;
    int h = img.h / states;
    if (x < 0 || y < 0 || x >= img.w || y >= h) return 0;
    DIBSECTION ds = {};
    if (GetObjectW(img.bmp, sizeof ds, &ds) != sizeof ds || !ds.dsBm.bmBits) return 0;
    return (int)(((const uint32_t*)ds.dsBm.bmBits)[(size_t)(state * h + y) * img.w + x] >> 24);
}

} // namespace navbar
