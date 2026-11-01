// The small string and file helpers flxp shares (shell layer, explorer,
// file dialog, taskbar): one copy each. No win32 here. wux keeps its own
// (it does not depend on flxp).
#pragma once
#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string>

namespace shell {

// s without the characters of ws at either end; the default strips blanks,
// tabs, CR and LF (a line read with or without its end)
inline std::string trim(const std::string& s, const char* ws = " \t\r\n")
{
    size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

// ASCII lower case
inline std::string lower(std::string s)
{
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

// equal ignoring case (ini sections and keys)
inline bool iequal(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

// the path names something (stat follows symlinks: a dangling link does not exist)
inline bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
inline bool is_dir(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
inline bool is_file(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }

// the whole file into out; false when it cannot be opened
inline bool read_file(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

} // namespace shell
