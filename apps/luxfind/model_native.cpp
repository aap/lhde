// luxfind's model (model.h): the threads of a search, the reachability checks, the commits and the deferred edits.
#include "model.h"
#include <errno.h>
#include <sqlite3.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
extern "C" {
#include "liblhcat/lhcat.h"
}

namespace luxfind {

// ---- roots ------------------------------------------------------------------------------------------------------------

static void root_cb(void* ctx, const char* name, const char* path, const char*)
{
    static_cast<std::vector<Root>*>(ctx)->push_back(Root{ name, path, false, false });
}

std::vector<Root> roots()
{
    std::vector<Root> rs;
    lhcat_roots(root_cb, &rs);
    for (Root& r : rs) {   // the authority and offline flag are the catalogue's own
        char err[256];
        if (lhcat* c = lhcat_open(r.name.c_str(), err, sizeof err)) {
            r.catalogue = lhcat_catalogue_authority(c) != 0;
            r.offline = lhcat_offline(c) != 0;
            if (*lhcat_root_path(c)) r.path = lhcat_root_path(c);
            lhcat_close(c);
        }
    }
    return rs;
}

const Root* root_for(const std::vector<Root>& rs, const std::string& path, std::string* rel)
{
    const Root* best = nullptr;
    for (const Root& r : rs) {
        const std::string& p = r.path;
        bool under = p == "/" ? !path.empty() && path[0] == '/'
                              : path.compare(0, p.size(), p) == 0 && (path.size() == p.size() || path[p.size()] == '/');
        if (under && (!best || p.size() > best->path.size())) best = &r;
    }
    if (best && rel) {
        size_t k = best->path == "/" ? 1 : best->path.size() + 1;
        *rel = path.size() > k ? path.substr(k) : "";
    }
    return best;
}

static std::string join(const std::string& root, const std::string& rel)
{
    if (rel.empty()) return root;
    return root == "/" ? "/" + rel : root + "/" + rel;
}

// ---- a search ---------------------------------------------------------------------------------------------------------

// the type choice as the catalogue's query takes it: folders a kind, the rest a type list
static void type_of(const std::string& t, lhcat_query& q, std::string& keep)
{
    if (t == "folders") { q.kind = 'd'; return; }
    keep = t;
    q.type = keep.empty() ? nullptr : keep.c_str();
}

struct Search::Impl {
    Notify notify;
    void* ctx;
    int page;
    std::mutex m;
    std::condition_variable cv;
    Query q;
    uint32_t gen = 0;                     // the generation asked for
    bool quit = false;
    int64_t allowed = 0;                  // rows the lister may give in this generation
    volatile int stop_list = 0, stop_count = 0;
    std::vector<Row> rows;                // delivered, not taken
    Status st;
    std::thread lister, counter;
    std::map<std::string, lhcat*> list_handles, count_handles;
    std::map<std::string, bool> indexed;  // the name index brought up to date this run

    void post() { if (notify) notify(ctx); }

    static lhcat* handle(std::map<std::string, lhcat*>& hs, const std::string& name)
    {
        auto it = hs.find(name);
        if (it != hs.end()) return it->second;
        char err[256];
        lhcat* c = lhcat_open(name.c_str(), err, sizeof err);
        if (c) lhcat_set_program(c, "luxfind");
        return hs[name] = c;
    }

    std::vector<std::string> root_names(const Query& qq)
    {
        if (!qq.root.empty()) return { qq.root };
        std::vector<std::string> out;
        for (const Root& r : roots()) out.push_back(r.name);
        return out;
    }

    struct Ctx { Impl* s; uint32_t gen; std::string root, root_path; std::vector<Row> batch; int64_t given = 0;
                 std::chrono::steady_clock::time_point last; };

    static void hit(void* ctx, const lhcat_hit* h)
    {
        Ctx* x = static_cast<Ctx*>(ctx);
        Row r;
        r.root = x->root;
        r.rel = h->path;
        r.path = join(x->root_path, h->path);
        r.kind = h->kind;
        r.size = h->size;
        r.mtime = h->mtime;
        r.type = h->type ? h->type : "";
        for (int i = 0; i < h->nattrs; i++) r.attrs.push_back({ h->attrs[2 * i], h->attrs[2 * i + 1] });
        r.pending = h->pending;
        r.conflicts = h->conflicts;
        x->batch.push_back(std::move(r));
        x->given++;
        auto now = std::chrono::steady_clock::now();
        bool flush = x->batch.size() >= 500 || now - x->last > std::chrono::milliseconds(60);
        Impl* s = x->s;
        std::unique_lock<std::mutex> l(s->m);
        if (s->gen != x->gen) { s->stop_list = 1; return; }
        bool full = s->st.delivered + (int64_t)x->batch.size() >= s->allowed;
        if (flush || full) {
            s->st.delivered += (int64_t)x->batch.size();
            for (Row& rr : x->batch) s->rows.push_back(std::move(rr));
            x->batch.clear();
            x->last = now;
            if (full) s->st.listing = false;
            l.unlock();
            s->post();
            l.lock();
        }
        // a page given: wait for more() or another generation (the statement stays open, a read of the snapshot)
        while (s->gen == x->gen && !s->quit && s->st.delivered >= s->allowed) s->cv.wait(l);
        if (s->gen != x->gen || s->quit) s->stop_list = 1;
        else s->st.listing = true;
    }

    void list_loop()
    {
        uint32_t done_gen = 0;
        for (;;) {
            Query qq;
            uint32_t g;
            {
                std::unique_lock<std::mutex> l(m);
                cv.wait(l, [&] { return quit || gen != done_gen; });
                if (quit) return;
                g = gen;
                qq = q;
                stop_list = 0;
            }
            std::string error;
            for (const std::string& name : root_names(qq)) {
                lhcat* c = handle(list_handles, name);
                if (!c) { error = "The catalogue " + name + " cannot be opened."; continue; }
                if (!qq.name.empty() && !indexed[name]) {   // the names catalogued since the last import or index
                    int64_t left = 0;
                    lhcat_names_update(c, 1, &left);
                    if (left > 0) {
                        { std::lock_guard<std::mutex> l(m); st.note = "Indexing the names of " + name + "..."; }
                        post();
                        lhcat_names_update(c, 0, nullptr);
                        { std::lock_guard<std::mutex> l(m); st.note.clear(); }
                    }
                    indexed[name] = true;
                }
                lhcat_query lq = {};
                std::string type_keep;
                type_of(qq.type, lq, type_keep);
                lq.name = qq.name.empty() ? nullptr : qq.name.c_str();
                lq.tag = qq.tags.empty() ? nullptr : qq.tags.c_str();
                lq.in = qq.in.empty() ? nullptr : qq.in.c_str();
                lq.stop = &stop_list;
                lq.with_attrs = 1;
                Ctx x{ this, g, name, lhcat_root_path(c), {}, 0, std::chrono::steady_clock::now() };
                int64_t n = lhcat_find(c, &lq, hit, &x, nullptr, nullptr);
                if (n < 0 && !stop_list) error = std::string(name) + ": " + lhcat_error(c);
                std::unique_lock<std::mutex> l(m);
                if (!x.batch.empty() && gen == g) {
                    st.delivered += (int64_t)x.batch.size();
                    for (Row& rr : x.batch) rows.push_back(std::move(rr));
                }
                if (gen != g || quit) break;
            }
            {
                std::lock_guard<std::mutex> l(m);
                done_gen = g;
                if (gen == g) { st.done = true; st.listing = false; if (!error.empty()) st.error = error; }
            }
            post();
        }
    }

    void count_loop()
    {
        uint32_t done_gen = 0;
        for (;;) {
            Query qq;
            uint32_t g;
            {
                std::unique_lock<std::mutex> l(m);
                cv.wait(l, [&] { return quit || gen != done_gen; });
                if (quit) return;
                g = gen;
                qq = q;
                stop_count = 0;
            }
            int64_t total = 0;
            bool ok = true;
            for (const std::string& name : root_names(qq)) {
                lhcat* c = handle(count_handles, name);
                if (!c) continue;
                lhcat_query lq = {};
                std::string type_keep;
                type_of(qq.type, lq, type_keep);
                lq.name = qq.name.empty() ? nullptr : qq.name.c_str();
                lq.tag = qq.tags.empty() ? nullptr : qq.tags.c_str();
                lq.in = qq.in.empty() ? nullptr : qq.in.c_str();
                lq.stop = &stop_count;
                int64_t n = lhcat_find(c, &lq, nullptr, nullptr, nullptr, nullptr);
                if (stop_count) { ok = false; break; }
                if (n > 0) total += n;
            }
            {
                std::lock_guard<std::mutex> l(m);
                done_gen = g;
                if (ok && gen == g) st.count = total;
            }
            if (ok) post();
        }
    }
};

Search::Search(Notify notify, void* ctx, int page) : impl_(new Impl)
{
    impl_->notify = notify;
    impl_->ctx = ctx;
    impl_->page = page;
    impl_->lister = std::thread([this] { impl_->list_loop(); });
    impl_->counter = std::thread([this] { impl_->count_loop(); });
}

Search::~Search()
{
    {
        std::lock_guard<std::mutex> l(impl_->m);
        impl_->quit = true;
        impl_->stop_list = impl_->stop_count = 1;
    }
    impl_->cv.notify_all();
    impl_->lister.join();
    impl_->counter.join();
    for (auto& kv : impl_->list_handles) if (kv.second) lhcat_close(kv.second);
    for (auto& kv : impl_->count_handles) if (kv.second) lhcat_close(kv.second);
    delete impl_;
}

void Search::start(const Query& q)
{
    {
        std::lock_guard<std::mutex> l(impl_->m);
        impl_->gen++;
        impl_->q = q;
        impl_->stop_list = impl_->stop_count = 1;   // the running ones end at their next row
        impl_->allowed = impl_->page;
        impl_->rows.clear();
        impl_->st = Status();
        impl_->st.gen = impl_->gen;
        impl_->st.listing = true;
    }
    impl_->cv.notify_all();
}

void Search::more()
{
    {
        std::lock_guard<std::mutex> l(impl_->m);
        if (impl_->st.done || impl_->allowed > impl_->st.delivered) return;
        impl_->allowed = impl_->st.delivered + impl_->page;
        impl_->st.listing = true;
    }
    impl_->cv.notify_all();
}

void Search::stop()
{
    {
        std::lock_guard<std::mutex> l(impl_->m);
        impl_->gen++;
        impl_->stop_list = impl_->stop_count = 1;
        impl_->rows.clear();
        impl_->st = Status();
        impl_->st.gen = impl_->gen;
        impl_->st.done = true;
    }
    impl_->cv.notify_all();
}

void Search::take(std::vector<Row>& rows, Status& st)
{
    std::lock_guard<std::mutex> l(impl_->m);
    rows.swap(impl_->rows);
    impl_->rows.clear();
    st = impl_->st;
}

// ---- reachability -------------------------------------------------------------------------------------------------------

struct Reach::Impl {
    void (*notify)(void*);
    void* ctx;
    int timeout_ms, keep_ms;
    struct Entry {
        State state = Unknown;
        std::string why;
        std::chrono::steady_clock::time_point at;   // the check's start (Checking) or its answer
        std::shared_ptr<int> running;                // a check thread is out (it may hang)
    };
    std::mutex m;
    std::map<std::string, Entry> e;
};

Reach::Reach(void (*notify)(void*), void* ctx, int timeout_ms, int keep_ms) : impl_(new Impl)
{
    impl_->notify = notify;
    impl_->ctx = ctx;
    impl_->timeout_ms = timeout_ms;
    impl_->keep_ms = keep_ms;
}

Reach::~Reach() { impl_->notify = nullptr; /* a hung check keeps its own copy of what it needs */ }

Reach::State Reach::state(const std::string& root, const std::string& path, bool force, std::string* why)
{
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> l(impl_->m);
    Impl::Entry& en = impl_->e[root];
    if (en.state == Checking && now - en.at > std::chrono::milliseconds(impl_->timeout_ms)) {
        en.state = Unreachable;
        en.why = "no answer in " + std::to_string(impl_->timeout_ms / 1000) + " s";
        en.at = now;
    }
    bool stale = en.state == Unknown || force || (en.state != Checking && now - en.at > std::chrono::milliseconds(impl_->keep_ms));
    if (stale && !(en.running && *en.running)) {   // (never a second thread while one hangs)
        en.state = Checking;
        en.at = now;
        auto flag = std::make_shared<int>(1);
        en.running = flag;
        Impl* im = impl_;
        std::string r = root, p = path;
        std::thread([im, r, p, flag] {
            struct stat st;
            int rc = stat(p.c_str(), &st);
            int e = errno;
            void (*notify)(void*) = nullptr;
            void* ctx = nullptr;
            {
                std::lock_guard<std::mutex> l(im->m);
                *flag = 0;
                Impl::Entry& en = im->e[r];
                en.state = rc == 0 && S_ISDIR(st.st_mode) ? Reachable : Unreachable;
                en.why = rc != 0 ? strerror(e) : S_ISDIR(st.st_mode) ? "" : "not a directory";
                en.at = std::chrono::steady_clock::now();
                notify = im->notify;
                ctx = im->ctx;
            }
            if (notify) notify(ctx);
        }).detach();
    }
    if (why) *why = en.why;
    return en.state;
}

void Reach::forget(const std::string& root)
{
    std::lock_guard<std::mutex> l(impl_->m);
    Impl::Entry& en = impl_->e[root];
    if (en.state != Checking) en.state = Unknown;
}

// ---- commits --------------------------------------------------------------------------------------------------------------

void commit(const std::string& root, void (*done)(void*, const std::string&, bool, const std::string&), void* ctx)
{
    std::thread([root, done, ctx] {
        char err[256];
        lhcat* c = lhcat_open(root.c_str(), err, sizeof err);
        if (!c) { done(ctx, root, false, err); return; }
        lhcat_set_program(c, "luxfind");
        uint64_t written = 0, conflicts = 0;
        int rc = lhcat_commit(c, nullptr, &written, &conflicts);
        std::string text;
        bool ok = rc == 0 && conflicts == 0;
        if (rc != 0) text = std::string("The commit of ") + root + " failed: " + lhcat_error(c);
        else {
            text = std::to_string(written) + (written == 1 ? " value" : " values") + " written to the files of " + root;
            if (conflicts) text += "; " + std::to_string(conflicts) + (conflicts == 1 ? " conflict" : " conflicts") +
                                   " (the file changed): see lhde-cat conflicts " + root;
            text += ".";
        }
        lhcat_close(c);
        done(ctx, root, ok, text);
    }).detach();
}

// ---- the edits ----------------------------------------------------------------------------------------------------------------

static std::map<std::string, lhcat*> g_ui;

static lhcat* ui_handle(const std::string& root)
{
    auto it = g_ui.find(root);
    if (it != g_ui.end()) return it->second;
    char err[256];
    lhcat* c = lhcat_open(root.c_str(), err, sizeof err);
    if (c) lhcat_set_program(c, "luxfind");
    return g_ui[root] = c;
}

void close_handles()
{
    for (auto& kv : g_ui) if (kv.second) lhcat_close(kv.second);
    g_ui.clear();
}

bool desired(const std::string& root, const std::string& rel, std::vector<std::pair<std::string, std::string>>* attrs,
             int* pending, int* conflicts)
{
    lhcat* c = ui_handle(root);
    if (!c) return false;
    int64_t id = lhcat_id(c, rel.c_str()), obj = id ? lhcat_object(c, id) : 0;
    if (!obj) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(lhcat_db(c), "SELECT name, desired, pending FROM attrs WHERE object_id = ?1", -1, &st, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, obj);
    if (attrs) attrs->clear();
    int p = 0, k = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char* pend = (const char*)sqlite3_column_text(st, 2);
        if (pend && !strcmp(pend, "write")) p++;
        else if (pend && !strcmp(pend, "conflict")) k++;
        const char* v = (const char*)sqlite3_column_text(st, 1);
        if (v && attrs) attrs->push_back({ (const char*)sqlite3_column_text(st, 0), v });
    }
    sqlite3_finalize(st);
    if (pending) *pending = p;
    if (conflicts) *conflicts = k;
    return true;
}

static bool apply(const std::string& root, const std::string& rel, int op, const std::string& xattr, const std::string& value,
                  std::string* err)
{
    lhcat* c = ui_handle(root);
    if (!c) { if (err) *err = "The catalogue " + root + " cannot be opened."; return false; }
    lhcat_op o = {};
    o.op = op;
    o.attr = xattr.c_str();
    o.value = value.c_str();
    o.base_rev = -1;
    int status = 0;
    char id[64];
    if (lhcat_apply(c, rel.c_str(), &o, 0, &status, id) != 0) {
        if (err) *err = std::string("The catalogue ") + root + " did not take the change: " + lhcat_error(c);
        return false;
    }
    if ((status & 15) == LHCAT_CONFLICT) { if (err) *err = "The change is in conflict with another one: see lhde-cat conflicts " + root + "."; return false; }
    return true;
}

bool defer_set(const std::string& root, const std::string& rel, const std::string& xattr, const std::string& value, std::string* err)
{
    return apply(root, rel, value.empty() ? LHCAT_UNSET : LHCAT_SET, xattr, value, err);
}

bool defer_tags(const std::string& root, const std::string& rel, const std::vector<std::string>& add,
                const std::vector<std::string>& remove, std::string* err)
{
    for (const std::string& t : add) if (!apply(root, rel, LHCAT_TAG_ADD, "user.xdg.tags", t, err)) return false;
    for (const std::string& t : remove) if (!apply(root, rel, LHCAT_TAG_REMOVE, "user.xdg.tags", t, err)) return false;
    return true;
}

int64_t pending_count(const std::string& root)
{
    lhcat* c = ui_handle(root);
    if (!c) return -1;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(lhcat_db(c), "SELECT count(*) FROM attrs WHERE pending != 'none'", -1, &st, nullptr) != SQLITE_OK) return -1;
    int64_t n = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : -1;
    sqlite3_finalize(st);
    return n;
}

std::vector<std::string> catalogue_tags(const std::vector<Root>& rs, int max)
{
    std::map<std::string, int64_t> n;
    for (const Root& r : rs) {
        lhcat* c = ui_handle(r.name);
        sqlite3_stmt* st = nullptr;
        if (!c || sqlite3_prepare_v2(lhcat_db(c), "SELECT tag, count(*) FROM tags GROUP BY tag", -1, &st, nullptr) != SQLITE_OK)
            continue;
        while (sqlite3_step(st) == SQLITE_ROW) n[(const char*)sqlite3_column_text(st, 0)] += sqlite3_column_int64(st, 1);
        sqlite3_finalize(st);
    }
    std::vector<std::pair<int64_t, std::string>> v;
    for (auto& kv : n) v.push_back({ -kv.second, kv.first });
    std::sort(v.begin(), v.end());
    std::vector<std::string> out;
    for (auto& p : v) { if ((int)out.size() >= max) break; out.push_back(p.second); }
    return out;
}

} // namespace luxfind
