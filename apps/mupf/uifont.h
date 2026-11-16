// mupf's Markdown in the session's UI font (uifont_native.cpp): no win32 and no mupdf types here.
#pragma once
#include <string>
#include "engine.h"

namespace mupf {

// the faces of family (regular, bold, italic, bold italic) the session's text would use; an empty family when it is not
// installed (the page is then in mupdf's sans-serif)
FontFiles ui_font_files(const std::string& family);

}   // namespace mupf
