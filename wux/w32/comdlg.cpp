// comdlg32's file dialogs: GetOpenFileNameW/A, GetSaveFileNameW/A and
// CommDlgExtendedError, mapped onto src/filedlg (the XP Explorer-style
// dialog re-created on the toolkit). Programs that do not link the file
// dialog get CDERR_INITIALIZATION: filedlg::run is referenced weakly so the
// shim does not drag the dialog into every binary (link $(FILEDLGLIB)).
//
// Supported: lpstrFilter (double-NUL "name\0patterns\0" list, ';'-separated
// globs), nFilterIndex (1-based, 0 = lpstrCustomFilter), lpstrFile (initial
// name, may carry a directory; result buffer with nFileOffset /
// nFileExtension, FNERR_BUFFERTOOSMALL), lpstrFileTitle, lpstrInitialDir,
// lpstrTitle, lpstrDefExt, Flags OFN_ALLOWMULTISELECT (Explorer format:
// "dir\0name\0name\0\0"), OFN_FILEMUSTEXIST, OFN_PATHMUSTEXIST,
// OFN_OVERWRITEPROMPT, OFN_CREATEPROMPT, OFN_HIDEREADONLY, OFN_READONLY,
// OFN_NOCHANGEDIR (without it the process cwd becomes the chosen folder, as
// on Windows), OFN_FORCESHOWHIDDEN. Not supported: hooks (lpfnHook is
// never called), custom templates, OFN_NODEREFERENCELINKS, OFN_SHAREAWARE.
#define NOMINMAX
#include <string.h>
#include <algorithm>
#include <unistd.h>
#include "w32.h"
#include "filedlg/filedlg.h"
extern "C" {
#include "commdlg.h"
#include "cderr.h"
}

namespace filedlg { Result run(void* owner, const Options& o) __attribute__((weak)); }

using namespace w32;

static thread_local DWORD g_cderr;

extern "C" DWORD WINAPI CommDlgExtendedError(void) { return g_cderr; }

static std::string u8(const WCHAR* s) { return s ? utf8_of(s) : std::string(); }

// "Text Files\0*.txt;*.text\0All Files\0*.*\0\0"
static std::vector<filedlg::Filter> parse_filters(const WCHAR* p)
{
    std::vector<filedlg::Filter> out;
    if (!p) return out;
    while (*p) {
        filedlg::Filter f;
        f.name = utf8_of(p);
        p += utf16_len(p) + 1;
        if (!*p) { out.push_back(f); break; }
        std::string pats = utf8_of(p);
        p += utf16_len(p) + 1;
        std::string cur;
        for (char c : pats + ";") {
            if (c == ';' || c == ' ') { if (!cur.empty()) f.patterns.push_back(cur); cur.clear(); }
            else cur += c;
        }
        out.push_back(f);
    }
    return out;
}

static std::string common_dir(const std::vector<std::string>& paths)
{
    std::string dir = paths[0].substr(0, paths[0].find_last_of('/'));
    for (const std::string& p : paths) {
        while (!dir.empty() && !(p.size() > dir.size() && p.compare(0, dir.size(), dir) == 0 && p[dir.size()] == '/'))
            dir = dir.substr(0, dir.find_last_of('/'));
    }
    return dir.empty() ? "/" : dir;
}

// Write a UTF-16 string list into buf (cch WCHARs); false if it does not fit.
static bool put(WCHAR* buf, DWORD cch, const std::u16string& s)
{
    if (!buf || s.size() + 1 > cch) return false;
    memcpy(buf, s.data(), s.size() * sizeof(WCHAR));
    buf[s.size()] = 0;
    return true;
}

static BOOL file_dialog(OPENFILENAMEW* ofn, bool save)
{
    g_cderr = 0;
    if (!ofn || (ofn->lStructSize != sizeof(OPENFILENAMEW) && ofn->lStructSize != OPENFILENAME_SIZE_VERSION_400W)) {
        g_cderr = CDERR_STRUCTSIZE;
        return FALSE;
    }
    if (!ofn->lpstrFile || ofn->nMaxFile < 2) { g_cderr = FNERR_BUFFERTOOSMALL; return FALSE; }
    if (!filedlg::run) { g_cderr = CDERR_INITIALIZATION; return FALSE; }

    DWORD fl = ofn->Flags;
    filedlg::Options o;
    o.mode = save ? filedlg::Mode::Save : filedlg::Mode::Open;
    o.title = u8(ofn->lpstrTitle);
    o.initial_dir = u8(ofn->lpstrInitialDir);
    o.app_location = o.initial_dir;   // the Longhorn look's "App Location"
    o.default_name = u8(ofn->lpstrFile);
    o.default_ext = u8(ofn->lpstrDefExt);
    o.multiple = !save && (fl & OFN_ALLOWMULTISELECT);
    o.file_must_exist = !save && (fl & OFN_FILEMUSTEXIST);
    o.path_must_exist = (fl & (OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST)) != 0;
    o.overwrite_prompt = save && (fl & OFN_OVERWRITEPROMPT);
    o.create_prompt = !save && (fl & OFN_CREATEPROMPT);
    o.show_read_only = !(fl & OFN_HIDEREADONLY);
    o.read_only = fl & OFN_READONLY;
    o.show_hidden = fl & OFN_FORCESHOWHIDDEN;
    bool custom = ofn->lpstrCustomFilter && ofn->nMaxCustFilter && ofn->lpstrCustomFilter[0] | ofn->lpstrCustomFilter[utf16_len(ofn->lpstrCustomFilter) + 1];
    if (custom) {
        const WCHAR* c = ofn->lpstrCustomFilter;
        filedlg::Filter f;
        f.name = utf8_of(c);
        std::string pats = utf8_of(c + utf16_len(c) + 1), cur;
        for (char ch : pats + ";") {
            if (ch == ';' || ch == ' ') { if (!cur.empty()) f.patterns.push_back(cur); cur.clear(); }
            else cur += ch;
        }
        if (f.name.empty()) f.name = pats;
        o.filters.push_back(f);
    }
    for (auto& f : parse_filters(ofn->lpstrFilter)) o.filters.push_back(f);
    if (ofn->nFilterIndex == 0) o.filter_index = 0;
    else o.filter_index = (int)ofn->nFilterIndex - 1 + (custom ? 1 : 0);

    filedlg::Result r = filedlg::run(ofn->hwndOwner, o);
    if (!r.ok || r.paths.empty()) return FALSE;

    if (r.filter_index >= 0) {
        if (custom && r.filter_index == 0) ofn->nFilterIndex = 0;
        else ofn->nFilterIndex = (DWORD)(r.filter_index - (custom ? 1 : 0) + 1);
    }
    if (r.read_only) ofn->Flags |= OFN_READONLY; else ofn->Flags &= ~OFN_READONLY;
    ofn->Flags &= ~OFN_EXTENSIONDIFFERENT;
    std::string path = r.paths[0];
    std::string dir;
    if (r.paths.size() > 1) {
        // Explorer-style multi-select: the folder, then the names, double-NUL terminated
        dir = common_dir(r.paths);
        std::u16string s = utf16_of(dir.c_str());
        s.push_back(0);
        for (const std::string& p : r.paths) { s += utf16_of(p.substr(dir == "/" ? 1 : dir.size() + 1).c_str()); s.push_back(0); }
        if (s.size() + 1 > ofn->nMaxFile) {
            g_cderr = FNERR_BUFFERTOOSMALL;
            ofn->lpstrFile[0] = (WCHAR)(s.size() + 1);   // required size, as Windows reports it
            ofn->lpstrFile[1] = (WCHAR)((s.size() + 1) >> 16);
            return FALSE;
        }
        memcpy(ofn->lpstrFile, s.data(), s.size() * sizeof(WCHAR));
        ofn->lpstrFile[s.size()] = 0;
        ofn->nFileOffset = (WORD)(utf16_of(dir.c_str()).size() + 1);
        ofn->nFileExtension = 0;
    } else {
        std::u16string w = utf16_of(path.c_str());
        if (!put(ofn->lpstrFile, ofn->nMaxFile, w)) {
            g_cderr = FNERR_BUFFERTOOSMALL;
            ofn->lpstrFile[0] = (WCHAR)(w.size() + 1);
            ofn->lpstrFile[1] = (WCHAR)((w.size() + 1) >> 16);
            return FALSE;
        }
        size_t slash = w.find_last_of(u'/');
        size_t name = slash == std::u16string::npos ? 0 : slash + 1;
        size_t dot = w.find_last_of(u'.');
        ofn->nFileOffset = (WORD)name;
        ofn->nFileExtension = (WORD)(dot != std::u16string::npos && dot > name ? dot + 1 : w.size());
        if (ofn->lpstrDefExt && save) {
            // the extension differs from the default one
            std::u16string ext = dot != std::u16string::npos && dot > name ? w.substr(dot + 1) : std::u16string();
            if (ext != std::u16string((const char16_t*)ofn->lpstrDefExt)) ofn->Flags |= OFN_EXTENSIONDIFFERENT;
        }
        dir = path.substr(0, path.find_last_of('/'));
        if (dir.empty()) dir = "/";
    }
    if (ofn->lpstrFileTitle && ofn->nMaxFileTitle) {
        std::string n = path.substr(path.find_last_of('/') + 1);
        put(ofn->lpstrFileTitle, ofn->nMaxFileTitle, utf16_of(n.c_str()));
    }
    if (!(fl & OFN_NOCHANGEDIR)) { if (chdir(dir.c_str()) != 0) {} }
    return TRUE;
}

extern "C" BOOL WINAPI GetOpenFileNameW(OPENFILENAMEW* ofn) { return file_dialog(ofn, false); }
extern "C" BOOL WINAPI GetSaveFileNameW(OPENFILENAMEW* ofn) { return file_dialog(ofn, true); }

// ---- ANSI: the shim's code page is UTF-8 -------------------------------------------

static std::u16string w_list(const char* p)   // a double-NUL list
{
    std::u16string out;
    if (!p) return out;
    while (*p) { out += utf16_of(p); out.push_back(0); p += strlen(p) + 1; }
    out.push_back(0);
    return out;
}

static std::u16string w_str(const char* s) { return s ? utf16_of(s) : std::u16string(); }

static BOOL file_dialog_a(OPENFILENAMEA* a, bool save)
{
    g_cderr = 0;
    if (!a || (a->lStructSize != sizeof(OPENFILENAMEA) && a->lStructSize != OPENFILENAME_SIZE_VERSION_400A)) {
        g_cderr = CDERR_STRUCTSIZE;
        return FALSE;
    }
    if (!a->lpstrFile || a->nMaxFile < 2) { g_cderr = FNERR_BUFFERTOOSMALL; return FALSE; }
    OPENFILENAMEW w;
    memset(&w, 0, sizeof w);
    w.lStructSize = sizeof w;
    w.hwndOwner = a->hwndOwner;
    w.hInstance = a->hInstance;
    std::u16string filter = w_list(a->lpstrFilter);
    w.lpstrFilter = a->lpstrFilter ? (LPCWSTR)filter.c_str() : nullptr;
    std::u16string custom;
    if (a->lpstrCustomFilter && a->nMaxCustFilter) {
        custom = utf16_of(a->lpstrCustomFilter);
        custom.push_back(0);
        custom += utf16_of(a->lpstrCustomFilter + strlen(a->lpstrCustomFilter) + 1);
        custom.push_back(0);
        custom.resize(std::max<size_t>(custom.size(), a->nMaxCustFilter));
        w.lpstrCustomFilter = (LPWSTR)&custom[0];
        w.nMaxCustFilter = (DWORD)custom.size();
    }
    w.nFilterIndex = a->nFilterIndex;
    std::u16string file = utf16_of(a->lpstrFile);
    file.resize(std::max<size_t>(file.size() + 1, (size_t)a->nMaxFile * 4));
    w.lpstrFile = (LPWSTR)&file[0];
    w.nMaxFile = (DWORD)file.size();
    std::u16string title_buf;
    if (a->lpstrFileTitle && a->nMaxFileTitle) { title_buf.resize((size_t)a->nMaxFileTitle * 4); w.lpstrFileTitle = (LPWSTR)&title_buf[0]; w.nMaxFileTitle = (DWORD)title_buf.size(); }
    std::u16string idir = w_str(a->lpstrInitialDir), title = w_str(a->lpstrTitle), defext = w_str(a->lpstrDefExt);
    w.lpstrInitialDir = a->lpstrInitialDir ? (LPCWSTR)idir.c_str() : nullptr;
    w.lpstrTitle = a->lpstrTitle ? (LPCWSTR)title.c_str() : nullptr;
    w.lpstrDefExt = a->lpstrDefExt ? (LPCWSTR)defext.c_str() : nullptr;
    w.Flags = a->Flags;
    w.lCustData = a->lCustData;
    BOOL ok = file_dialog(&w, save);
    a->nFilterIndex = w.nFilterIndex;
    a->Flags = w.Flags;
    if (!ok) {
        if (g_cderr == FNERR_BUFFERTOOSMALL && a->nMaxFile >= 2) { a->lpstrFile[0] = 0; }
        return FALSE;
    }
    // back to UTF-8: one string, or the double-NUL multi-select list
    std::string out;
    const WCHAR* p = w.lpstrFile;
    bool multi = (a->Flags & OFN_ALLOWMULTISELECT) && w.nFileOffset && p[w.nFileOffset - 1] == 0;
    WORD name_off = 0, ext_off = 0;
    if (multi) {
        while (*p) { out += utf8_of(p); out.push_back(0); p += utf16_len(p) + 1; }
        name_off = (WORD)(strlen(out.c_str()) + 1);
    } else {
        std::string s = utf8_of(p);
        out = s;
        out.push_back(0);
        size_t slash = s.find_last_of('/'), dot = s.find_last_of('.');
        name_off = (WORD)(slash == std::string::npos ? 0 : slash + 1);
        ext_off = (WORD)(dot != std::string::npos && dot > name_off ? dot + 1 : s.size());
    }
    if (out.size() + 1 > a->nMaxFile) { g_cderr = FNERR_BUFFERTOOSMALL; *(WORD*)a->lpstrFile = (WORD)(out.size() + 1); return FALSE; }
    memcpy(a->lpstrFile, out.data(), out.size());
    a->lpstrFile[out.size()] = 0;
    a->nFileOffset = name_off;
    a->nFileExtension = ext_off;
    if (a->lpstrFileTitle && a->nMaxFileTitle) {
        std::string t = utf8_of(w.lpstrFileTitle);
        size_t n = std::min(t.size(), (size_t)a->nMaxFileTitle - 1);
        memcpy(a->lpstrFileTitle, t.data(), n);
        a->lpstrFileTitle[n] = 0;
    }
    return TRUE;
}

extern "C" BOOL WINAPI GetOpenFileNameA(OPENFILENAMEA* ofn) { return file_dialog_a(ofn, false); }
extern "C" BOOL WINAPI GetSaveFileNameA(OPENFILENAMEA* ofn) { return file_dialog_a(ofn, true); }
