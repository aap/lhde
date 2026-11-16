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
#include <algorithm>
#include <ctype.h>
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
    bool lcd;               // ClearType: mask is w*h*3 (R, G, B subpixel coverages)
    std::vector<uint8_t> mask;   // w*h, 0..255 (lcd: w*h*3)
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
    std::unordered_map<uint64_t, Glyph> cache;  // key: codepoint | mode << 32 (0 grey, 1 mono, 2 ClearType)
    // font linking: the faces that draw what this one lacks (created at this font's lfHeight, weight and style)
    int32_t height, weight;                     // the LOGFONT's lfHeight (0 as 12) and weight
    bool italic;
    std::string file;                           // the face's file and index (a child is never its parent's face)
    int index;
    bool symbol;                                // a symbol font (the symbol cmap, or the OS/2 symbol code page): never linked
    bool linked;                                // the link table's children were made
    std::vector<Font*> links;                   // the link table's faces for this family, in its order
    std::vector<Font*> picked;                  // faces fontconfig chose for earlier characters
    std::unordered_map<uint32_t, Font*> fallback;   // code point -> the face that draws it (nullptr: none)
};

static FT_Library library;
static int smoothing = SMOOTH_STANDARD;
static std::vector<std::string> font_dirs;
static FcConfig* app_config;     // only the add_font_dir directories
static FcConfig* fallback_config;  // only the add_font_dir(dir, true) ones, asked after app_config
static FcConfig* theme_config;   // only the set_theme_font_dir directory

static void sorted_faces_reset();

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

void add_font_dir(const char* dir, bool fallback)
{
    if (!dir || !*dir) return;
    FcInit();
    FcConfig*& cfg = fallback ? fallback_config : app_config;
    if (!cfg) {
        // An empty config, so lookups in it only see our directories; give it
        // the usual user cache dir so it doesn't rescan (and complain) each run.
        cfg = FcConfigCreate();
        FcConfigParseAndLoadFromMemory(cfg, (const FcChar8*)
            "<fontconfig><cachedir prefix=\"xdg\">fontconfig</cachedir></fontconfig>", FcFalse);
    }
    struct stat st;   // a directory, or one font file (luxwm's Marlett without the rest of its directory)
    bool file = stat(dir, &st) == 0 && S_ISREG(st.st_mode);
    if (file ? FcConfigAppFontAddFile(cfg, (const FcChar8*)dir) : FcConfigAppFontAddDir(cfg, (const FcChar8*)dir))
        font_dirs.push_back(dir);
    sorted_faces_reset();
}

// fontconfig breaks ties between same-family faces by font version, not by
// directory, so with every build's fonts registered 5112's Segoe UI
// (segoeui.ttf) would beat 4074's own (seguibk.ttf) under Jade 4074. The
// theme's directory is therefore a config of its own that is asked first.
void set_theme_font_dir(const char* dir)
{
    sorted_faces_reset();
    if (theme_config) { FcConfigDestroy(theme_config); theme_config = nullptr; }
    if (!dir || !*dir) return;
    FcInit();
    theme_config = FcConfigCreate();
    FcConfigParseAndLoadFromMemory(theme_config, (const FcChar8*)
        "<fontconfig><cachedir prefix=\"xdg\">fontconfig</cachedir></fontconfig>", FcFalse);
    struct stat st;
    bool file = stat(dir, &st) == 0 && S_ISREG(st.st_mode);
    if (!(file ? FcConfigAppFontAddFile(theme_config, (const FcChar8*)dir) : FcConfigAppFontAddDir(theme_config, (const FcChar8*)dir))) {
        FcConfigDestroy(theme_config); theme_config = nullptr;
    }
}

void set_font_smoothing(int mode) { smoothing = mode < SMOOTH_OFF || mode > SMOOTH_CLEARTYPE ? SMOOTH_STANDARD : mode; }
int font_smoothing() { return smoothing; }

// ---------------------------------------------------------------------------
// Face lookup

static std::string shell_dlg = "Tahoma";
void set_shell_dlg_face(const char* family) { shell_dlg = family && *family ? family : "Tahoma"; }
const char* shell_dlg_face() { return shell_dlg.c_str(); }

static const char* alias(const char* family)
{
    if (!family || !*family) return "Tahoma";
    if (!strcasecmp(family, "MS Sans Serif")) return "Microsoft Sans Serif";
    if (!strcasecmp(family, "MS Shell Dlg") || !strcasecmp(family, "MS Shell Dlg 2")) return shell_dlg.c_str();
    return family;
}

static bool family_matches(FcPattern* p, const char* family)
{
    FcChar8* s;
    for (int i = 0; FcPatternGetString(p, FC_FAMILY, i, &s) == FcResultMatch; i++)
        if (!strcasecmp((const char*)s, family)) return true;
    return false;
}

bool have_family(const char* family)
{
    if (!family || !*family || !init_freetype()) return false;
    FcPattern* pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    bool ok = false;
    FcConfig* configs[4] = { theme_config, app_config, fallback_config, nullptr };
    for (int c = 0; c < 4 && !ok; c++) {
        if (c < 3 && !configs[c]) continue;
        FcPattern* p = FcPatternDuplicate(pat);
        FcConfigSubstitute(configs[c], p, FcMatchPattern);
        FcDefaultSubstitute(p);
        FcResult res;
        FcPattern* m = FcFontMatch(configs[c], p, &res);
        ok = m && family_matches(m, family);
        if (m) FcPatternDestroy(m);
        FcPatternDestroy(p);
    }
    FcPatternDestroy(pat);
    return ok;
}

std::vector<FaceInfo> list_faces()
{
    std::vector<FaceInfo> out;
    if (!init_freetype()) return out;
    std::vector<std::string> seen_in_earlier;   // families of the sources already listed (lower case)
    FcConfig* configs[4] = { theme_config, app_config, fallback_config, nullptr };
    for (int c = 0; c < 4; c++) {
        if (c < 3 && !configs[c]) continue;
        FcPattern* pat = FcPatternCreate();
        FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
        FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_WEIGHT, FC_SLANT, FC_SPACING, (char*)nullptr);
        FcFontSet* fs = FcFontList(c < 3 ? configs[c] : FcConfigGetCurrent(), pat, os);
        std::vector<std::string> here;
        for (int i = 0; fs && i < fs->nfont; i++) {
            FcPattern* p = fs->fonts[i];
            FcChar8* fam = nullptr;
            if (FcPatternGetString(p, FC_FAMILY, 0, &fam) != FcResultMatch || !fam || !*fam || *fam == '.') continue;
            std::string lower = (const char*)fam;
            for (char& ch : lower) ch = (char)tolower((unsigned char)ch);
            bool earlier = false;
            for (const std::string& e : seen_in_earlier) if (e == lower) { earlier = true; break; }
            if (earlier) continue;
            here.push_back(lower);
            FaceInfo f;
            f.family = (const char*)fam;
            FcChar8* st = nullptr;
            f.style = FcPatternGetString(p, FC_STYLE, 0, &st) == FcResultMatch && st ? (const char*)st : "Regular";
            int w = FC_WEIGHT_REGULAR, sl = FC_SLANT_ROMAN, sp = FC_PROPORTIONAL;
            FcPatternGetInteger(p, FC_WEIGHT, 0, &w);
            FcPatternGetInteger(p, FC_SLANT, 0, &sl);
            FcPatternGetInteger(p, FC_SPACING, 0, &sp);
            f.weight = FcWeightToOpenType(w);
            f.italic = sl != FC_SLANT_ROMAN;
            f.fixed_pitch = sp == FC_MONO || sp == FC_CHARCELL;
            bool dup = false;
            for (const FaceInfo& o : out)
                if (!strcasecmp(o.family.c_str(), f.family.c_str()) && o.weight == f.weight && o.italic == f.italic) { dup = true; break; }
            if (!dup) out.push_back(f);
        }
        if (fs) FcFontSetDestroy(fs);
        FcObjectSetDestroy(os);
        FcPatternDestroy(pat);
        seen_in_earlier.insert(seen_in_earlier.end(), here.begin(), here.end());
    }
    std::stable_sort(out.begin(), out.end(), [](const FaceInfo& a, const FaceInfo& b) {
        int k = strcasecmp(a.family.c_str(), b.family.c_str());
        if (k) return k < 0;
        if (a.weight != b.weight) return a.weight < b.weight;
        return !a.italic && b.italic;
    });
    return out;
}

// Returns true and fills file/index. The add_font_dir directories win over
// the system for the same family, so a real XP tahoma.ttf beats whatever
// Tahoma the system has; otherwise plain fontconfig matching (with fallback).
static bool find_face(const char* family, int32_t weight, bool italic, std::string& file, int& index, bool exact = false)
{
    FcPattern* pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family);
    FcPatternAddInteger(pat, FC_WEIGHT, FcWeightFromOpenType(weight));
    FcPatternAddInteger(pat, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);

    bool ok = false;
    // the theme's directory, the add_font_dir ones, the fallback ones, then the system (only it may fall back to another family)
    FcConfig* configs[4] = { theme_config, app_config, fallback_config, nullptr };
    for (int c = 0; c < 4 && !ok; c++) {
        if (c < 3 && !configs[c]) continue;
        FcPattern* p = FcPatternDuplicate(pat);
        FcConfigSubstitute(configs[c], p, FcMatchPattern);
        FcDefaultSubstitute(p);
        FcResult res;
        FcPattern* m = FcFontMatch(configs[c], p, &res);
        FcChar8* f;
        if (m && ((c == 3 && !exact) || family_matches(m, family)) &&
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

bool face_file(const char* family, int32_t weight, bool italic, std::string* file, int* index)
{
    if (!family || !*family || !init_freetype()) return false;
    std::string f;
    int i = 0;
    if (!find_face(alias(family), weight ? weight : 400, italic, f, i, true)) return false;
    if (file) *file = f;
    if (index) *index = i;
    return true;
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

static Font* open_font(const std::string& file, int index, const FontDesc& d)
{
    int32_t weight = d.weight ? d.weight : 400;
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
    f->height = d.height ? d.height : 12;
    f->weight = weight;
    f->italic = d.italic;
    f->file = file;
    f->index = index;
    f->linked = false;

    // font.c create_gdi_font: synthesize what the face lacks
    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    f->symbol = (face->charmap && face->charmap->encoding == FT_ENCODING_MS_SYMBOL) ||
                (os2 && os2->version != 0xffff && (os2->ulCodePageRange1 & 0x80000000u));
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

Font* create_font(const FontDesc& d)
{
    if (!init_freetype()) return nullptr;
    const char* family = alias(d.family);
    int32_t weight = d.weight ? d.weight : 400;
    std::string file;
    int index = 0;
    if (!find_face(family, weight, d.italic, file, index)) return nullptr;
    return open_font(file, index, d);
}

Font* create_font(const char* family, int32_t height, int32_t weight, bool italic)
{
    FontDesc d{family, height, weight, italic, false, false};
    return create_font(d);
}

void destroy_font(Font* f)
{
    if (!f) return;
    for (Font* c : f->links) destroy_font(c);
    for (Font* c : f->picked) destroy_font(c);
    FT_Done_Face(f->face);
    delete f;
}

TextMetrics metrics(const Font* f) { return f->tm; }

// ---------------------------------------------------------------------------
// Glyphs

static const FT_Matrix slant = { 1 << 16, (1 << 16) >> 2, 0, 1 << 16 };

// freetype_get_glyph_outline + get_mono/antialias_glyph_bitmap, no transform
// other than the fake-italic slant. lcd: ClearType, the outline (hinted as for
// grey, so the advances stay GDI's) rendered at three times the width and
// filtered across the subpixels, one pixel wider on each side.
static bool render_glyph(Font* f, FT_UInt index, bool mono, Glyph& g, bool lcd, const TextMetrics& clip)
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

    // Clip the glyph box to the text metrics' ascent/descent (a linked face's glyph to its base font's).
    FT_Pos top = m.horiBearingY < (FT_Pos)clip.ascent << 6 ? m.horiBearingY : (FT_Pos)clip.ascent << 6;
    FT_Pos bot = m.horiBearingY - m.height;
    if (bot < -((FT_Pos)clip.descent << 6)) bot = -((FT_Pos)clip.descent << 6);
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

    if (lcd && slot->format == FT_GLYPH_FORMAT_OUTLINE && bb.xMax > bb.xMin) { bb.xMin -= 64; bb.xMax += 64; }
    else lcd = false;

    // compute_metrics
    g.lcd = lcd;
    g.advance = (int16_t)(adv >> 6);
    g.left = (int16_t)(bb.xMin >> 6);
    g.top = (int16_t)(bb.yMax >> 6);
    g.w = (int16_t)((bb.xMax - bb.xMin) >> 6);
    g.h = (int16_t)((bb.yMax - bb.yMin) >> 6);
    g.mask.clear();
    if (g.w <= 0 || g.h <= 0) { g.w = g.h = 0; g.lcd = false; return true; }
    if (lcd) {
        if (f->fake_italic) FT_Outline_Transform(&slot->outline, &slant);
        FT_Outline_Translate(&slot->outline, -bb.xMin, -bb.yMin);
        static const FT_Matrix triple = { 3 << 16, 0, 0, 1 << 16 };
        FT_Outline_Transform(&slot->outline, &triple);
        int32_t sw = g.w * 3;
        std::vector<uint8_t> sub((size_t)sw * g.h, 0);
        FT_Bitmap bm;
        memset(&bm, 0, sizeof bm);
        bm.width = sw; bm.rows = g.h; bm.pitch = sw;
        bm.pixel_mode = FT_PIXEL_MODE_GRAY; bm.num_grays = 256; bm.buffer = sub.data();
        FT_Outline_Get_Bitmap(library, &slot->outline, &bm);
        // FreeType's default LCD filter (FT_LCD_FILTER_DEFAULT): 5 taps over the subpixels
        static const int fir[5] = { 8, 77, 86, 77, 8 };
        g.mask.assign((size_t)sw * g.h, 0);
        for (int32_t y = 0; y < g.h; y++) {
            const uint8_t* in = sub.data() + (size_t)y * sw;
            uint8_t* out = g.mask.data() + (size_t)y * sw;
            for (int32_t x = 0; x < sw; x++) {
                int v = 0;
                for (int k = 0; k < 5; k++) { int i = x + k - 2; if (i >= 0 && i < sw) v += fir[k] * in[i]; }
                v = (v + 128) >> 8;
                out[x] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
        return true;
    }
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
        // embedded bitmap (mono load only), placed by its own origin: the rows above the clipped top are left out
        const FT_Bitmap& src = slot->bitmap;
        int32_t ox = slot->bitmap_left - g.left, oy = g.top - slot->bitmap_top;   // the source's offset in the mask
        for (int32_t sy = 0; sy < (int32_t)src.rows; sy++) {
            int32_t y = sy + oy;
            if (y < 0 || y >= g.h) continue;
            const uint8_t* s = src.buffer + sy * src.pitch;
            for (int32_t sx = 0; sx < (int32_t)src.width; sx++) {
                int32_t x = sx + ox;
                if (x < 0 || x >= g.w) continue;
                bool on = src.pixel_mode == FT_PIXEL_MODE_MONO ? (s[sx / 8] & (0x80 >> (sx % 8))) : s[sx] >= 128;
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

// ---------------------------------------------------------------------------
// Font linking: a character the face lacks is drawn by another face at the same lfHeight, weight and style, as GDI's
// SystemLink and Uniscribe's fallback do: the link table's faces for the family in its order, then fontconfig's choice
// for the character. The glyph keeps the base font's baseline and is clipped to its ascent and descent; its advance is
// the linked face's, so measuring and drawing (both through get_glyph) agree.

static bool fallback_on = true;
void set_font_fallback(bool on) { fallback_on = on; }

static std::unordered_map<std::string, std::vector<std::string>> link_table;   // lower-case family -> linked families
static std::string lower(const char* s) { std::string o = s ? s : ""; for (char& c : o) c = (char)tolower((unsigned char)c); return o; }
static std::string trim(const std::string& v)
{
    size_t a = v.find_first_not_of(" \t\r\n"), b = v.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
}

bool load_font_links(const char* path)
{
    link_table.clear();
    FILE* fp = path && *path ? fopen(path, "r") : nullptr;
    if (!fp) return false;
    char line[2048];
    bool in = false;
    while (fgets(line, sizeof line, fp)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') { in = !strcasecmp(l.c_str(), "[SystemLink]"); continue; }
        size_t eq = l.find('=');
        if (!in || eq == std::string::npos) continue;
        std::vector<std::string>& to = link_table[lower(trim(l.substr(0, eq)).c_str())];
        to.clear();
        std::string v = l.substr(eq + 1);
        for (size_t p = 0; p <= v.size();) {
            size_t q = v.find(',', p);
            if (q == std::string::npos) q = v.size();
            std::string fam = trim(v.substr(p, q - p));
            if (!fam.empty()) to.push_back(fam);
            p = q + 1;
        }
    }
    fclose(fp);
    return true;
}

// a symbol font (Marlett, Wingdings: SYMBOL_CHARSET) is never linked; neither are the C0 and C1 controls
static bool may_link(const Font* f, uint32_t cp)
{
    return fallback_on && !f->symbol && cp >= 0x20 && !(cp >= 0x7f && cp < 0xa0);
}

// Unicode's default-ignorable code points (joiners, direction marks, variation selectors, tags): one the face lacks
// draws nothing and has no width, as Uniscribe shows them, instead of the missing glyph
static bool ignorable(uint32_t cp)
{
    return cp == 0xad || cp == 0x34f || cp == 0x61c || (cp >= 0x115f && cp <= 0x1160) || (cp >= 0x17b4 && cp <= 0x17b5) ||
           (cp >= 0x180b && cp <= 0x180f) || (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x202a && cp <= 0x202e) ||
           (cp >= 0x2060 && cp <= 0x206f) || cp == 0x3164 || (cp >= 0xfe00 && cp <= 0xfe0f) || cp == 0xfeff ||
           cp == 0xffa0 || (cp >= 0xfff0 && cp <= 0xfff8) || (cp >= 0x1bca0 && cp <= 0x1bca3) ||
           (cp >= 0x1d173 && cp <= 0x1d17a) || (cp >= 0xe0000 && cp <= 0xe0fff);
}

// fontconfig's faces for a family, best first (one FcFontSort per family, weight and slant, without a character): the
// theme's directory, the imported fonts, the free ones, then the system's. Colour-only and bitmap faces are left out:
// GDI draws text in one colour from outlines.
struct SortedFace { std::string file; int index; FcCharSet* chars; };
struct SortedSet { std::vector<FcFontSet*> sets; std::vector<SortedFace> faces; };
static std::unordered_map<std::string, SortedSet> sorted_cache;

static void sorted_faces_reset()
{
    for (auto& e : sorted_cache) for (FcFontSet* fs : e.second.sets) FcFontSetDestroy(fs);
    sorted_cache.clear();
}

static const SortedSet& sorted_faces(const char* family, int32_t weight, bool italic)
{
    std::string key = lower(family) + "|" + std::to_string(weight) + (italic ? "|i" : "|r");
    auto it = sorted_cache.find(key);
    if (it != sorted_cache.end()) return it->second;
    SortedSet& out = sorted_cache[key];
    FcConfig* configs[4] = { theme_config, app_config, fallback_config, nullptr };
    for (int c = 0; c < 4; c++) {
        if (c < 3 && !configs[c]) continue;
        FcConfig* cfg = c < 3 ? configs[c] : FcConfigGetCurrent();
        FcPattern* p = FcPatternCreate();
        FcPatternAddString(p, FC_FAMILY, (const FcChar8*)family);
        FcPatternAddInteger(p, FC_WEIGHT, FcWeightFromOpenType(weight));
        FcPatternAddInteger(p, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
        FcConfigSubstitute(cfg, p, FcMatchPattern);
        FcDefaultSubstitute(p);
        FcResult res;
        FcFontSet* fs = FcFontSort(cfg, p, FcFalse, nullptr, &res);
        FcPatternDestroy(p);
        if (!fs) continue;
        out.sets.push_back(fs);
        for (int i = 0; i < fs->nfont; i++) {
            FcPattern* m = fs->fonts[i];
            FcBool b;
            FcChar8* file;
            FcCharSet* cs;
            int index = 0;
            if (FcPatternGetBool(m, FC_OUTLINE, 0, &b) == FcResultMatch && !b) continue;
            if (FcPatternGetBool(m, FC_COLOR, 0, &b) == FcResultMatch && b) continue;
            if (FcPatternGetString(m, FC_FILE, 0, &file) != FcResultMatch || FcPatternGetCharSet(m, FC_CHARSET, 0, &cs) != FcResultMatch)
                continue;
            FcPatternGetInteger(m, FC_INDEX, 0, &index);
            out.faces.push_back(SortedFace{ (const char*)file, index, cs });
        }
    }
    return out;
}

static FT_UInt glyph_index(FT_Face face, uint32_t cp);

static Font* child_font(Font* f, const std::string& file, int index)
{
    if (file == f->file && index == f->index) return nullptr;
    for (Font* c : f->links) if (c->file == file && c->index == index) return c;
    for (Font* c : f->picked) if (c->file == file && c->index == index) return c;
    FontDesc d{ nullptr, f->height, f->weight, f->italic, false, false, f->quality };
    Font* c = open_font(file, index, d);
    if (c) f->picked.push_back(c);
    return c;
}

// the face that draws cp for f, which lacks it; nullptr when none has it
static Font* linked_font(Font* f, uint32_t cp)
{
    auto it = f->fallback.find(cp);
    if (it != f->fallback.end()) return it->second;
    if (!f->linked) {
        f->linked = true;
        auto lt = link_table.find(lower(f->face->family_name));
        if (lt != link_table.end())
            for (const std::string& fam : lt->second) {
                std::string file;
                int index = 0;
                if (!find_face(alias(fam.c_str()), f->weight, f->italic, file, index, true)) continue;
                if (file == f->file && index == f->index) continue;
                FontDesc d{ nullptr, f->height, f->weight, f->italic, false, false, f->quality };
                if (Font* c = open_font(file, index, d)) f->links.push_back(c);
            }
    }
    Font* found = nullptr;
    for (Font* c : f->links) if (glyph_index(c->face, cp)) { found = c; break; }
    // a face fontconfig gave an earlier character of this font, so a run of one script stays in one face
    if (!found) for (Font* c : f->picked) if (glyph_index(c->face, cp)) { found = c; break; }
    if (!found)
        for (const SortedFace& sf : sorted_faces(f->face->family_name, f->weight, f->italic).faces) {
            if (!FcCharSetHasChar(sf.chars, cp)) continue;
            Font* c = child_font(f, sf.file, sf.index);
            if (c && glyph_index(c->face, cp)) { found = c; break; }
        }
    f->fallback[cp] = found;
    return found;
}

static inline bool mono_mode(const Font* f, bool cleartype)
{
    return f->quality == 2 ? true : f->quality == 1 || cleartype ? false : !smoothing || f->gasp_mono;
}

// opaque: the text goes onto an opaque surface (ClearType needs one; an alpha-tracking surface gets grey instead)
static const Glyph* get_glyph(const Font* cf, uint32_t cp, bool opaque = false)
{
    Font* f = (Font*)cf;   // the cache is not part of the font's logical state
    // ClearType (the system setting, or CLEARTYPE_QUALITY) ignores the gasp table's grey veto, as GDI's ClearType does
    bool cleartype = f->quality == 3 || (f->quality == 0 && smoothing == SMOOTH_CLEARTYPE);
    bool mono = mono_mode(f, cleartype);
    bool lcd = cleartype && opaque;
    uint64_t key = (uint64_t)cp | (uint64_t)(lcd ? 2 : mono ? 1 : 0) << 32;
    auto it = f->cache.find(key);
    if (it != f->cache.end()) return &it->second;

    Glyph g{};
    FT_UInt index = glyph_index(f->face, cp);
    bool ok = false;
    if (!index && may_link(f, cp)) {
        if (ignorable(cp)) ok = true;
        // a linked face's own gasp table decides grey or mono, as it would on its own
        else if (Font* c = linked_font(f, cp)) ok = render_glyph(c, glyph_index(c->face, cp), mono_mode(c, cleartype), g, lcd, f->tm);
    }
    // cache_glyph_bitmap: the glyph, else .notdef, else space
    FT_UInt indices[3] = { index, 0, glyph_index(f->face, ' ') };
    for (int i = 0; i < 3 && !ok; i++) ok = render_glyph(f, indices[i], mono, g, lcd, f->tm);
    if (!ok) g = Glyph{};
    return &(f->cache[key] = std::move(g));
}

std::string glyph_family(const Font* cf, uint32_t cp)
{
    Font* f = (Font*)cf;
    if (!f) return "";
    if (glyph_index(f->face, cp)) return f->face->family_name ? f->face->family_name : "";
    Font* c = may_link(f, cp) && !ignorable(cp) ? linked_font(f, cp) : nullptr;
    return c && c->face->family_name ? c->face->family_name : "";
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
    bool opaque_target = !dc->target->has_alpha;
    for_each_cp(s, len, [&](uint32_t cp) { glyphs.push_back(get_glyph(f, cp, opaque_target)); });
    int32_t width = 0;
    for (const Glyph* g : glyphs) width += g->advance;

    if (opaque && fill) fill_rect(dc, *fill, bg);
    else if (opaque) fill_rect(dc, Rect{pos.x, pos.y, pos.x + width, pos.y + f->tm.height}, bg);

    for (const Glyph* g : glyphs) {
        if (g->w && g->lcd) blend_mask_lcd(dc, Point{x + g->left, baseline - g->top}, g->mask.data(), g->w, g->h, g->w * 3, fg);
        else if (g->w) blend_mask(dc, Point{x + g->left, baseline - g->top}, g->mask.data(), g->w, g->h, g->w, fg);
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

} // namespace gdi
