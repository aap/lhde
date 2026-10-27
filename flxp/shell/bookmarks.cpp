#include "bookmarks.h"
#include "config.h"

namespace shell {

std::string Bookmark::path() const { return path_from_uri(uri); }

std::string Bookmark::display_name() const
{
    if (!label.empty()) return label;
    std::string p = path();
    if (p.empty()) return uri;
    return base_of(p);
}

std::string bookmarks_path() { return config_home() + "/gtk-3.0/bookmarks"; }

std::vector<Bookmark> load_bookmarks()
{
    std::vector<Bookmark> out;
    std::string text;
    if (!read_file(bookmarks_path(), text) && !read_file(home_dir() + "/.gtk-bookmarks", text)) return out;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = text.substr(i, j - i);
        i = j + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        Bookmark b;
        size_t sp = line.find(' ');
        b.uri = line.substr(0, sp);
        if (sp != std::string::npos) b.label = line.substr(sp + 1);
        out.push_back(b);
    }
    return out;
}

bool save_bookmarks(const std::vector<Bookmark>& list)
{
    std::string text;
    for (const Bookmark& b : list) text += b.uri + (b.label.empty() ? "" : " " + b.label) + "\n";
    return write_file_atomic(bookmarks_path(), text);
}

bool add_bookmark(const std::string& path, const std::string& label)
{
    std::vector<Bookmark> list = load_bookmarks();
    std::string uri = uri_from_path(path);
    for (const Bookmark& b : list) if (b.uri == uri || b.path() == path) return true;
    Bookmark b;
    b.uri = uri;
    b.label = label;
    list.push_back(b);
    return save_bookmarks(list);
}

} // namespace shell
