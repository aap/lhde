// mupf's Markdown (apps/mupf/markdown.h), without mupdf or a window: which files are Markdown; GitHub's dialect as md4c
// renders it (a heading, bold, a link, a table, strikethrough, a fenced block keeping its language as a class, an
// autolink); the headings' ids, GitHub's anchors, a repeat numbered; the task boxes as characters; the front matter left
// out (and a "---" that is a rule kept); the title the first H1; the page around it with the stylesheet's family.
#include <stdio.h>
#include <string>
#include <vector>
#include "apps/mupf/markdown.h"

using namespace mupf;

static int fails, checks;
static void check(const char* what, bool ok, const std::string& in = std::string())
{
    checks++;
    if (ok) return;
    fails++;
    printf("FAIL %s%s%s\n", what, in.empty() ? "" : ": ", in.c_str());
}
static bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
static void contains(const char* what, const std::string& html, const char* part) { check(what, has(html, part), html); }

int main()
{
    check("README.md is Markdown", md::is_markdown("/x/README.md"));
    check("notes.markdown is Markdown", md::is_markdown("notes.markdown"));
    check("A.MKD is Markdown", md::is_markdown("A.MKD"));
    check("README is not", !md::is_markdown("/x/README"));
    check("a .md directory's file is not", !md::is_markdown("/x/y.md/README"));
    check("readme.txt is not", !md::is_markdown("readme.txt"));

    std::vector<md::Heading> hs;
    std::string h = md::body_html("# Hello World\n\nSome **bold** and a [link](other.md).\n\n"
                                  "| A | B |\n| --- | --- |\n| 1 | ~~2~~ |\n\n```c\nint x;\n```\n\nhttps://example.org\n", &hs);
    contains("the heading with its anchor", h, "<h1 id=\"hello-world\">Hello World</h1>");
    contains("bold", h, "<strong>bold</strong>");
    contains("the link", h, "<a href=\"other.md\">link</a>");
    contains("the table", h, "<table>");
    contains("its header cell", h, "<th>A</th>");
    contains("strikethrough", h, "<del>2</del>");
    contains("the fenced block's language as a class", h, "<pre><code class=\"language-c\">int x;");
    contains("an autolink", h, "<a href=\"https://example.org\">https://example.org</a>");
    check("one heading", hs.size() == 1);

    hs.clear();
    h = md::body_html("##No space\n\n## Usage & Notes!\n\n## Usage & Notes!\n\n### `code` *here*\n", &hs);
    contains("an ATX heading without the space", h, "<h2 id=\"no-space\">No space</h2>");
    contains("punctuation out of the anchor, the space a dash", h, "id=\"usage--notes\"");
    contains("a repeat numbered", h, "id=\"usage--notes-1\"");
    check("four headings", hs.size() == 4);
    check("the markup out of a heading's text", hs.size() == 4 && hs[3].text == "code here" && hs[3].level == 3, hs.size() == 4 ? hs[3].text : "");
    check("its anchor", hs.size() == 4 && hs[3].id == "code-here", hs.size() == 4 ? hs[3].id : "");

    h = md::body_html("- [ ] open\n- [x] done\n", nullptr);
    contains("an open task's box", h, "\xE2\x98\x90");
    contains("a done task's box", h, "\xE2\x98\x91");
    check("no form control left", !has(h, "<input"), h);

    h = md::body_html("---\ntitle: secret\nlayout: post\n---\n# Visible\n", nullptr);
    check("the front matter left out", !has(h, "secret") && has(h, "Visible"), h);
    h = md::body_html("Text\n\n---\n\nMore\n", nullptr);
    contains("a rule after text kept", h, "<hr>");
    h = md::body_html("---\nnever closed\n", nullptr);
    contains("an unclosed front matter is text", h, "never closed");

    md::Style st;
    st.family = "Tahoma";
    md::Page p = md::convert("Intro\n\n## Second\n\n# First H1\n\n# Another H1\n", st);
    check("the title: the first H1", p.title == "First H1", p.title);
    contains("the page: a stylesheet", p.html, "<style>");
    contains("the family in it", p.html, "\"Tahoma\", sans-serif");
    contains("the body in it", p.html, "<h2 id=\"second\">Second</h2>");
    check("no H1: no title", md::convert("## Only\n", st).title.empty());
    check("slug of mixed case", md::slug("Getting Started, Fast") == "getting-started-fast", md::slug("Getting Started, Fast"));

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
