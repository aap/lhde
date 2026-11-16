// mupf's engine (engine.h): mupdf on a thread of its own. The only file that includes mupdf's headers; the thread is the
// only one that calls mupdf, so the context needs no locks. mupdf's fz_try / fz_catch are setjmp and longjmp: no object
// with a destructor lives inside one of their blocks.
#include "engine.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
extern "C" {
#include <mupdf/fitz.h>
}

namespace mupf {

namespace {
const size_t STORE_MAX = 64u << 20;    // mupdf's own store (fonts, decoded images, ...)
const int LISTS_KEPT = 8;              // the display lists kept: the pages last drawn
const int SIZES_BATCH = 32;            // page sizes read between other work
const int SEARCH_QUADS = 512;          // the rectangles one page's search reports at most
}   // namespace

struct Engine::Impl {
    Notify notify = nullptr;
    void* ctx = nullptr;
    std::mutex mu;
    std::condition_variable cv;
    std::thread th;
    // asked for (under mu)
    bool quit = false;
    bool open_pending = false, close_pending = false;
    std::string open_path, open_password;
    uint32_t gen = 0;
    std::vector<Job> jobs;
    bool search_pending = false;
    std::string needle;
    int search_from = 0;
    uint32_t search_id = 0;
    std::deque<Event> events;
    fz_cookie* running = nullptr;       // the render in progress, its job
    Job running_job;

    // the thread's own
    fz_context* fz = nullptr;
    fz_document* doc = nullptr;
    uint32_t doc_gen = 0;
    int pages = 0;
    std::vector<fz_rect> bounds;
    std::vector<char> known;
    int sizes_next = 0;
    struct List { int page; fz_display_list* list; };
    std::deque<List> lists;             // most recent first
    std::set<int> links_sent;
    // the search being done
    bool searching = false;
    std::string s_needle;
    int s_from = 0, s_done = 0;
    uint32_t s_id = 0;

    void run();
    void push(Event&& e);
    void do_open(const std::string& path, const std::string& password, uint32_t g);
    void do_close();
    bool ensure_bound(int page);
    void read_sizes();
    fz_display_list* list_for(int page, fz_cookie* ck);
    Tile* render(const Job& j, fz_cookie* ck);
    void search_step();
    void outline(fz_outline* o, int level, std::vector<TocItem>& out);
    int resolve(const char* uri, float* y);
};

static bool same_job(const Job& a, const Job& b)
{
    return a.page == b.page && a.rot == b.rot && fabs(a.zoom - b.zoom) <= b.zoom * 1e-6 && a.part.contains(b.part);
}

Engine::Engine(Notify notify, void* ctx) : d(new Impl)
{
    d->notify = notify;
    d->ctx = ctx;
    d->th = std::thread([this] { d->run(); });
}

Engine::~Engine()
{
    {
        std::lock_guard<std::mutex> lk(d->mu);
        d->quit = true;
        if (d->running) d->running->abort = 1;
        d->cv.notify_all();
    }
    d->th.join();
    for (Event& e : d->events) delete e.tile;
    delete d;
}

uint32_t Engine::open(const std::string& path, const std::string& password)
{
    std::lock_guard<std::mutex> lk(d->mu);
    d->open_pending = true;
    d->close_pending = false;
    d->open_path = path;
    d->open_password = password;
    d->jobs.clear();
    d->search_pending = false;
    if (d->running) d->running->abort = 1;
    for (Event& e : d->events) delete e.tile;
    d->events.clear();
    d->cv.notify_all();
    return ++d->gen;
}

void Engine::close()
{
    std::lock_guard<std::mutex> lk(d->mu);
    d->open_pending = false;
    d->close_pending = true;
    d->jobs.clear();
    d->search_pending = false;
    if (d->running) d->running->abort = 1;
    for (Event& e : d->events) delete e.tile;
    d->events.clear();
    ++d->gen;
    d->cv.notify_all();
}

void Engine::want(const std::vector<Job>& jobs)
{
    std::lock_guard<std::mutex> lk(d->mu);
    d->jobs.clear();
    bool running_wanted = false;
    for (const Job& j : jobs) {
        if (d->running && same_job(d->running_job, j)) { running_wanted = true; continue; }   // being drawn
        d->jobs.push_back(j);
    }
    if (d->running && !running_wanted) d->running->abort = 1;
    d->cv.notify_all();
}

uint32_t Engine::search(const std::string& needle, int from)
{
    std::lock_guard<std::mutex> lk(d->mu);
    d->search_pending = true;
    d->needle = needle;
    d->search_from = from;
    d->cv.notify_all();
    return ++d->search_id;
}

bool Engine::take(Event& e)
{
    std::lock_guard<std::mutex> lk(d->mu);
    if (d->events.empty()) return false;
    e = std::move(d->events.front());
    d->events.pop_front();
    return true;
}

void Engine::Impl::push(Event&& e)
{
    {
        std::lock_guard<std::mutex> lk(mu);
        if (e.gen != gen) { delete e.tile; return; }   // for a document since closed
        events.push_back(std::move(e));
    }
    if (notify) notify(ctx);
}

void Engine::Impl::run()
{
    fz = fz_new_context(nullptr, nullptr, STORE_MAX);
    if (fz) fz_register_document_handlers(fz);
    std::unique_lock<std::mutex> lk(mu);
    for (;;) {
        cv.wait(lk, [&] {
            return quit || open_pending || close_pending || !jobs.empty() || search_pending || (doc && sizes_next < pages) ||
                   searching;
        });
        if (quit) break;
        if (open_pending || close_pending) {
            bool opening = open_pending;
            std::string path = open_path, password = open_password;
            uint32_t g = gen;
            open_pending = close_pending = false;
            searching = false;
            lk.unlock();
            do_close();
            if (opening) do_open(path, password, g);
            lk.lock();
            continue;
        }
        if (search_pending) {
            search_pending = false;
            s_needle = needle;
            s_from = search_from;
            s_id = search_id;
            s_done = 0;
            searching = !s_needle.empty() && doc && pages > 0;
        }
        if (!jobs.empty() && doc) {
            Job j = jobs.front();
            jobs.erase(jobs.begin());
            fz_cookie ck;
            memset(&ck, 0, sizeof ck);
            running = &ck;
            running_job = j;
            uint32_t g = gen;
            lk.unlock();
            Tile* t = render(j, &ck);
            lk.lock();
            running = nullptr;
            if (t && !ck.abort && g == gen && g == doc_gen) {
                t->gen = g;
                Event e;
                e.kind = Event::RENDERED;
                e.gen = g;
                e.page = j.page;
                e.tile = t;
                events.push_back(std::move(e));
                lk.unlock();
                if (notify) notify(ctx);
                lk.lock();
            } else delete t;
            continue;
        }
        jobs.clear();
        if (doc && sizes_next < pages) {
            lk.unlock();
            read_sizes();
            lk.lock();
            continue;
        }
        if (searching) {
            lk.unlock();
            search_step();
            lk.lock();
        }
    }
    lk.unlock();
    do_close();
    if (fz) fz_drop_context(fz);
    fz = nullptr;
}

void Engine::Impl::do_close()
{
    for (List& l : lists) fz_drop_display_list(fz, l.list);
    lists.clear();
    links_sent.clear();
    if (doc) fz_drop_document(fz, doc);
    doc = nullptr;
    pages = 0;
    bounds.clear();
    known.clear();
    sizes_next = 0;
    searching = false;
}

void Engine::Impl::outline(fz_outline* o, int level, std::vector<TocItem>& out)
{
    fz_var(o);
    for (; o; o = o->next) {
        TocItem it;
        it.title = o->title ? o->title : "";
        it.level = level;
        it.open = o->is_open;
        it.page = -1;
        if (o->page.page >= 0) {
            int n = -1;
            fz_var(n);
            fz_try(fz) { n = fz_page_number_from_location(fz, doc, o->page); }
            fz_catch(fz) { n = -1; }
            it.page = n;
            it.y = o->y;
        } else if (o->uri) it.page = resolve(o->uri, &it.y);
        out.push_back(std::move(it));
        if (o->down) outline(o->down, level + 1, out);
    }
}

int Engine::Impl::resolve(const char* uri, float* y)
{
    int n = -1;
    float yy = 0;
    fz_var(n);
    if (!uri || fz_is_external_link(fz, uri)) return -1;
    fz_try(fz) {
        float x = 0;
        fz_location loc = fz_resolve_link(fz, doc, uri, &x, &yy);
        n = loc.page >= 0 ? fz_page_number_from_location(fz, doc, loc) : -1;
    }
    fz_catch(fz) { n = -1; }
    *y = yy > 0 ? yy : 0;
    return n;
}

void Engine::Impl::do_open(const std::string& path, const std::string& password, uint32_t g)
{
    Event e;
    e.gen = g;
    if (!fz) { e.kind = Event::FAILED; e.error = "mupdf could not start"; push(std::move(e)); return; }
    const char* err = nullptr;
    char msg[512] = "";
    bool locked = false;
    fz_var(err);
    fz_var(locked);
    fz_try(fz) {
        doc = fz_open_document(fz, path.c_str());
        if (fz_needs_password(fz, doc) && !fz_authenticate_password(fz, doc, password.c_str())) {
            err = password.empty() ? "The document is protected by a password." : "The password is not right.";
            locked = true;
        } else {
            pages = fz_count_pages(fz, doc);
        }
    }
    fz_catch(fz) {
        snprintf(msg, sizeof msg, "%s", fz_caught_message(fz));
        err = msg;
    }
    if (!err && pages <= 0) err = "The document has no pages.";
    if (err) {
        do_close();
        e.kind = Event::FAILED;
        e.error = err;
        e.password = locked;
        push(std::move(e));
        return;
    }
    doc_gen = g;
    bounds.assign(pages, fz_empty_rect);
    known.assign(pages, 0);
    sizes_next = 0;
    ensure_bound(0);
    e.kind = Event::OPENED;
    e.pages = pages;
    char buf[1024] = "";
    if (fz_lookup_metadata(fz, doc, FZ_META_INFO_TITLE, buf, sizeof buf) > 0) e.title = buf;
    buf[0] = 0;
    if (fz_lookup_metadata(fz, doc, FZ_META_FORMAT, buf, sizeof buf) > 0) e.format = buf;
    fz_outline* o = nullptr;
    fz_var(o);
    fz_try(fz) { o = fz_load_outline(fz, doc); }
    fz_catch(fz) { o = nullptr; }
    if (o) {
        outline(o, 0, e.toc);
        fz_drop_outline(fz, o);
    }
    // until the others are read every page is taken to be the first one's size
    SizeF s = { bounds[0].x1 - bounds[0].x0, bounds[0].y1 - bounds[0].y0 };
    if (!(s.w > 0 && s.h > 0)) s = { 612, 792 };
    e.first = 0;
    e.sizes.assign(pages, s);
    push(std::move(e));
}

bool Engine::Impl::ensure_bound(int page)
{
    if (!doc || page < 0 || page >= pages) return false;
    if (known[page]) return true;
    fz_page* p = nullptr;
    fz_rect r = fz_empty_rect;
    fz_var(p);
    fz_var(r);
    fz_try(fz) {
        p = fz_load_page(fz, doc, page);
        r = fz_bound_page(fz, p);
    }
    fz_always(fz) { fz_drop_page(fz, p); }
    fz_catch(fz) { r = fz_empty_rect; }
    if (fz_is_empty_rect(r)) r = page > 0 && known[0] ? bounds[0] : fz_make_rect(0, 0, 612, 792);
    bounds[page] = r;
    known[page] = 1;
    return true;
}

void Engine::Impl::read_sizes()
{
    int first = sizes_next, end = std::min(pages, sizes_next + SIZES_BATCH);
    Event e;
    e.kind = Event::SIZES;
    e.gen = doc_gen;
    e.first = first;
    for (int i = first; i < end; i++) {
        ensure_bound(i);
        e.sizes.push_back({ bounds[i].x1 - bounds[i].x0, bounds[i].y1 - bounds[i].y0 });
    }
    sizes_next = end;
    push(std::move(e));
}

fz_display_list* Engine::Impl::list_for(int page, fz_cookie* ck)
{
    for (size_t i = 0; i < lists.size(); i++)
        if (lists[i].page == page) {
            List l = lists[i];
            lists.erase(lists.begin() + i);
            lists.push_front(l);
            return l.list;
        }
    ensure_bound(page);
    fz_page* p = nullptr;
    fz_display_list* list = nullptr;
    fz_device* dev = nullptr;
    fz_link* links = nullptr;
    bool ok = false;
    fz_var(p);
    fz_var(list);
    fz_var(dev);
    fz_var(links);
    fz_try(fz) {
        p = fz_load_page(fz, doc, page);
        list = fz_new_display_list(fz, fz_bound_page(fz, p));
        dev = fz_new_list_device(fz, list);
        fz_run_page(fz, p, dev, fz_identity, ck);
        fz_close_device(fz, dev);
        if (!links_sent.count(page)) links = fz_load_links(fz, p);
        ok = !ck->abort;
    }
    fz_always(fz) {
        fz_drop_device(fz, dev);
    }
    fz_catch(fz) { ok = false; }
    if (ok && !links_sent.count(page)) {
        links_sent.insert(page);
        Event e;
        e.kind = Event::LINKS;
        e.gen = doc_gen;
        e.page = page;
        for (fz_link* l = links; l; l = l->next) {
            Link k;
            k.rect = { l->rect.x0 - bounds[page].x0, l->rect.y0 - bounds[page].y0, l->rect.x1 - bounds[page].x0,
                       l->rect.y1 - bounds[page].y0 };
            if (l->uri && fz_is_external_link(fz, l->uri)) k.uri = l->uri;
            else k.page = resolve(l->uri, &k.y);
            if (k.page >= 0 || !k.uri.empty()) e.links.push_back(std::move(k));
        }
        push(std::move(e));
    }
    if (links) fz_drop_link(fz, links);
    if (p) fz_drop_page(fz, p);
    if (!ok) {
        if (list) fz_drop_display_list(fz, list);
        return nullptr;
    }
    lists.push_front({ page, list });
    while ((int)lists.size() > LISTS_KEPT) {
        fz_drop_display_list(fz, lists.back().list);
        lists.pop_back();
    }
    return list;
}

Tile* Engine::Impl::render(const Job& j, fz_cookie* ck)
{
    if (!doc || j.page < 0 || j.page >= pages) return nullptr;
    fz_display_list* list = list_for(j.page, ck);
    if (!list || ck->abort) return nullptr;
    fz_pixmap* pix = nullptr;
    fz_device* dev = nullptr;
    fz_irect full = {}, bb = {};
    bool ok = false;
    fz_var(pix);
    fz_var(dev);
    float s = (float)(j.zoom * PX_PER_PT);
    fz_matrix ctm = fz_pre_rotate(fz_scale(s, s), (float)j.rot);
    fz_try(fz) {
        full = fz_round_rect(fz_transform_rect(bounds[j.page], ctm));
        fz_irect want = { full.x0 + j.part.x, full.y0 + j.part.y, full.x0 + j.part.x + j.part.w, full.y0 + j.part.y + j.part.h };
        bb = fz_intersect_irect(want, full);
        if (!fz_is_empty_irect(bb)) {
            pix = fz_new_pixmap_with_bbox(fz, fz_device_bgr(fz), bb, nullptr, 1);
            fz_clear_pixmap_with_value(fz, pix, 0xff);
            dev = fz_new_draw_device(fz, fz_identity, pix);
            fz_run_display_list(fz, list, dev, ctm, fz_rect_from_irect(bb), ck);
            fz_close_device(fz, dev);
            ok = !ck->abort;
        }
    }
    fz_always(fz) { fz_drop_device(fz, dev); }
    fz_catch(fz) { ok = false; }
    Tile* t = nullptr;
    if (ok) {
        t = new Tile;
        t->page = j.page;
        t->rot = j.rot;
        t->zoom = j.zoom;
        t->full_w = full.x1 - full.x0;
        t->full_h = full.y1 - full.y0;
        t->x = bb.x0 - full.x0;
        t->y = bb.y0 - full.y0;
        t->w = fz_pixmap_width(fz, pix);
        t->h = fz_pixmap_height(fz, pix);
        t->px.resize((size_t)t->w * t->h);
        const unsigned char* src = fz_pixmap_samples(fz, pix);
        ptrdiff_t stride = fz_pixmap_stride(fz, pix);
        // B, G, R, A bytes: a little-endian 0xAARRGGBB, a 32 bpp DIB's pixel
        for (int y = 0; y < t->h; y++) memcpy(&t->px[(size_t)y * t->w], src + y * stride, (size_t)t->w * 4);
    }
    if (pix) fz_drop_pixmap(fz, pix);
    return t;
}

void Engine::Impl::search_step()
{
    int p = (s_from + s_done) % pages;
    std::vector<fz_quad> quads(SEARCH_QUADS);
    std::vector<int> marks(SEARCH_QUADS);
    int n = 0;
    fz_var(n);
    ensure_bound(p);
    fz_try(fz) { n = fz_search_page_number(fz, doc, p, s_needle.c_str(), marks.data(), quads.data(), SEARCH_QUADS); }
    fz_catch(fz) { n = 0; }
    s_done++;
    if (n > 0) {
        Event e;
        e.kind = Event::HITS;
        e.gen = doc_gen;
        e.search = s_id;
        e.page = p;
        e.searched = s_done;
        int hit = -1;
        for (int i = 0; i < n; i++) {
            if (marks[i] || hit < 0) hit++;   // a mark starts a hit; a hit across lines has one rectangle a line
            fz_rect r = fz_rect_from_quad(quads[i]);
            e.hits.push_back({ r.x0 - bounds[p].x0, r.y0 - bounds[p].y0, r.x1 - bounds[p].x0, r.y1 - bounds[p].y0 });
            e.hit_of.push_back(hit);
        }
        push(std::move(e));
    }
    if (s_done >= pages) {
        searching = false;
        Event e;
        e.kind = Event::SEARCH_DONE;
        e.gen = doc_gen;
        e.search = s_id;
        e.searched = s_done;
        push(std::move(e));
    }
}

}   // namespace mupf
