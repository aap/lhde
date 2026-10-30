// FreeType text rendering that reproduces GDI's choices. Every rule here is
// lifted from Wine's dlls/win32u/freetype.c (and font.c for ExtTextOut);
// function names in comments point at the Wine function that has the rule.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H
#include FT_OUTLINE_H
#include FT_DRIVER_H
#include FT_MODULE_H
#include <fontconfig/fontconfig.h>
#include "font.h"

namespace gdi {

enum { GASP_GRIDFIT = 1, GASP_DOGRAY = 2 };

struct Glyph {
    int16_t left, top;      // gmptGlyphOrigin: offset of the mask from the pen, y up
    int16_t w, h;           // gmBlackBox
    int16_t advance;        // gmCellIncX
    std::vector<uint8_t> mask;   // w*h, 0..255
};

struct Font {
    FT_Face face;
    int32_t ppem;
    TextMetrics tm;
    int32_t ul_pos, ul_size, so_pos, so_size;   // otmsUnderscore*/otmsStrikeout*, y up from baseline
    int32_t avg_width_units;                    // OS/2 xAvgCharWidth (ntmAvgWidth)
    bool underline, strikeout;
    bool fake_bold, fake_italic;
    bool gasp_mono;         // gasp table exists and says no gray at this ppem
    uint8_t quality;        // FontDesc::quality
    std::unordered_map<uint32_t, Glyph> cache;  // key: codepoint | mono << 31
};

static FT_Library library;
static int smoothing = SMOOTH_STANDARD;
static std::vector<std::string> font_dirs;
static FcConfig* app_config;     // only the add_font_dir directories

static bool init_freetype()
{
    if (library) return true;
    if (FT_Init_FreeType(&library)) return false;
    // The classic (pre-v40) TrueType interpreter: what Windows 2000/XP hinting
    // looks like. Wine only forces this for old FreeTypes; we want it always.
    FT_UInt v = TT_INTERPRETER_VERSION_35;
    FT_Property_Set(library, "truetype", "interpreter-version", &v);
    FcInit();
    return true;
}

void add_font_dir(const char* dir)
{
    if (!dir || !*dir) return;
    FcInit();
    if (!app_config) {
        // An empty config, so lookups in it only see our directories; give it
        // the usual user cache dir so it doesn't rescan (and complain) each run.
        app_config = FcConfigCreate();
        FcConfigParseAndLoadFromMemory(app_config, (const FcChar8*)
            "<fontconfig><cachedir prefix=\"xdg\">fontconfig</cachedir></fontconfig>", FcFalse);
    }
    struct stat st;   // a directory, or one font file (luxwm's Marlett without the rest of its directory)
    bool file = stat(dir, &st) == 0 && S_ISREG(st.st_mode);
    if (file ? FcConfigAppFontAddFile(app_config, (const FcChar8*)dir) : FcConfigAppFontAddDir(app_config, (const FcChar8*)dir))
        font_dirs.push_back(dir);
}

void set_font_smoothing(int mode) { smoothing = mode < SMOOTH_OFF || mode > SMOOTH_CLEARTYPE ? SMOOTH_STANDARD : mode; }
int font_smoothing() { return smoothing; }

// ---------------------------------------------------------------------------
// Face lookup

static const char* alias(const char* family)
{
    if (!family || !*family) return "Tahoma";
    if (!strcasecmp(family, "MS Sans Serif")) return "Microsoft Sans Serif";
    if (!strcasecmp(family, "MS Shell Dlg") || !strcasecmp(family, "MS Shell Dlg 2")) return "Tahoma";
    return family;
}

static bool family_matches(FcPattern* p, const char* family)
{
    FcChar8* s;
    for (int i = 0; FcPatternGetString(p, FC_FAMILY, i, &s) == FcResultMatch; i++)
        if (!strcasecmp((const char*)s, family)) return true;
    return false;
}

// Returns true and fills file/index. The add_font_dir directories win over
// the system for the same family, so a real XP tahoma.ttf beats whatever
// Tahoma the system has; otherwise plain fontconfig matching (with fallback).
static bool find_face(const char* family, int32_t weight, bool italic, std::string& file, int& index)
{
    FcPattern* pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family);
    FcPatternAddInteger(pat, FC_WEIGHT, FcWeightFromOpenType(weight));
    FcPatternAddInteger(pat, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);

    bool ok = false;
    FcConfig* configs[2] = { app_config, nullptr };
    for (int c = 0; c < 2 && !ok; c++) {
        if (c == 0 && !app_config) continue;
        FcPattern* p = FcPatternDuplicate(pat);
        FcConfigSubstitute(configs[c], p, FcMatchPattern);
        FcDefaultSubstitute(p);
        FcResult res;
        FcPattern* m = FcFontMatch(configs[c], p, &res);
        FcChar8* f;
        if (m && (c == 1 || family_matches(m, family)) &&
            FcPatternGetString(m, FC_FILE, 0, &f) == FcResultMatch) {
            file = (const char*)f;
            if (FcPatternGetInteger(m, FC_INDEX, 0, &index) != FcResultMatch) index = 0;
            ok = true;
        }
        if (m) FcPatternDestroy(m);
        FcPatternDestroy(p);
    }
    FcPatternDestroy(pat);
    return ok;
}

// ---------------------------------------------------------------------------
// Size selection and metrics

static inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

static bool load_table(FT_Face face, FT_ULong tag, std::vector<uint8_t>& buf)
{
    FT_ULong len = 0;
    if (FT_Load_Sfnt_Table(face, tag, 0, nullptr, &len) || !len) return false;
    buf.resize(len);
    return !FT_Load_Sfnt_Table(face, tag, 0, buf.data(), &len);
}

// load_VDMX: the device metrics table picks ppem and ascent/descent. Ratio
// records with bCharSet 0 are skipped, as Wine does (so Tahoma has none).
static int32_t load_vdmx(FT_Face face, int32_t height, int32_t& ymax, int32_t& ymin)
{
    std::vector<uint8_t> t;
    ymax = ymin = 0;
    if (!load_table(face, FT_MAKE_TAG('V', 'D', 'M', 'X'), t) || t.size() < 6) return 0;
    uint32_t num_recs = be16(&t[2]), num_ratios = be16(&t[4]);
    (void)num_recs;
    int64_t offset = -1;
    for (uint32_t i = 0; i < num_ratios; i++) {
        size_t o = 6 + i * 4;
        if (o + 4 > t.size()) return 0;
        uint8_t charset = t[o], xr = t[o + 1], ys = t[o + 2], ye = t[o + 3];
        if (!charset) continue;
        if ((xr == 0 && ys == 0 && ye == 0) || (xr == 1 && ys <= 1 && ye >= 1)) {
            size_t go = 6 + num_ratios * 4 + i * 2;
            if (go + 2 > t.size()) return 0;
            offset = be16(&t[go]);
            break;
        }
    }
    if (offset < 0 || (size_t)offset + 4 > t.size()) return 0;
    uint32_t recs = be16(&t[offset]);
    uint8_t startsz = t[offset + 2], endsz = t[offset + 3];
    const uint8_t* v = &t[offset + 4];
    if ((size_t)offset + 4 + recs * 6 > t.size()) return 0;
    auto pel = [&](uint32_t i) { return (int32_t)be16(v + i * 6); };
    auto vmax = [&](uint32_t i) { return (int32_t)(int16_t)be16(v + i * 6 + 2); };
    auto vmin = [&](uint32_t i) { return (int32_t)(int16_t)be16(v + i * 6 + 4); };

    int32_t ppem = 0;
    if (height > 0) {
        for (uint32_t i = 0; i < recs; i++) {
            ppem = pel(i);
            if (vmax(i) - vmin(i) == height) { ymax = vmax(i); ymin = vmin(i); break; }
            if (vmax(i) - vmin(i) > height) {
                if (i == 0) return 0;
                ymax = vmax(i - 1); ymin = vmin(i - 1); ppem = pel(i - 1);
                break;
            }
        }
        if (!ymax) ppem = 0;
    } else {
        ppem = -height;
        if (ppem < startsz || ppem > endsz) return 0;
        for (uint32_t i = 0; i < recs; i++) {
            if (pel(i) > ppem) return 0;
            if (pel(i) == ppem) { ymax = vmax(i); ymin = vmin(i); break; }
        }
    }
    return ppem;
}

// calc_ppem_for_height
static int32_t calc_ppem(FT_Face face, int32_t height)
{
    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    TT_HoriHeader* hhea = (TT_HoriHeader*)FT_Get_Sfnt_Table(face, FT_SFNT_HHEA);
    const int32_t max_ppem = (1 << 16) - 1;
    int32_t ppem;
    if (height > 0) {
        int32_t units = 0;
        if (os2) units = os2->usWinAscent + abs((int16_t)os2->usWinDescent);
        if (!units && hhea) units = hhea->Ascender - hhea->Descender;
        if (!units) units = face->units_per_EM;
        ppem = FT_MulDiv(face->units_per_EM, height, units);
        if (ppem > 1 && FT_MulDiv(units, ppem, face->units_per_EM) > height) ppem--;
        if (ppem > max_ppem) ppem = 1;
    } else if (height >= -max_ppem) {
        ppem = -height;
    } else {
        ppem = 1;
    }
    return ppem < 1 ? 1 : ppem;
}

// get_gasp_flags: flags of the first range whose max ppem >= ours.
static bool gasp_flags(FT_Face face, int32_t ppem, uint16_t& flags)
{
    std::vector<uint8_t> t;
    flags = 0;
    if (!load_table(face, FT_MAKE_TAG('g', 'a', 's', 'p'), t) || t.size() < 8) return false;
    uint32_t version = be16(&t[0]), n = be16(&t[2]);
    if (version > 1 || t.size() < (n * 2 + 2) * 2) return false;
    for (uint32_t i = 0; i < n; i++) {
        flags = be16(&t[4 + i * 4 + 2]);
        if (ppem <= be16(&t[4 + i * 4])) break;
    }
    return true;
}

// freetype_set_outline_text_metrics
static bool set_metrics(Font* f, int32_t vdmx_ymax, int32_t vdmx_ymin)
{
    FT_Face face = f->face;
    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    TT_HoriHeader* hhea = (TT_HoriHeader*)FT_Get_Sfnt_Table(face, FT_SFNT_HHEA);
    TT_Postscript* post = (TT_Postscript*)FT_Get_Sfnt_Table(face, FT_SFNT_POST);
    if (!os2 || !hhea) return false;
    FT_Fixed em_scale = FT_MulDiv(f->ppem, 1 << 16, face->units_per_EM);
    auto scale = [&](FT_Long v) { return (int32_t)FT_MulFix(v, em_scale); };

    int32_t windescent = abs((int16_t)os2->usWinDescent);
    int32_t ascent, descent;
    if (os2->usWinAscent + windescent == 0) {
        ascent = hhea->Ascender;
        descent = -hhea->Descender;
    } else {
        ascent = os2->usWinAscent;
        descent = windescent;
    }
    f->avg_width_units = os2->xAvgCharWidth;

    TextMetrics& tm = f->tm;
    if (vdmx_ymax) {
        tm.ascent = vdmx_ymax;
        tm.descent = -vdmx_ymin;
        tm.internal_leading = tm.ascent + tm.descent - (int32_t)face->size->metrics.y_ppem;
    } else {
        tm.ascent = scale(ascent);
        tm.descent = scale(descent);
        tm.internal_leading = scale(ascent + descent - face->units_per_EM);
    }
    tm.height = tm.ascent + tm.descent;
    int32_t el = scale(hhea->Line_Gap - ((ascent + descent) - (hhea->Ascender - hhea->Descender)));
    tm.external_leading = el > 0 ? el : 0;
    tm.ave_char_width = scale(os2->xAvgCharWidth);
    if (tm.ave_char_width == 0) tm.ave_char_width = 1;
    tm.max_char_width = scale(face->bbox.xMax - face->bbox.xMin);
    // TMPF_FIXED_PITCH logic, un-inverted
    tm.fixed_pitch = FT_IS_FIXED_WIDTH(face) ||
                     (os2->version != 0xffff && os2->panose[3] == 9 /* PAN_PROP_MONOSPACED */);

    f->so_size = scale(os2->yStrikeoutSize);
    f->so_pos = scale(os2->yStrikeoutPosition);
    if (post) {
        f->ul_size = scale(post->underlineThickness);
        f->ul_pos = scale(post->underlinePosition);
    } else {
        f->ul_size = f->ul_pos = 0;
    }
    return true;
}

Font* create_font(const FontDesc& d)
{
    if (!init_freetype()) return nullptr;
    const char* family = alias(d.family);
    int32_t weight = d.weight ? d.weight : 400;
    std::string file;
    int index = 0;
    if (!find_face(family, weight, d.italic, file, index)) return nullptr;

    FT_Face face;
    if (FT_New_Face(library, file.c_str(), index, &face)) return nullptr;
    if (!FT_IS_SCALABLE(face)) { FT_Done_Face(face); return nullptr; }
    // Unicode, else the symbol cmap (Marlett has only that; FreeType selects none for it)
    if (!face->charmap && FT_Select_Charmap(face, FT_ENCODING_UNICODE)) FT_Select_Charmap(face, FT_ENCODING_MS_SYMBOL);

    Font* f = new Font;
    f->face = face;
    f->underline = d.underline;
    f->strikeout = d.strikeout;
    f->quality = d.quality;

    // font.c create_gdi_font: synthesize what the face lacks
    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    int32_t face_weight = os2 ? os2->usWeightClass : (face->style_flags & FT_STYLE_FLAG_BOLD ? 700 : 400);
    f->fake_bold = weight > 550 && !(face->style_flags & FT_STYLE_FLAG_BOLD) && face_weight < 550;
    f->fake_italic = d.italic && !(face->style_flags & FT_STYLE_FLAG_ITALIC);

    // load_face. lfHeight 0 is a 12px cell here (Wine uses 16).
    int32_t height = d.height ? d.height : 12;
    int32_t ymax, ymin;
    f->ppem = load_vdmx(face, height, ymax, ymin);
    if (!f->ppem) { f->ppem = calc_ppem(face, height); ymax = ymin = 0; }
    FT_Set_Pixel_Sizes(face, 0, f->ppem);

    if (!set_metrics(f, ymax, ymin)) {
        // not a TrueType face: freetype_set_bitmap_text_metrics fallback
        TextMetrics& tm = f->tm;
        tm.ascent = face->size->metrics.ascender >> 6;
        tm.descent = -face->size->metrics.descender >> 6;
        tm.height = tm.ascent + tm.descent;
        tm.internal_leading = tm.height - face->size->metrics.y_ppem;
        tm.external_leading = (face->size->metrics.height >> 6) - tm.height;
        tm.max_char_width = face->size->metrics.max_advance >> 6;
        tm.ave_char_width = tm.max_char_width * 2 / 3;
        tm.fixed_pitch = FT_IS_FIXED_WIDTH(face);
        f->avg_width_units = 0;
        f->ul_pos = 0; f->ul_size = tm.ascent / 20 + 1;
        f->so_pos = tm.ascent / 2; f->so_size = f->ul_size;
    }

    // freetype_get_aa_flags: with antialias_fakes (Wine's default) synthesized
    // faces stay gray; otherwise the gasp table may veto gray.
    uint16_t flags;
    f->gasp_mono = !f->fake_bold && !f->fake_italic &&
                   gasp_flags(face, f->ppem, flags) && !(flags & GASP_DOGRAY);
    return f;
}

Font* create_font(const char* family, int32_t height, int32_t weight, bool italic)
{
    FontDesc d{family, height, weight, italic, false, false};
    return create_font(d);
}

void destroy_font(Font* f)
{
    if (!f) return;
    FT_Done_Face(f->face);
    delete f;
}

TextMetrics metrics(const Font* f) { return f->tm; }

// ---------------------------------------------------------------------------
// Glyphs

static const FT_Matrix slant = { 1 << 16, (1 << 16) >> 2, 0, 1 << 16 };

// freetype_get_glyph_outline + get_mono/antialias_glyph_bitmap, no transform
// other than the fake-italic slant.
static bool render_glyph(Font* f, FT_UInt index, bool mono, Glyph& g)
{
    FT_Face face = f->face;
    FT_Int32 load = FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH | (mono ? FT_LOAD_TARGET_MONO : FT_LOAD_TARGET_NORMAL);
    if (!mono || f->fake_italic) load |= FT_LOAD_NO_BITMAP;
    FT_Error err = FT_Load_Glyph(face, index, load | FT_LOAD_PEDANTIC);
    if (err) err = FT_Load_Glyph(face, index, load | FT_LOAD_NO_HINTING);
    if (err) return false;

    FT_GlyphSlot slot = face->glyph;
    FT_Glyph_Metrics m = slot->metrics;
    if (f->fake_bold) {   // get_bold_glyph_outline
        bool done = false;
        if (slot->format == FT_GLYPH_FORMAT_OUTLINE &&
            !FT_Outline_Embolden(&slot->outline, FT_MulDiv(f->ppem, 1 << 6, 24))) {
            FT_BBox cb;
            FT_Outline_Get_CBox(&slot->outline, &cb);
            m.width = cb.xMax - cb.xMin;
            m.height = cb.yMax - cb.yMin;
            m.horiBearingX = cb.xMin;
            m.horiBearingY = cb.yMax;
            done = true;
        }
        if (!done && m.width) m.width += 1 << 6;
    }

    // Clip the glyph box to the text metrics' ascent/descent.
    FT_Pos top = m.horiBearingY < (FT_Pos)f->tm.ascent << 6 ? m.horiBearingY : (FT_Pos)f->tm.ascent << 6;
    FT_Pos bot = m.horiBearingY - m.height;
    if (bot < -((FT_Pos)f->tm.descent << 6)) bot = -((FT_Pos)f->tm.descent << 6);
    if (top < bot) { FT_Pos t = top; top = bot; bot = t; }
    m.horiBearingY = top;
    m.height = top - bot;

    // get_transformed_bbox
    FT_BBox bb;
    if (!f->fake_italic) {
        bb.xMin = m.horiBearingX & -64;
        bb.xMax = (m.horiBearingX + m.width + 63) & -64;
        bb.yMax = (m.horiBearingY + 63) & -64;
        bb.yMin = (m.horiBearingY - m.height) & -64;
    } else {
        for (int i = 0; i < 4; i++) {
            FT_Vector v = { m.horiBearingX + (i & 1) * m.width, m.horiBearingY - (i >> 1) * m.height };
            FT_Vector_Transform(&v, &slant);
            if (i == 0) { bb.xMin = bb.xMax = v.x; bb.yMin = bb.yMax = v.y; continue; }
            if (v.x < bb.xMin) bb.xMin = v.x; else if (v.x > bb.xMax) bb.xMax = v.x;
            if (v.y < bb.yMin) bb.yMin = v.y; else if (v.y > bb.yMax) bb.yMax = v.y;
        }
        bb.xMin &= -64; bb.xMax = (bb.xMax + 63) & -64;
        bb.yMin &= -64; bb.yMax = (bb.yMax + 63) & -64;
    }

    // get_advance_metric (the slant leaves a horizontal advance unchanged)
    FT_Pos adv = m.horiAdvance;
    if (f->tm.fixed_pitch && f->avg_width_units) {
        FT_Fixed em_scale = FT_MulDiv(f->ppem, 1 << 16, face->units_per_EM);
        FT_Pos avg = FT_MulFix(f->avg_width_units, em_scale);
        if (avg > 0 && (adv + 63) >> 6 == FT_MulFix(f->avg_width_units * 2, em_scale))
            adv = (avg * 2) << 6;
    }
    if (f->fake_bold) adv += 1 << 6;
    adv = (adv + 63) & -64;

    // compute_metrics
    g.advance = (int16_t)(adv >> 6);
    g.left = (int16_t)(bb.xMin >> 6);
    g.top = (int16_t)(bb.yMax >> 6);
    g.w = (int16_t)((bb.xMax - bb.xMin) >> 6);
    g.h = (int16_t)((bb.yMax - bb.yMin) >> 6);
    g.mask.clear();
    if (g.w <= 0 || g.h <= 0) { g.w = g.h = 0; return true; }
    g.mask.assign((size_t)g.w * g.h, 0);

    if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        if (f->fake_italic) FT_Outline_Transform(&slot->outline, &slant);
        FT_Outline_Translate(&slot->outline, -bb.xMin, -bb.yMin);
        FT_Bitmap bm;
        memset(&bm, 0, sizeof bm);
        bm.width = g.w;
        bm.rows = g.h;
        if (mono) {
            int32_t pitch = ((g.w + 31) >> 5) << 2;
            std::vector<uint8_t> bits((size_t)pitch * g.h, 0);
            bm.pitch = pitch;
            bm.pixel_mode = FT_PIXEL_MODE_MONO;
            bm.buffer = bits.data();
            FT_Outline_Get_Bitmap(library, &slot->outline, &bm);
            for (int32_t y = 0; y < g.h; y++)
                for (int32_t x = 0; x < g.w; x++)
                    if (bits[y * pitch + x / 8] & (0x80 >> (x % 8))) g.mask[y * g.w + x] = 255;
        } else {
            bm.pitch = g.w;
            bm.pixel_mode = FT_PIXEL_MODE_GRAY;
            bm.num_grays = 256;
            bm.buffer = g.mask.data();
            FT_Outline_Get_Bitmap(library, &slot->outline, &bm);
            // GGO_GRAY4_BITMAP: 17 levels, which is what Wine's dibdrv blends
            for (uint8_t& c : g.mask) {
                int lv = c * 17 / 256;
                c = (uint8_t)(lv * 255 / 16);
            }
        }
    } else if (slot->format == FT_GLYPH_FORMAT_BITMAP) {
        // embedded bitmap (mono load only)
        const FT_Bitmap& src = slot->bitmap;
        int32_t w = (int32_t)src.width < g.w ? (int32_t)src.width : g.w;
        int32_t h = (int32_t)src.rows < g.h ? (int32_t)src.rows : g.h;
        for (int32_t y = 0; y < h; y++) {
            const uint8_t* s = src.buffer + y * src.pitch;
            for (int32_t x = 0; x < w; x++) {
                bool on = src.pixel_mode == FT_PIXEL_MODE_MONO ? (s[x / 8] & (0x80 >> (x % 8))) : s[x] >= 128;
                if (on) {
                    g.mask[y * g.w + x] = 255;
                    if (f->fake_bold && x + 1 < g.w) g.mask[y * g.w + x + 1] = 255;
                }
            }
        }
    } else {
        g.w = g.h = 0;
        g.mask.clear();
    }
    return true;
}

// get_glyph_index: a symbol font (Marlett, Wingdings: only a (3,0) cmap) has its characters at U+F000 + the byte, as GDI
// maps them; try that first and the code itself after (fonts with a broken symbol cmap)
static FT_UInt glyph_index(FT_Face face, uint32_t cp)
{
    if (face->charmap && face->charmap->encoding == FT_ENCODING_MS_SYMBOL && cp < 0x100) {
        if (FT_UInt i = FT_Get_Char_Index(face, cp + 0xf000)) return i;
    }
    return FT_Get_Char_Index(face, cp);
}

static const Glyph* get_glyph(const Font* cf, uint32_t cp)
{
    Font* f = (Font*)cf;   // the cache is not part of the font's logical state
    bool mono = f->quality == 2 ? true : f->quality == 1 ? false : !smoothing || f->gasp_mono;
    uint32_t key = (cp & 0x7fffffff) | (mono ? 0x80000000u : 0);
    auto it = f->cache.find(key);
    if (it != f->cache.end()) return &it->second;

    Glyph g{};
    // cache_glyph_bitmap: the glyph, else .notdef, else space
    FT_UInt indices[3] = { glyph_index(f->face, cp), 0, glyph_index(f->face, ' ') };
    bool ok = false;
    for (int i = 0; i < 3 && !ok; i++) ok = render_glyph(f, indices[i], mono, g);
    if (!ok) g = Glyph{};
    return &(f->cache[key] = std::move(g));
}

// UTF-8 decode; malformed bytes come out as U+FFFD.
static uint32_t next_cp(const uint8_t*& p, const uint8_t* end)
{
    uint32_t c = *p++;
    if (c < 0x80) return c;
    int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : -1;
    if (n < 0 || c >= 0xf8) return 0xfffd;
    c &= 0x3f >> n;
    for (int i = 0; i < n; i++) {
        if (p >= end || (*p & 0xc0) != 0x80) return 0xfffd;
        c = c << 6 | (*p++ & 0x3f);
    }
    return c;
}

template <class F>
static void for_each_cp(const char* s, int32_t len, F fn)
{
    if (!s) return;
    if (len < 0) len = (int32_t)strlen(s);
    const uint8_t* p = (const uint8_t*)s;
    const uint8_t* end = p + len;
    while (p < end) fn(next_cp(p, end));
}

Size text_extent(const Font* f, const char* s, int32_t len)
{
    int32_t cx = 0;
    for_each_cp(s, len, [&](uint32_t cp) { cx += get_glyph(f, cp)->advance; });
    return Size{cx, f->tm.height};
}

int32_t char_width(const Font* f, uint32_t cp) { return get_glyph(f, cp)->advance; }

// ---------------------------------------------------------------------------
// Drawing

static void draw_text(DC* dc, Point pos, const Font* f, const char* s, int32_t len, Color fg, bool opaque, const Rect* fill, Color bg)
{
    int32_t baseline = pos.y + f->tm.ascent;
    int32_t x = pos.x;
    std::vector<const Glyph*> glyphs;
    for_each_cp(s, len, [&](uint32_t cp) { glyphs.push_back(get_glyph(f, cp)); });
    int32_t width = 0;
    for (const Glyph* g : glyphs) width += g->advance;

    if (opaque && fill) fill_rect(dc, *fill, bg);
    else if (opaque) fill_rect(dc, Rect{pos.x, pos.y, pos.x + width, pos.y + f->tm.height}, bg);

    for (const Glyph* g : glyphs) {
        if (g->w) blend_mask(dc, Point{x + g->left, baseline - g->top}, g->mask.data(), g->w, g->h, g->w, fg);
        x += g->advance;
    }

    // NtGdiExtTextOutW: underline/strikeout as a filled band of `size` rows
    // centred on position (y up from the baseline).
    auto band = [&](int32_t p, int32_t sz) {
        int32_t w = abs(sz) ? abs(sz) : 1;
        int32_t top = baseline - (p + w / 2);
        fill_rect(dc, Rect{pos.x, top, pos.x + width, top + w}, fg);
    };
    if (f->underline) band(f->ul_pos, f->ul_size);
    if (f->strikeout) band(f->so_pos, f->so_size);
}

void text_out(DC* dc, Point pos, const Font* f, const char* s, int32_t len, Color fg)
{
    if (!f) return;
    draw_text(dc, pos, f, s, len, fg, false, nullptr, fg);
}

void text_out(DC* dc, Point pos, const Font* f, const char* s, int32_t len, Color fg, Color bg, const Rect* opaque)
{
    if (!f) return;
    draw_text(dc, pos, f, s, len, fg, true, opaque, bg);
}

void glow_text(DC* dc, Point pos, const Font* f, const char* s, int32_t len, Color glow, int32_t radius)
{
    if (!f) return;
    std::vector<const Glyph*> glyphs;
    for_each_cp(s, len, [&](uint32_t cp) { glyphs.push_back(get_glyph(f, cp)); });
    // bounds of all glyph masks, relative to pos
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = f->tm.height, x = 0;
    for (const Glyph* g : glyphs) {
        if (g->w) {
            int32_t gx = x + g->left, gy = f->tm.ascent - g->top;
            if (gx < x0) x0 = gx;
            if (gy < y0) y0 = gy;
            if (gx + g->w > x1) x1 = gx + g->w;
            if (gy + g->h > y1) y1 = gy + g->h;
        }
        x += g->advance;
    }
    int32_t w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    std::vector<uint8_t> mask((size_t)w * h, 0);
    x = 0;
    for (const Glyph* g : glyphs) {
        int32_t gx = x + g->left - x0, gy = f->tm.ascent - g->top - y0;
        for (int32_t yy = 0; yy < g->h; yy++)
            for (int32_t xx = 0; xx < g->w; xx++) {
                uint8_t c = g->mask[(size_t)yy * g->w + xx];
                uint8_t& t = mask[(size_t)(gy + yy) * w + gx + xx];
                if (t < c) t = c;
            }
        x += g->advance;
    }
    glow_mask(dc, Point{pos.x + x0, pos.y + y0}, mask.data(), w, h, w, glow, radius);
}

} // namespace gdi
