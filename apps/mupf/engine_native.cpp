// mupf's engine (engine.h): mupdf on a thread of its own. The only file that includes mupdf's headers; the thread is the
// only one that calls mupdf, so the context needs no locks. mupdf's fz_try / fz_catch are setjmp and longjmp: no object
// with a destructor lives inside one of their blocks.
#include "engine.h"
#include "markdown.h"
#include <strings.h>
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
// a reflowable document (Markdown, EPUB, ...): laid out as pages this tall (in points), each cut to its ink and the bottom
// margin below it; a longer file or chapter goes on to more pages of this height
const float MD_PAGE_H = 100000;
const float MD_EM = 12;                // the default font size (16 px), the Markdown stylesheet's body size
const float MD_BOTTOM = 30;            // the margin below the last ink (the stylesheet's bottom padding)

}   // namespace

struct Engine::Impl {
    Notify notify = nullptr;
    void* ctx = nullptr;
    std::mutex mu;
    std::condition_variable cv;
    std::thread th;
    // asked for (under mu)
    bool quit = false;
    bool open_pending = false, close_pending = false, layout_pending = false;
    std::string open_path, open_password;
    float open_flow_w = 0, layout_w = 0;
    uint32_t gen = 0;
    std::vector<Job> jobs;
    bool search_pending = false;
    std::string needle;
    int search_from = 0;
    uint32_t search_id = 0;
    std::deque<Event> events;
    FontFiles font_asked;
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
    bool markdown = false;
    bool reflowable = false;            // laid out by mupdf (fz_layout_document) flow_w points wide
    float flow_w = 0;
    std::string md_title;               // a Markdown file's first H1 and its headings (its outline after a new layout)
    std::vector<md::Heading> md_headings;
    FontFiles font;                     // the faces installed (load_font)
    fz_font* fonts[4] = {};             // loaded from them
    // the search being done
    bool searching = false;
    std::string s_needle;
    int s_from = 0, s_done = 0;
    uint32_t s_id = 0;

    void run();
    void push(Event&& e);
    void do_open(const std::string& path, const std::string& password, float w, uint32_t g);
    void do_layout(float w, uint32_t g);
    void opened(uint32_t g, bool relaid);
    void lay_out(float w);
    void do_close();
    bool ensure_bound(int page);
    void read_sizes();
    fz_display_list* list_for(int page, fz_cookie* ck);
    Tile* render(const Job& j, fz_cookie* ck);
    void search_step();
    void outline(fz_outline* o, int level, std::vector<TocItem>& out);
    int resolve(const char* uri, float* y);
    fz_document* open_markdown(const std::string& path, const char** err);
    void drop_fonts();
};

// mupdf's system font hook: the Markdown stylesheet's family from the files the window found (else mupdf's own faces)
static fz_font* load_font(fz_context* ctx, const char* name, int bold, int italic, int)
{
    Engine::Impl* d = (Engine::Impl*)fz_user_context(ctx);
    if (!d || !name || d->font.family.empty() || strcasecmp(name, d->font.family.c_str())) return nullptr;
    // a font object for each style asked: mupdf marks the one it is given for a style the face lacks (fake bold, slant),
    // so the regular face standing in for its italic is an object of its own
    int want = (bold ? 1 : 0) + (italic ? 2 : 0), i = want;
    if (d->font.file[i].empty()) i = d->font.file[i & 1].empty() ? 0 : i & 1;   // bold italic as bold, else regular
    if (d->font.file[i].empty()) return nullptr;
    if (!d->fonts[want]) {
        fz_font* f = nullptr;
        fz_var(f);
        fz_try(ctx) { f = fz_new_font_from_file(ctx, name, d->font.file[i].c_str(), d->font.index[i], 0); }
        fz_catch(ctx) { f = nullptr; }
        d->fonts[want] = f;
    }
    return d->fonts[want] ? fz_keep_font(ctx, d->fonts[want]) : nullptr;
}

void Engine::Impl::drop_fonts()
{
    for (fz_font*& f : fonts) {
        if (f) fz_drop_font(fz, f);
        f = nullptr;
    }
}

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

uint32_t Engine::open(const std::string& path, const std::string& password, float flow_w)
{
    std::lock_guard<std::mutex> lk(d->mu);
    d->open_pending = true;
    d->close_pending = false;
    d->layout_pending = false;
    d->open_path = path;
    d->open_password = password;
    d->open_flow_w = flow_w;
    d->jobs.clear();
    d->search_pending = false;
    if (d->running) d->running->abort = 1;
    for (Event& e : d->events) delete e.tile;
    d->events.clear();
    d->cv.notify_all();
    return ++d->gen;
}

uint32_t Engine::layout(float flow_w)
{
    std::lock_guard<std::mutex> lk(d->mu);
    if (d->open_pending) d->open_flow_w = flow_w;   // (not open yet: opened at this width)
    else d->layout_pending = true;
    d->layout_w = flow_w;
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
    d->layout_pending = false;
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

void Engine::set_font(const FontFiles& f)
{
    std::lock_guard<std::mutex> lk(d->mu);
    d->font_asked = f;
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
    if (fz) {
        fz_register_document_handlers(fz);
        fz_set_user_context(fz, this);
        fz_install_load_system_font_funcs(fz, load_font, nullptr, nullptr);
    }
    std::unique_lock<std::mutex> lk(mu);
    for (;;) {
        cv.wait(lk, [&] {
            return quit || open_pending || close_pending || layout_pending || !jobs.empty() || search_pending ||
                   (doc && sizes_next < pages) || searching;
        });
        if (quit) break;
        if (open_pending || close_pending) {
            bool opening = open_pending;
            std::string path = open_path, password = open_password;
            float w = open_flow_w;
            uint32_t g = gen;
            open_pending = close_pending = false;
            searching = false;
            FontFiles f = font_asked;
            lk.unlock();
            do_close();
            if (f != font) {   // (the closed document's fonts are dropped with it)
                drop_fonts();
                font = f;
            }
            if (opening) do_open(path, password, w, g);
            lk.lock();
            continue;
        }
        if (layout_pending) {
            layout_pending = false;
            float w = layout_w;
            uint32_t g = gen;
            searching = false;
            lk.unlock();
            do_layout(w, g);
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
    if (fz) drop_fonts();
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
    markdown = false;
    reflowable = false;
    flow_w = 0;
    md_title.clear();
    md_headings.clear();
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

// mupdf resolves an id to its element's first baseline: a heading's top, with some of the space above it, is its size and
// a bit higher (the stylesheet's sizes: H1 2em, H2 1.5em, H3 1.25em and smaller)
static float heading_top(float y, int level)
{
    float size = MD_EM * (level <= 1 ? 2 : level == 2 ? 1.5f : 1.25f);
    return std::max(0.0f, y - size * 1.15f - 9);
}

// the Markdown file as an HTML page (markdown.h) opened by mupdf with the file's directory as its archive, so a relative
// <img src> and link resolve there (lay_out gives it its width); its title and headings kept (md_title, md_headings)
fz_document* Engine::Impl::open_markdown(const std::string& path, const char** err)
{
    std::string text;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { *err = "The file cannot be read."; return nullptr; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    md::Style st;
    if (!font.file[0].empty()) st.family = font.family;
    md::Page page = md::convert(text, st);
    md_title = page.title;
    md_headings = page.headings;
    size_t slash = path.rfind('/');
    std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
    fz_buffer* b = nullptr;
    fz_stream* stm = nullptr;
    fz_archive* arc = nullptr;
    fz_document* d = nullptr;
    const char* html = page.html.c_str();
    size_t len = page.html.size();
    fz_var(b);
    fz_var(stm);
    fz_var(arc);
    fz_var(d);
    fz_try(fz) {
        b = fz_new_buffer_from_copied_data(fz, (const unsigned char*)html, len);
        stm = fz_open_buffer(fz, b);
        arc = fz_open_directory(fz, dir.c_str());
        d = fz_open_document_with_stream_and_dir(fz, "text/html", stm, arc);
    }
    fz_always(fz) {
        fz_drop_stream(fz, stm);
        fz_drop_archive(fz, arc);
        fz_drop_buffer(fz, b);
    }
    fz_catch(fz) {
        if (d) fz_drop_document(fz, d);
        d = nullptr;
        *err = "The Markdown file cannot be laid out.";
    }
    return d;
}

// a reflowable document laid out w points wide (the narrowest FLOW_MIN_PT), its pages counted
void Engine::Impl::lay_out(float w)
{
    flow_w = std::max(FLOW_MIN_PT, w > 0 ? w : md::PAGE_WIDTH_PT);
    fz_try(fz) {
        fz_layout_document(fz, doc, flow_w, MD_PAGE_H, MD_EM);
        pages = fz_count_pages(fz, doc);
    }
    fz_catch(fz) { pages = 0; }
}

void Engine::Impl::do_open(const std::string& path, const std::string& password, float w, uint32_t g)
{
    Event e;
    e.gen = g;
    if (!fz) { e.kind = Event::FAILED; e.error = "mupdf could not start"; push(std::move(e)); return; }
    const char* err = nullptr;
    char msg[512] = "";
    bool locked = false;
    markdown = md::is_markdown(path);
    if (markdown) {
        doc = open_markdown(path, &err);
        reflowable = doc != nullptr;
    }
    fz_var(err);
    fz_var(locked);
    if (!markdown) {
        fz_try(fz) {
            doc = fz_open_document(fz, path.c_str());
            if (fz_needs_password(fz, doc) && !fz_authenticate_password(fz, doc, password.c_str())) {
                err = password.empty() ? "The document is protected by a password." : "The password is not right.";
                locked = true;
            } else reflowable = fz_is_document_reflowable(fz, doc);
        }
        fz_catch(fz) {
            snprintf(msg, sizeof msg, "%s", fz_caught_message(fz));
            err = msg;
        }
        if (!err && !reflowable) {
            fz_try(fz) { pages = fz_count_pages(fz, doc); }
            fz_catch(fz) { pages = 0; }
        }
    }
    if (!err && reflowable) lay_out(w);
    if (!err && pages <= 0) err = "The document has no pages.";
    if (err) {
        do_close();
        e.kind = Event::FAILED;
        e.error = err;
        e.password = locked;
        push(std::move(e));
        return;
    }
    opened(g, false);
}

// a reflowable document laid out again at another width: its pages, sizes and outline anew, as generation g
void Engine::Impl::do_layout(float w, uint32_t g)
{
    if (!doc || !reflowable) return;
    for (List& l : lists) fz_drop_display_list(fz, l.list);
    lists.clear();
    links_sent.clear();
    lay_out(w);
    if (pages <= 0) {   // (mupdf failed to lay it out: nothing to show)
        Event e;
        e.kind = Event::FAILED;
        e.gen = g;
        e.error = "The document cannot be laid out.";
        do_close();
        push(std::move(e));
        return;
    }
    opened(g, true);
}

// OPENED for the document just opened or laid out again: its pages (every one the first one's size until they are read),
// the title, the format, the outline (a Markdown file's: its headings H1 to H3, mupdf's HTML has none)
void Engine::Impl::opened(uint32_t g, bool relaid)
{
    Event e;
    e.gen = g;
    doc_gen = g;
    bounds.assign(pages, fz_empty_rect);
    known.assign(pages, 0);
    sizes_next = 0;
    ensure_bound(0);
    e.kind = Event::OPENED;
    e.pages = pages;
    e.reflowable = reflowable;
    e.flow_w = reflowable ? flow_w : 0;
    e.relaid = relaid;
    char buf[1024] = "";
    if (markdown) {
        e.markdown = true;
        e.title = md_title;
        e.format = "Markdown";
        for (const md::Heading& h : md_headings) {
            if (h.level > 3) continue;
            TocItem it;
            it.title = h.text;
            it.level = h.level - 1;
            it.open = true;
            it.page = resolve(("#" + h.id).c_str(), &it.y);
            it.y = heading_top(it.y, h.level);
            e.toc.push_back(std::move(it));
        }
    }
    if (!markdown && fz_lookup_metadata(fz, doc, FZ_META_INFO_TITLE, buf, sizeof buf) > 0) e.title = buf;
    buf[0] = 0;
    if (!markdown && fz_lookup_metadata(fz, doc, FZ_META_FORMAT, buf, sizeof buf) > 0) e.format = buf;
    fz_outline* o = nullptr;
    fz_var(o);
    if (!markdown) {
        fz_try(fz) { o = fz_load_outline(fz, doc); }
        fz_catch(fz) { o = nullptr; }
    }
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
    fz_device* dev = nullptr;
    fz_rect r = fz_empty_rect, ink = fz_empty_rect;
    bool cut = reflowable;   // a reflowable document's tall pages: to their ink
    fz_var(p);
    fz_var(r);
    fz_var(dev);
    fz_var(cut);
    fz_try(fz) {
        p = fz_load_page(fz, doc, page);
        r = fz_bound_page(fz, p);
        if (cut) {
            dev = fz_new_bbox_device(fz, &ink);
            fz_run_page(fz, p, dev, fz_identity, nullptr);
            fz_close_device(fz, dev);
        }
    }
    fz_always(fz) {
        fz_drop_device(fz, dev);
        fz_drop_page(fz, p);
    }
    fz_catch(fz) { r = fz_empty_rect; }
    if (cut && !fz_is_empty_rect(r)) r.y1 = std::min(r.y1, std::max(r.y0 + 100, (fz_is_empty_rect(ink) ? r.y0 : ink.y1) + MD_BOTTOM));
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
            if (k.page >= 0 && markdown) k.y = heading_top(k.y, 1);   // (an anchor is a heading's, mostly)
            if (k.page < 0 && k.uri.empty() && markdown && l->uri && l->uri[0] && l->uri[0] != '#') k.uri = l->uri;
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
