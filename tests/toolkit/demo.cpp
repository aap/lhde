// Skeleton demo: a toplevel with child "box" windows that react to the mouse,
// overlap each other, and one that wanders on a timer. Exercises invalidation,
// clipping, capture, hover and timers. Classic-look bevels drawn by hand.
#include <stdio.h>
#include "win/window.h"

using namespace win;
using gdi::rgb;

static const gdi::Color face = rgb(212, 208, 200);   // COLOR_3DFACE
static const gdi::Color hilite = rgb(255, 255, 255);
static const gdi::Color shadow = rgb(128, 128, 128);
static const gdi::Color dark = rgb(64, 64, 64);

static void bevel(gdi::DC* dc, Rect r, bool sunken)
{
    gdi::Color tl = sunken ? shadow : hilite, br = sunken ? hilite : shadow;
    gdi::Color tl2 = sunken ? dark : face, br2 = sunken ? face : dark;
    gdi::hline(dc, r.left, r.right - 1, r.top, tl);
    gdi::vline(dc, r.left, r.top, r.bottom - 1, tl);
    gdi::hline(dc, r.left, r.right, r.bottom - 1, br2);
    gdi::vline(dc, r.right - 1, r.top, r.bottom, br2);
    r = gdi::inflate(r, -1, -1);
    gdi::hline(dc, r.left, r.right - 1, r.top, tl2);
    gdi::vline(dc, r.left, r.top, r.bottom - 1, tl2);
    gdi::hline(dc, r.left, r.right, r.bottom - 1, br);
    gdi::vline(dc, r.right - 1, r.top, r.bottom, br);
}

struct Box { bool hover, pressed; int clicks; };

static Result box_proc(Window* w, uint32_t msg, WParam wp, LParam lp)
{
    Box* b = (Box*)w->userdata;
    switch (msg) {
    case WM_CREATE:
        w->userdata = new Box{};
        return 0;
    case WM_DESTROY:
        delete (Box*)w->userdata; w->userdata = nullptr;
        return 0;
    case WM_MOUSEMOVE:
        if (!b->hover) { b->hover = true; track_mouse_leave(w); invalidate(w); }
        return 0;
    case WM_MOUSELEAVE:
        b->hover = false; invalidate(w);
        return 0;
    case WM_LBUTTONDOWN:
        b->pressed = true; set_capture(w); set_focus(w); invalidate(w);
        return 0;
    case WM_LBUTTONUP:
        if (b->pressed) {
            b->pressed = false; release_capture();
            if (client_rect(w).contains(point_of(lp))) { b->clicks++; send(w->parent, WM_COMMAND, (WParam)w->id, (LParam)w); }
            invalidate(w);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_SPACE) { b->clicks++; invalidate(w); }
        return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS:
        invalidate(w);
        return 0;
    case WM_PAINT: {
        PaintInfo pi;
        gdi::DC* dc = begin_paint(w, pi);
        Rect r = client_rect(w);
        gdi::Color fill = b->pressed ? rgb(180, 180, 180) : b->hover ? rgb(230, 230, 230) : face;
        gdi::fill_rect(dc, r, fill);
        bevel(dc, r, b->pressed);
        if (focus() == w) gdi::frame_rect(dc, gdi::inflate(r, -4, -4), rgb(0, 0, 0));
        // click counter as little squares
        for (int i = 0; i < b->clicks && i < 20; i++)
            gdi::fill_rect(dc, gdi::rect_xywh(8 + (i % 10) * 8, 8 + (i / 10) * 8, 6, 6), rgb(0, 0, 128));
        end_paint(w, pi);
        return 0;
    }
    }
    return def_proc(w, msg, wp, lp);
}

static Window* wanderer;
static int wander_t;

static Result main_proc(Window* w, uint32_t msg, WParam wp, LParam lp)
{
    switch (msg) {
    case WM_COMMAND:
        printf("box %d clicked\n", (int)wp);
        fflush(stdout);
        if (wp == 3) { destroy(w); }
        return 0;
    case WM_TIMER: {
        wander_t++;
        int x = 150 + (wander_t % 40 < 20 ? wander_t % 20 : 20 - wander_t % 20) * 5;
        set_window_pos(wanderer, nullptr, gdi::rect_xywh(x, 160, 80, 50), SWP_NOZORDER);
        return 0;
    }
    case WM_PAINT: {
        PaintInfo pi;
        gdi::DC* dc = begin_paint(w, pi);
        Rect r = client_rect(w);
        // a sunken "client" panel behind everything
        gdi::fill_rect(dc, gdi::inflate(r, -10, -10), rgb(255, 255, 255));
        bevel(dc, gdi::inflate(r, -10, -10), true);
        gdi::line(dc, Point{20, 20}, Point{r.right - 20, r.bottom - 20}, rgb(200, 0, 0));
        end_paint(w, pi);
        return 0;
    }
    case WM_DESTROY:
        quit(0);
        return 0;
    }
    return def_proc(w, msg, wp, lp);
}

int main()
{
    if (!init()) return 1;
    Class mc{"demo", main_proc};
    mc.background = face; mc.paint_background = true;
    register_class(mc);
    Class bc{"box", box_proc};
    bc.style = CS_DBLCLKS;
    register_class(bc);

    Window* w = create("demo", "flxp skeleton", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, gdi::rect_xywh(100, 100, 400, 300));
    create("box", "a", WS_VISIBLE, 0, gdi::rect_xywh(30, 30, 100, 60), w, nullptr, 1);
    create("box", "b", WS_VISIBLE, 0, gdi::rect_xywh(90, 60, 100, 60), w, nullptr, 2);   // overlaps a, created later = on top
    create("box", "quit", WS_VISIBLE, 0, gdi::rect_xywh(280, 30, 90, 40), w, nullptr, 3);
    wanderer = create("box", "w", WS_VISIBLE, 0, gdi::rect_xywh(150, 160, 80, 50), w, nullptr, 4);
    set_timer(w, 1, 100);
    return run();
}
