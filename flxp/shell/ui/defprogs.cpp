// shell::default_programs (shell/ui.h): XP's Set Program Access and Defaults over shell/roles.h, from the embedded
// flxp/rc/shell.res (IDD_DEFPROGS). XP lists, under its Custom configuration, one group per kind of program ("Choose a
// default Web browser:", the installed browsers as radio buttons, "Use my current Web browser"); here the roles are in a
// list on the left and the selected role's group is on the right. A choice is pending until OK or Apply, which write
// settings.ini [programs] (shell::store) and the role file (shell::apply_roles) at once.
#include "res/res.h"   // before the win32 headers: their macros clash with its names
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "shell/apps.h"
#include "shell/roles.h"
#include "shell/settings.h"
#include "w32/w32.h"
extern "C" {
#include "commctrl.h"
}
#include "shell/ui.h"
#include "resource.h"

namespace shell {

HINSTANCE ui_module();      // properties.cpp: flxp/rc/shell.res

namespace {

std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
void set_text(HWND dlg, int id, const std::string& s) { SetDlgItemTextW(dlg, id, (LPCWSTR)W(s).c_str()); }

struct RoleRow {
    Role role;
    std::vector<std::string> types;
    std::vector<std::string> ids;       // the radio buttons' programs after "Use my current ...": desktop ids
    std::string checked;                // "none" (Use my current), else a desktop id
    std::string initial;
};

struct State {
    std::vector<RoleRow> rows;
    int cur = -1;
    std::string first;
    bool applied = false;
    bool filling = false;
};
State* S;

bool trace() { return getenv("FLXP_TRACE_DEFPROGS") != nullptr; }

std::string escape_amp(const std::string& s)
{
    std::string r;
    for (char c : s) { if (c == '&') r += '&'; r += c; }
    return r;
}

void load_rows()
{
    S->rows.clear();
    std::vector<Role> rs = roles();
    auto sets = role_types(rs);
    for (const Role& r : rs) {
        RoleRow row;
        row.role = r;
        row.types = sets[r.id];
        RoleChoice c = role_choice(r);
        for (const App* a : role_candidates(r, row.types)) row.ids.push_back(a->id);
        // the program in force stays offered when it is no candidate (another category, no claim of its own)
        if (!c.app.empty() && std::find(row.ids.begin(), row.ids.end(), c.app) == row.ids.end()) row.ids.insert(row.ids.begin(), c.app);
        if (row.ids.size() > IDC_DP_SLOTS - 1) row.ids.resize(IDC_DP_SLOTS - 1);
        row.checked = c.app.empty() ? "none" : c.app;
        row.initial = row.checked;
        S->rows.push_back(row);
    }
}

std::string app_label(const std::string& id)
{
    const App* a = find_app(id);
    return a ? (a->name.empty() ? id : a->name) : id;
}

void show_role(HWND dlg, int i)
{
    if (i < 0 || i >= (int)S->rows.size()) return;
    S->cur = i;
    S->filling = true;
    const RoleRow& row = S->rows[(size_t)i];
    set_text(dlg, IDC_DP_GROUP, row.role.label);
    set_text(dlg, IDC_DP_QUESTION, row.role.question);
    std::string def = role_default(row.role);
    for (int k = 0; k < IDC_DP_SLOTS; k++) {
        HWND b = GetDlgItem(dlg, IDC_DP_RADIO0 + k);
        std::string id = k == 0 ? "none" : (size_t)k - 1 < row.ids.size() ? row.ids[(size_t)k - 1] : "";
        bool shown = k == 0 || !id.empty();
        ShowWindow(b, shown ? SW_SHOW : SW_HIDE);
        if (shown) {
            std::string label = k == 0 ? row.role.current : escape_amp(app_label(id)) + (id == def ? " (lhde's default)" : "");
            SetWindowTextW(b, (LPCWSTR)W(label).c_str());
        }
        SendMessageW(b, BM_SETCHECK, shown && id == row.checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    // how many types, and the role's own words for them: its types= (three at most), the types under its descendants=
    std::string info;
    if (row.types.empty()) info = "No file type of this kind is known.";
    else {
        info = "Opens " + std::to_string(row.types.size()) + " file type" + (row.types.size() == 1 ? "" : "s") + ": ";
        const std::vector<std::string>& named = row.role.types;
        for (size_t k = 0; k < named.size() && k < 3; k++) info += (k ? ", " : "") + named[k];
        if (named.size() > 3) info += ", ...";
        for (const std::string& d : row.role.descendants)
            info += (named.size() == 1 && named[0] == d ? " and every type under it" : (named.empty() ? "every type under " : ", every type under ") + d);
        info += ".";
    }
    if (row.ids.empty()) info += " No installed program offers to open them.";
    set_text(dlg, IDC_DP_INFO, info);
    S->filling = false;
    if (trace()) {
        printf("defprogs: %s %s", row.role.id.c_str(), row.checked.c_str());
        for (const std::string& id : row.ids) printf(" %s", id.c_str());
        printf("\n");
        fflush(stdout);
    }
}

bool pending()
{
    for (const RoleRow& r : S->rows) if (r.checked != r.initial) return true;
    return false;
}

bool apply(HWND dlg)
{
    std::vector<store::Edit> edits;
    for (RoleRow& r : S->rows)
        if (r.checked != r.initial) edits.push_back({ "programs", r.role.id, r.checked });
    if (!edits.empty() && !store::set(edits)) {
        message_box(dlg, "Default Programs", "Cannot write " + store::path() + ".", MB_OK | MB_ICONEXCLAMATION);
        return false;
    }
    std::string err;
    if (!apply_roles(&err)) { message_box(dlg, "Default Programs", err, MB_OK | MB_ICONEXCLAMATION); return false; }
    for (RoleRow& r : S->rows) r.initial = r.checked;
    if (!edits.empty()) S->applied = true;
    EnableWindow(GetDlgItem(dlg, IDC_DP_APPLY), FALSE);
    if (trace()) { printf("defprogs: applied %zu\n", edits.size()); fflush(stdout); }
    return true;
}

INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG: {
        set_window_icon_key(dlg, "settings");
        HWND lb = GetDlgItem(dlg, IDC_DP_ROLES);
        int sel = 0;
        for (size_t i = 0; i < S->rows.size(); i++) {
            SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)W(S->rows[i].role.label).c_str());
            if (S->rows[i].role.id == S->first) sel = (int)i;
        }
        SendMessageW(lb, LB_SETCURSEL, (WPARAM)sel, 0);
        show_role(dlg, sel);
        EnableWindow(GetDlgItem(dlg, IDC_DP_APPLY), FALSE);
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_DP_ROLES && HIWORD(wp) == LBN_SELCHANGE) {
            show_role(dlg, (int)SendDlgItemMessageW(dlg, IDC_DP_ROLES, LB_GETCURSEL, 0, 0));
            return TRUE;
        }
        if (id >= IDC_DP_RADIO0 && id < IDC_DP_RADIO0 + IDC_DP_SLOTS && HIWORD(wp) == BN_CLICKED) {
            if (S->filling || S->cur < 0) return TRUE;
            RoleRow& row = S->rows[(size_t)S->cur];
            int k = id - IDC_DP_RADIO0;
            if (SendDlgItemMessageW(dlg, id, BM_GETCHECK, 0, 0) != BST_CHECKED) return TRUE;
            row.checked = k == 0 ? "none" : (size_t)k - 1 < row.ids.size() ? row.ids[(size_t)k - 1] : row.checked;
            EnableWindow(GetDlgItem(dlg, IDC_DP_APPLY), pending());
            if (trace()) { printf("defprogs: %s chosen %s\n", row.role.id.c_str(), row.checked.c_str()); fflush(stdout); }
            return TRUE;
        }
        if (id == IDC_DP_APPLY) { apply(dlg); return TRUE; }
        if (id == IDOK) { if (apply(dlg)) EndDialog(dlg, IDOK); return TRUE; }
        if (id == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        return FALSE;
    }
    }
    return FALSE;
}

} // namespace

bool default_programs(HWND owner, const std::string& role)
{
    State st;
    st.first = role;
    S = &st;
    reload_apps();
    load_rows();
    DialogBoxParamW(ui_module(), MAKEINTRESOURCEW(IDD_DEFPROGS), owner, proc, 0);
    S = nullptr;
    return st.applied;
}

} // namespace shell
