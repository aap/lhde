// mupf's Markdown (markdown.h): md4c's HTML in a page of mupf's for mupdf's HTML engine.
#include "markdown.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>
extern "C" {
#include "vendor/md4c/md4c-html.h"
}

namespace mupf {
namespace md {

bool is_markdown(const std::string& path)
{
    size_t dot = path.rfind('.'), slash = path.rfind('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return false;
    const char* ext = path.c_str() + dot + 1;
    return !strcasecmp(ext, "md") || !strcasecmp(ext, "markdown") || !strcasecmp(ext, "mkd");
}

namespace {

void append(const MD_CHAR* text, MD_SIZE size, void* out) { ((std::string*)out)->append(text, size); }

// the front matter: a first line of "---" up to a line of "---" or "..." (Jekyll's, Hugo's YAML); its end, 0 when none
size_t front_matter_end(const std::string& t)
{
    size_t at = t.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;   // a byte order mark
    if (t.compare(at, 3, "---") != 0) return 0;
    size_t eol = t.find('\n', at);
    if (eol == std::string::npos) return 0;
    for (size_t i = at + 3; i < eol; i++)
        if (t[i] != ' ' && t[i] != '\t' && t[i] != '\r') return 0;
    for (size_t line = eol + 1; line < t.size();) {
        size_t end = t.find('\n', line);
        if (end == std::string::npos) end = t.size();
        std::string l = t.substr(line, end - line);
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
        if (l == "---" || l == "...") return end < t.size() ? end + 1 : end;
        line = end + 1;
    }
    return 0;   // never closed: not front matter
}

// the text of an HTML fragment: tags taken out, the entities md4c writes (and the common named ones) decoded
std::string text_of(const std::string& html)
{
    std::string out;
    for (size_t i = 0; i < html.size(); i++) {
        char c = html[i];
        if (c == '<') {
            size_t e = html.find('>', i);
            if (e == std::string::npos) break;
            i = e;
            continue;
        }
        if (c == '&') {
            size_t e = html.find(';', i);
            if (e != std::string::npos && e - i <= 10) {
                std::string ent = html.substr(i + 1, e - i - 1);
                const char* rep = ent == "amp" ? "&" : ent == "lt" ? "<" : ent == "gt" ? ">" : ent == "quot" ? "\"" :
                                  ent == "apos" || ent == "#39" ? "'" : ent == "nbsp" ? " " : nullptr;
                if (rep) { out += rep; i = e; continue; }
            }
        }
        out += c;
    }
    // runs of white space as one space, none at the ends
    std::string s;
    for (char c : out) {
        bool sp = c == ' ' || c == '\n' || c == '\t' || c == '\r';
        if (sp) { if (!s.empty() && s.back() != ' ') s += ' '; }
        else s += c;
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string escape_attr(const std::string& s)
{
    std::string o;
    for (char c : s) {
        if (c == '"') o += "&quot;";
        else if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else o += c;
    }
    return o;
}

}   // namespace

std::string slug(const std::string& text)
{
    // GitHub's: lower case, every character but letters, digits, '-', '_' and spaces dropped, a space a '-'; letters
    // beyond ASCII kept as they are (their case is not folded here)
    std::string o;
    for (unsigned char c : text) {
        if (c >= 0x80 || isalnum(c) || c == '-' || c == '_') o += (char)tolower(c);
        else if (c == ' ') o += '-';
    }
    return o;
}

std::string body_html(const std::string& text, std::vector<Heading>* headings)
{
    size_t from = front_matter_end(text);
    std::string raw;
    unsigned flags = MD_DIALECT_GITHUB | MD_FLAG_PERMISSIVEATXHEADERS;
    md_html(text.data() + from, (MD_SIZE)(text.size() - from), append, &raw, flags, MD_HTML_FLAG_SKIP_UTF8_BOM);

    // a pass over md4c's HTML: <hN> (md4c's, and raw HTML's without attributes) given ids, the task boxes characters
    std::string out;
    out.reserve(raw.size() + raw.size() / 8);
    std::vector<std::string> used;
    const char* box = "<input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled";
    size_t box_len = strlen(box);
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] == '<' && i + 3 < raw.size() && raw[i + 1] == 'h' && raw[i + 2] >= '1' && raw[i + 2] <= '6' && raw[i + 3] == '>') {
            int level = raw[i + 2] - '0';
            std::string close = "</h" + std::string(1, raw[i + 2]) + ">";
            size_t end = raw.find(close, i + 4);
            if (end != std::string::npos) {
                std::string inner = raw.substr(i + 4, end - i - 4);
                Heading h;
                h.level = level;
                h.text = text_of(inner);
                std::string base = slug(h.text), id = base;
                int n = 0;
                for (;;) {
                    bool taken = false;
                    for (const std::string& u : used) if (u == id) { taken = true; break; }
                    if (!taken) break;
                    id = base + "-" + std::to_string(++n);
                }
                used.push_back(id);
                h.id = id;
                out += "<h" + std::string(1, raw[i + 2]) + " id=\"" + escape_attr(id) + "\">" + inner + close;
                if (headings) headings->push_back(h);
                i = end + close.size();
                continue;
            }
        }
        if (raw[i] == '<' && raw.compare(i, box_len, box) == 0) {
            size_t e = raw.find('>', i);
            if (e != std::string::npos) {
                bool checked = raw.compare(i + box_len, 8, " checked") == 0;
                // a ballot box, checked or not (U+2611, U+2610), as GitHub's disabled check boxes look
                out += checked ? "<span class=\"task\">\xE2\x98\x91</span> " : "<span class=\"task\">\xE2\x98\x90</span> ";
                i = e + 1;
                continue;
            }
        }
        out += raw[i++];
    }
    return out;
}

// The look: no Windows program of the era rendered Markdown, so the model is GitHub's rendering of a README (its
// markdown-body stylesheet), a choice and not a measurement of anything Longhorn: its spacing and sizes, its greys and
// its link blue, the body in the theme's UI font instead of GitHub's system font stack. GitHub's sizes are CSS pixels
// at 96 DPI, which mupdf takes for points; mupf draws a point as 4/3 of a pixel at 100%, so they are given here in points
// (16 px = 12 pt). What mupdf's CSS does not do is left out: no border-radius, no rgba() colours (GitHub's translucent
// greys as the opaque colour they give on white), no :nth-child for the table's striped rows, no max-width.
std::string stylesheet(const Style& s)
{
    std::string fam = s.family.empty() ? "sans-serif" : "\"" + s.family + "\", sans-serif";
    char body[160];
    snprintf(body, sizeof body, "font-size:%gpt;", s.body_px * 0.75);
    std::string css;
    css += "@page{margin:0}\n";
    css += "body{margin:0;padding:24pt 24pt 30pt 24pt;" + std::string(body) + "line-height:1.5;color:#1f2328;font-family:" + fam + "}\n";
    css += "h1,h2,h3,h4,h5,h6{margin-top:18pt;margin-bottom:12pt;font-weight:bold;line-height:1.25}\n";
    css += "h1{font-size:2em;padding-bottom:0.3em;border-bottom:0.75pt solid #d8dee4}\n";
    css += "h2{font-size:1.5em;padding-bottom:0.3em;border-bottom:0.75pt solid #d8dee4}\n";
    css += "h3{font-size:1.25em}\nh4{font-size:1em}\nh5{font-size:0.875em}\nh6{font-size:0.85em;color:#59636e}\n";
    css += "p,blockquote,ul,ol,dl,table,pre{margin-top:0;margin-bottom:12pt}\n";
    css += "ul,ol{padding-left:2em}\nli{margin-top:0.25em}\nul ul,ul ol,ol ol,ol ul{margin-top:0;margin-bottom:0}\n";
    css += "li.task-list-item{list-style-type:none}\nspan.task{font-family:sans-serif}\n";
    css += "a:link{color:#0969da;text-decoration:none}\n";   // (mupdf's own sheet says a:link; a plain "a" loses to it)
    css += "strong,b{font-weight:bold}\n";
    css += "del{text-decoration:line-through}\n";
    css += "blockquote{margin-left:0;margin-right:0;padding:0 1em;color:#59636e;border-left:3pt solid #d1d9e0}\n";
    css += "code,tt,kbd,samp{font-family:monospace;font-size:85%;background-color:#eff1f3}\n";
    css += "pre{font-family:monospace;padding:12pt;font-size:85%;line-height:1.45;background-color:#f6f8fa;"
           "white-space:pre-wrap}\n";   // (GitHub scrolls a long line; a page cannot: it wraps)
    css += "pre code{font-size:100%;background-color:transparent;padding:0}\n";
    css += "table{border-collapse:collapse}\n";
    css += "th,td{padding:4.5pt 9.75pt;border:0.75pt solid #d1d9e0}\nth{font-weight:bold;background-color:#f6f8fa}\n";
    css += "hr{border:0;border-top:3pt solid #d1d9e0;margin:18pt 0;height:0}\n";
    css += "img{border:0}\n";
    return css;
}

Page convert(const std::string& text, const Style& s)
{
    Page p;
    std::string body = body_html(text, &p.headings);
    for (const Heading& h : p.headings)
        if (h.level == 1) { p.title = h.text; break; }
    p.html = "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><style>\n" + stylesheet(s) + "</style></head><body>\n" +
             body + "</body></html>\n";
    return p;
}

}   // namespace md
}   // namespace mupf
