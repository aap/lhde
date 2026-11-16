// mupf's Markdown in the session's UI font: the files of a family's four faces, as wux's text finds them (wux/gdi/font.h
// face_file: the theme's fonts, the imported ones, the free ones, the system's), for mupdf to draw the page's body with.
#include "uifont.h"
#include "gdi/font.h"

namespace mupf {

FontFiles ui_font_files(const std::string& family)
{
    FontFiles f;
    if (family.empty()) return f;
    const struct { int weight; bool italic; } faces[4] = { { 400, false }, { 700, false }, { 400, true }, { 700, true } };
    for (int i = 0; i < 4; i++) {
        std::string file;
        int index = 0;
        if (!gdi::face_file(family.c_str(), faces[i].weight, faces[i].italic, &file, &index)) continue;
        // a style the family lacks is the regular face again (fontconfig's nearest): left empty, mupdf makes it bold or
        // slanted itself
        if (i > 0 && file == f.file[0] && index == f.index[0]) continue;
        f.file[i] = file;
        f.index[i] = index;
    }
    if (!f.file[0].empty()) f.family = family;
    return f;
}

}   // namespace mupf
