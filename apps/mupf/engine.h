// mupf's engine: mupdf (the fz_* API) on a thread of its own. Only that thread touches mupdf; the window asks and takes
// results, so it never waits on a document. No win32 and no mupdf types here: engine_native.cpp is the one file that
// includes mupdf's headers.
//
// What the thread does, most urgent first: open a document (its page count, the first page's size, the outline, the
// title); render the jobs the window wants, in its order (a job no longer wanted is abandoned: mupdf's cookie aborts
// the page being drawn); the other pages' sizes, a batch at a time (until they are known every page is taken to be
// the first one's size); a search, a page at a time. A page's display list is kept for the few pages last drawn, so a
// new zoom of them redraws without interpreting the page again.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "layout.h"

namespace mupf {

struct TocItem {
    std::string title;
    int level = 0;              // 0 at the top
    int page = -1;              // -1: no page in this document (an external link)
    float y = 0;                // the destination's top on the page, in points (0 when the outline gives none)
    bool open = false;          // the document asks for it to be expanded
};

struct RectF { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };   // points of the unrotated page, from its top left

struct Link {
    RectF rect;
    int page = -1;              // a destination in the document, else -1 and uri
    float y = 0;
    std::string uri;
};

struct Job {
    int page = 0, rot = 0;
    double zoom = 1;
    Box part;                   // in the page's pixels at zoom, turned
};

struct Event {
    enum Kind { OPENED, FAILED, SIZES, RENDERED, LINKS, HITS, SEARCH_DONE } kind = OPENED;
    uint32_t gen = 0;           // the document's generation
    // OPENED
    int pages = 0;
    std::string title, format;
    std::vector<TocItem> toc;
    // OPENED, SIZES: the sizes of pages first .. first + sizes.size() - 1
    int first = 0;
    std::vector<SizeF> sizes;
    // FAILED
    std::string error;
    // RENDERED
    Tile* tile = nullptr;       // the receiver takes it
    // LINKS, HITS: a page's links, a page's hits
    int page = 0;
    std::vector<Link> links;
    // HITS, SEARCH_DONE
    uint32_t search = 0;        // the search's id
    std::vector<RectF> hits;    // one rectangle a line of each hit
    std::vector<int> hit_of;    // which hit (counted on the page) each rectangle belongs to
    int searched = 0;           // pages searched so far
};

class Engine {
public:
    typedef void (*Notify)(void* ctx);  // an event is ready (called on the engine's thread: post a message)
    Engine(Notify notify, void* ctx);
    ~Engine();                          // ends the thread, closing the document
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    uint32_t open(const std::string& path); // the new generation; its OPENED or FAILED follows
    void close();
    // the jobs to do now, most wanted first; what was asked before and is not here is dropped
    void want(const std::vector<Job>& jobs);
    // search the document for text (case-insensitive, mupdf's fz_search_page), from page from to the end and round to it;
    // HITS for each page with hits, SEARCH_DONE at the end. An empty needle stops the search.
    uint32_t search(const std::string& needle, int from);
    bool take(Event& e);                // the next event, false when none

    struct Impl;
private:
    Impl* d;
};

}   // namespace mupf
