// Fonts and text, rendered the way GDI does it on Windows XP at 96 DPI:
// FreeType with the v35 bytecode interpreter, hinted integer advances, mono
// glyphs unless smoothing is on and the gasp table asks for gray. Metrics and
// size selection follow Wine's win32u/freetype.c.
#pragma once
#include <stdint.h>
#include "geom.h"
#include "color.h"
#include "dc.h"

namespace gdi {

struct Font;                       // opaque

struct FontDesc {
    const char* family;            // "Tahoma", "Microsoft Sans Serif", ...
    int32_t height;                // lfHeight: <0 em height px, >0 cell height px, 0 = default (12px cell)
    int32_t weight;                // 400 normal, 700 bold; 0 = 400
    bool italic, underline, strikeout;
    uint8_t quality = 0;           // 0 = the system setting (set_font_smoothing, gasp decides), 1 = antialiased, 2 = never
};

struct TextMetrics {
    int32_t height, ascent, descent, internal_leading, external_leading, ave_char_width, max_char_width;
    bool fixed_pitch;              // true for monospaced fonts (not the inverted TMPF_FIXED_PITCH bit)
};

Font* create_font(const FontDesc& desc);
Font* create_font(const char* family, int32_t height, int32_t weight = 400, bool italic = false);
void destroy_font(Font* f);

TextMetrics metrics(const Font* f);
Size text_extent(const Font* f, const char* utf8, int32_t len = -1);   // len -1 = nul terminated
int32_t char_width(const Font* f, uint32_t codepoint);

// pos = top-left of the text cell (TA_TOP|TA_LEFT), logical coords. Honours
// the DC clip and origin. Underline/strikeout come from the font.
void text_out(DC* dc, Point pos, const Font* f, const char* utf8, int32_t len, Color fg);
// Same, but first fills `opaque` (logical coords) with bg, or the text cell
// if opaque is null (ETO_OPAQUE).
void text_out(DC* dc, Point pos, const Font* f, const char* utf8, int32_t len, Color fg, Color bg, const Rect* opaque);

// Longhorn TextGlow: a soft halo of `glow` (about `radius` px) around the
// glyphs of the string at pos, without the text itself; draw it with
// text_out afterwards. Works on opaque and alpha-tracking surfaces.
void glow_text(DC* dc, Point pos, const Font* f, const char* utf8, int32_t len, Color glow, int32_t radius = 3);

void add_font_dir(const char* dir);       // searched before the system fonts (a directory or one font file)
// "Smooth edges of screen fonts" (Display Properties > Appearance > Effects):
// SPI_SETFONTSMOOTHING / SPI_SETFONTSMOOTHINGTYPE. Standard = grayscale where
// the font's gasp table allows gray at the ppem (Tahoma 8 stays mono);
// ClearType is drawn as Standard for now. Default Standard, as on XP.
enum { SMOOTH_OFF = 0, SMOOTH_STANDARD = 1, SMOOTH_CLEARTYPE = 2 };
void set_font_smoothing(int mode);        // SMOOTH_* (true = Standard, false = off)
int font_smoothing();

} // namespace gdi
