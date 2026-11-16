// mupf's Markdown: a .md file converted to an HTML page for mupdf's HTML engine to lay out (engine_native.cpp opens the
// page with the file's directory as its base, so relative pictures and links resolve there). md4c (apps/mupf/vendor/md4c)
// parses it with GitHub's extensions; what this adds is the page around md4c's HTML: the stylesheet, an id on every
// heading (GitHub's anchors, "#getting-started"), the task lists' boxes as characters (mupdf draws no form controls), the
// front matter left out. Plain C++: no win32 and no mupdf here (tests/toolkit/mdtest.cpp drives it).
#pragma once
#include <string>
#include <vector>

namespace mupf {
namespace md {

// .md, .markdown, .mkd (any case): Markdown, text/markdown (a README without an extension stays with the text editor)
bool is_markdown(const std::string& path);

struct Heading {
    int level = 1;              // 1..6
    std::string text;           // the heading's text, its markup taken out
    std::string id;             // its anchor, GitHub's way: lower case, punctuation dropped, spaces as '-', a repeat "-1"
};

struct Page {
    std::string html;           // the whole document: <html><head><style>...</style></head><body>...</body></html>
    std::vector<Heading> headings;
    std::string title;          // the first H1's text, "" when there is none
};

// What the stylesheet takes from the session: the UI font's family (the theme's message font), the size of the body text in
// CSS pixels; an empty family is mupdf's sans-serif.
struct Style {
    std::string family;
    int body_px = 16;
};

// the page's width in points that mupf lays Markdown out at (GitHub's README column: 980 px less its padding)
const float PAGE_WIDTH_PT = 660;

// md4c's HTML for the text (GitHub's dialect: tables, strikethrough, task lists, autolinks, ATX headings without the space),
// the headings given ids and the task boxes made characters; the front matter (a first line "---" up to the next "---" or
// "...") left out
std::string body_html(const std::string& text, std::vector<Heading>* headings);
std::string stylesheet(const Style& s);
Page convert(const std::string& text, const Style& s);
// GitHub's anchor for a heading's text (without the repeat's suffix)
std::string slug(const std::string& text);

}   // namespace md
}   // namespace mupf
