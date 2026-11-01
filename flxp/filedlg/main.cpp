// flxp-filedialog: the file dialog from the command line, zenity-style.
// Prints the chosen paths one per line (or NUL-separated with --null) and
// exits 0; exit 1 on cancel, 2 on bad usage. The xdg-desktop-portal backend
// (flxp-portal) runs this for every request.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "filedlg.h"
#include "shell/config.h"
#include "w32/w32.h"
#include "w32/user.h"

// The shared start-up plumbing (src/shell/runtime.cpp: fonts, theme name ->
// .msstyles, flxp.ini). Weak so this builds while that file does not exist.
namespace shell {
void setup_fonts() __attribute__((weak));
std::string setup_theme() __attribute__((weak));
}

static void usage(FILE* f)
{
    fputs("usage: flxp-filedialog [options]\n"
          "  --open | --save | --directory   mode (default --open)\n"
          "  --multiple                      allow several files (open)\n"
          "  --title TEXT                    window title\n"
          "  --accept-label TEXT             the Open/Save button (\"_Open\" or \"&Open\")\n"
          "  --filename NAME                 initial file name (may include a directory)\n"
          "  --directory-start DIR           initial folder (default: the current directory)\n"
          "  --filter 'Name|*.png *.jpg image/webp'   repeatable; tokens with '/' are MIME types\n"
          "  --current-filter N              initially selected filter (0-based)\n"
          "  --default-ext EXT               save: appended when the name has no extension\n"
          "  --no-confirm-overwrite          save: do not ask before replacing\n"
          "  --show-hidden                   show dot files (Ctrl+H toggles)\n"
          "  --parent x11:HEXID | ID         transient-for window (another program's)\n"
          "  --no-modal                      do not ask the window manager to make it modal for --parent\n"
          "  --null                          separate output paths with NUL instead of newline\n"
          "  --separator STR                 separator between paths (zenity)\n"
          "  --print-filter                  first output record: filter=<index of the filter in effect>\n"
          "  --uri                           print file:// URIs instead of paths\n"
          "  --look xp|longhorn|auto         the dialog's look (default: filedlg= in flxp.ini, else the\n"
          "                                  explorer's frame style: Longhorn under luxwm's Longhorn themes)\n"
          "Exit status: 0 chosen, 1 cancelled, 2 usage error.\n", f);
}

static uint64_t parse_parent(const char* s)
{
    if (!strncmp(s, "x11:", 4)) return strtoull(s + 4, nullptr, 16);
    if (!strncmp(s, "wayland:", 8)) return 0;   // no X id to be transient for
    return strtoull(s, nullptr, 0);
}

static filedlg::Filter parse_filter(const std::string& spec)
{
    filedlg::Filter f;
    size_t bar = spec.rfind('|');   // names may contain '|', patterns do not
    std::string pats = bar == std::string::npos ? spec : spec.substr(bar + 1);
    f.name = bar == std::string::npos ? spec : spec.substr(0, bar);
    std::string cur;
    auto flush = [&] {
        if (cur.empty()) return;
        if (cur.find('/') != std::string::npos) f.mime_types.push_back(cur); else f.patterns.push_back(cur);
        cur.clear();
    };
    for (char c : pats) { if (c == ' ' || c == ';' || c == ',') flush(); else cur += c; }
    flush();
    if (bar == std::string::npos) {   // zenity also takes a bare pattern list
        f.name = spec;
    }
    return f;
}

int main(int argc, char** argv)
{
    filedlg::Options o;
    bool null_sep = false, print_filter = false, uris = false;
    std::string sep = "\n";
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd)) o.initial_dir = cwd;
    bool explicit_dir = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        std::string val;
        size_t eq = a.find('=');
        bool has_eq = a.rfind("--", 0) == 0 && eq != std::string::npos;
        if (has_eq) { val = a.substr(eq + 1); a = a.substr(0, eq); }
        auto arg = [&]() -> std::string {
            if (has_eq) return val;
            if (i + 1 >= argc) { fprintf(stderr, "flxp-filedialog: %s needs a value\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--open" || a == "--file-selection") {}
        else if (a == "--save") o.mode = filedlg::Mode::Save;
        else if (a == "--directory") o.mode = filedlg::Mode::Folder;
        else if (a == "--multiple") o.multiple = true;
        else if (a == "--title") o.title = arg();
        else if (a == "--accept-label") o.accept_label = arg();
        else if (a == "--filename") o.default_name = arg();
        else if (a == "--directory-start" || a == "--initial-dir") { o.initial_dir = arg(); explicit_dir = true; }
        else if (a == "--filter" || a == "--file-filter") o.filters.push_back(parse_filter(arg()));
        else if (a == "--current-filter") o.filter_index = atoi(arg().c_str());
        else if (a == "--default-ext") o.default_ext = arg();
        else if (a == "--no-confirm-overwrite") o.overwrite_prompt = false;
        else if (a == "--confirm-overwrite") o.overwrite_prompt = true;
        else if (a == "--show-hidden") o.show_hidden = true;
        else if (a == "--no-modal") o.modal = false;
        else if (a == "--parent" || a == "--attach") o.parent_window = parse_parent(arg().c_str());
        else if (a == "--null" || a == "-0") null_sep = true;
        else if (a == "--separator") sep = arg();
        else if (a == "--print-filter") print_filter = true;
        else if (a == "--uri") uris = true;
        else if (a == "--look") {
            std::string l = arg();
            o.style = l == "xp" ? filedlg::Options::Style::XP : l == "longhorn" ? filedlg::Options::Style::Longhorn : filedlg::Options::Style::Auto;
        }
        else if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else { fprintf(stderr, "flxp-filedialog: unknown option %s\n", argv[i]); usage(stderr); return 2; }
    }
    // a --filename with a directory decides the folder unless one was given
    if (!explicit_dir && o.default_name.find('/') != std::string::npos) o.initial_dir.clear();
    // the folder the caller asked for is its "App Location" (the Longhorn look's Common Places)
    if (explicit_dir) o.app_location = o.initial_dir;
    else if (o.default_name.find('/') != std::string::npos) o.app_location = o.default_name.substr(0, o.default_name.rfind('/') + 1);

    if (shell::setup_fonts) shell::setup_fonts();
    if (shell::setup_theme) shell::setup_theme();
    else if (!getenv("FLXP_THEME")) {
        std::string t = shell::setting("theme");
        if (!t.empty()) setenv("FLXP_THEME", t.c_str(), 1);
    }
    w32::init();

    filedlg::Result r = filedlg::run(nullptr, o);
    if (!r.ok) return 1;
    const char* s = null_sep ? "" : sep.c_str();
    size_t slen = null_sep ? 1 : sep.size();
    if (print_filter) { printf("filter=%d", r.filter_index); fwrite(null_sep ? "\0" : "\n", 1, 1, stdout); }
    for (size_t i = 0; i < r.paths.size(); i++) {
        std::string p = uris ? shell::uri_from_path(r.paths[i]) : r.paths[i];
        fwrite(p.data(), 1, p.size(), stdout);
        if (i + 1 < r.paths.size() || null_sep || sep == "\n") fwrite(s, 1, slen, stdout);
    }
    fflush(stdout);
    return 0;
}
