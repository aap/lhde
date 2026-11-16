// A slow folder's listing (shell/namespace.h enumerate_names, stat_entry; shell/lister.h) on a scratch folder: what the
// names alone give and what the lister fills in add up to what enumerate gives (folders, links to folders, a dangling
// link, sizes, dates, a read-only file), the stored properties read when asked (where the file system has user xattrs),
// a new start drops the last one's results, and every entry comes back once; the Reader's names (type by name), a folder
// that is not there, a cancelled request never delivered.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include "liblhde/compat.h"
#include "shell/details.h"
#include "shell/lister.h"
#include "shell/namespace.h"

using namespace shell;

static int fails, checks;
static void check(const char* what, const std::string& want, const std::string& got)
{
    checks++;
    if (want == got) return;
    fails++;
    printf("FAIL %s: expected [%s] got [%s]\n", what, want.c_str(), got.c_str());
}

static std::mutex g_lock;
static std::condition_variable g_cv;
static int g_wakes;
static void notify(void*) { std::lock_guard<std::mutex> l(g_lock); g_wakes++; g_cv.notify_all(); }

// everything the lister fills in until it is idle (or 10 s)
static std::vector<Lister::Filled> drain(Lister& l)
{
    std::vector<Lister::Filled> all, got;
    for (int i = 0; i < 1000; i++) {
        {
            std::unique_lock<std::mutex> k(g_lock);
            g_cv.wait_for(k, std::chrono::milliseconds(10));
        }
        if (l.take(got)) all.insert(all.end(), got.begin(), got.end());
        if (!l.busy() && !l.take(got)) break;
        all.insert(all.end(), got.begin(), got.end());
    }
    return all;
}

static std::string facts(const Item& it)
{
    char b[128];
    snprintf(b, sizeof b, "%s%s%s%s %llu %lld", it.is_folder() ? "d" : "-", it.flags & IsLink ? "l" : "-",
             it.flags & IsReadOnly ? "r" : "-", it.flags & IsHidden ? "h" : "-", (unsigned long long)it.size, (long long)it.mtime);
    return it.name + " " + b;
}

int main()
{
    char tmpl[] = "/tmp/listertest.XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 1; }
    std::string d = dir;
    auto put = [&](const char* name, const char* text) { FILE* f = fopen((d + "/" + name).c_str(), "w"); fputs(text, f); fclose(f); };
    put("a.txt", "hello");
    put("b.jpg", "not really a picture, but named as one");
    put("ro.txt", "x");
    chmod((d + "/ro.txt").c_str(), 0444);
    mkdir((d + "/sub").c_str(), 0755);
    symlink("sub", (d + "/tosub").c_str());
    symlink("nowhere", (d + "/dangling").c_str());
    put(".hidden", "h");
    struct timeval tv[2] = { { 1000000000, 0 }, { 1000000000, 0 } };
    utimes((d + "/a.txt").c_str(), tv);

    auto f = folder_for(d);
    if (!f) { printf("FAIL no folder\n"); return 1; }
    check("a local folder is not slow", "0", f->slow() ? "1" : "0");
    std::vector<Item> full, names;
    f->enumerate(full, true);
    f->enumerate_names(names, true);
    check("a local folder's names are its full listing", std::to_string(full.size()), std::to_string(names.size()));
    std::map<std::string, Item> want;
    for (const Item& it : full) want[it.name] = it;

    // stat_entry on bare names: what enumerate gives
    for (const Item& w : full) {
        Item it;
        it.name = w.name;
        it.path = w.path;
        it.flags = (w.flags & IsHidden) | IsSlow | IsPending;
        bool ok = stat_entry(it);
        check(("stat_entry " + w.name).c_str(), "1", ok ? "1" : "0");
        check(("stat_entry " + w.name + " facts").c_str(), facts(w), facts(it));
        check(("stat_entry " + w.name + " not pending").c_str(), "0", it.flags & IsPending ? "1" : "0");
    }
    Item gone;
    gone.name = "gone"; gone.path = d + "/gone";
    check("stat_entry of what is not there", "0", stat_entry(gone) ? "1" : "0");

    // the lister: every pending entry back once, with its stat
    std::vector<Item> pending;
    for (const Item& w : full) {
        Item it;
        it.name = w.name; it.path = w.path; it.flags = (w.flags & IsHidden) | IsSlow | IsPending;
        pending.push_back(it);
    }
    pending.push_back(gone);
    pending.back().flags = IsSlow | IsPending;
    Lister lister(notify, nullptr, 3);
    lister.start(d, pending, true, false);
    std::vector<Lister::Filled> got = drain(lister);
    check("every entry filled once", std::to_string(pending.size()), std::to_string(got.size()));
    std::map<std::string, int> seen;
    for (const Lister::Filled& x : got) {
        seen[x.item.name]++;
        if (x.item.name == "gone") { check("the entry gone", "1", x.gone ? "1" : "0"); continue; }
        check(("lister " + x.item.name).c_str(), facts(want[x.item.name]), facts(x.item));
        check(("lister " + x.item.name + " stat").c_str(), "1", x.stat ? "1" : "0");
    }
    for (auto& kv : seen) check(("once: " + kv.first).c_str(), "1", std::to_string(kv.second));
    check("idle", "0", lister.busy() ? "1" : "0");
    check("woken", "1", g_wakes > 0 ? "1" : "0");

    // a new start drops what the last one queued: only the second folder's entries come back
    std::vector<Item> many;
    for (int i = 0; i < 400; i++) { Item it; it.name = "x"; it.path = d + "/a.txt"; it.flags = IsSlow | IsPending; many.push_back(it); it.path = d + "/b.jpg"; many.push_back(it); }
    lister.start(d, many, true, false);
    lister.start(d, std::vector<Item>(pending.begin(), pending.begin() + 2), true, false);
    got = drain(lister);
    check("the earlier start dropped", "2", std::to_string(got.size()));

    // the stored properties, where user xattrs can be set here
    if (lhde_xattr_set((d + "/a.txt").c_str(), "user.xdg.comment", "a note", 6) == 0) {
        std::vector<Item> one(1, want["a.txt"]);
        lister.add(one, false, true);
        got = drain(lister);
        check("stored read", "1", got.size() == 1 && got[0].stored ? "1" : "0");
        std::string c;
        if (!got.empty()) c = got[0].item.stored("user.xdg.comment");
        check("the comment", "a note", c);
        check("IsStored", "1", !got.empty() && (got[0].item.flags & IsStored) ? "1" : "0");
    } else printf("note: no user xattrs in /tmp, the stored properties not checked\n");

    // the Reader: the names (type and icon by the name), a folder that is not there, a request cancelled before its
    // answer is never delivered
    {
        Reader reader(notify, nullptr);
        uint32_t t1 = reader.read(d, false), t2 = reader.read(d + "/nothere", false), t3 = reader.read(d, true);
        reader.cancel(t3);
        std::vector<Reader::Result> res, part;
        for (int i = 0; i < 500 && res.size() < 2; i++) {
            {
                std::unique_lock<std::mutex> k(g_lock);
                g_cv.wait_for(k, std::chrono::milliseconds(10));
            }
            if (reader.take(part)) res.insert(res.end(), part.begin(), part.end());
        }
        usleep(100000);
        if (reader.take(part)) res.insert(res.end(), part.begin(), part.end());
        check("reader: two answers", "2", std::to_string(res.size()));
        for (const Reader::Result& r : res) {
            if (r.ticket == t1) {
                std::map<std::string, const Item*> by;
                for (const Item& it : r.items) by[it.name] = &it;
                check("reader: read", "1", r.ok ? "1" : "0");
                check("reader: no hidden entry", "0", std::to_string(by.count(".hidden")));
                check("reader: a.txt's type by its name", "Text Document", by.count("a.txt") ? by["a.txt"]->type : "");
                check("reader: sub a folder (d_type), pending", "1",
                      by.count("sub") && by["sub"]->is_folder() && (by["sub"]->flags & IsPending) ? "1" : "0");
            } else if (r.ticket == t2) {
                check("reader: not there", "0", r.ok ? "1" : "0");
                check("reader: ENOENT", std::to_string(ENOENT), std::to_string(r.error));
            } else check("reader: the cancelled one delivered", "", std::to_string(r.ticket));
        }
    }

    chmod((d + "/ro.txt").c_str(), 0644);
    for (const char* n : { "a.txt", "b.jpg", "ro.txt", "tosub", "dangling", ".hidden" }) unlink((d + "/" + n).c_str());
    rmdir((d + "/sub").c_str());
    rmdir(dir);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
