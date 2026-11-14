// luxview's view model (no win32): the list of pictures, which one is shown, the zoom and rotation, and the loader that
// decodes on a thread of its own. The window (luxview.cpp) asks for what it shows and draws what it gets; everything that
// decides what to decode, when, and what to keep is here, so the toolkit test (tests/toolkit/luxviewtest.cpp) drives it
// with a slow fake decoder.
//
// Loading a picture is two stages: the thumbnail from the shared cache (shell/thumbs.h's freedesktop cache, when one is
// there: drawn blown up to size at once) and the decode at the size the window needs (a 6000 x 4000 JPEG in a 1280 px
// window is decoded at 1/4 by libjpeg's DCT scaling and resampled to the fitted size, never kept whole). Zooming in past
// what was decoded asks for a larger decode; zooming out resamples what is there. Each request carries the generation of
// the picture it is for: going to another picture starts a new generation, drops what is queued, and a decode of an older
// generation that finishes is thrown away, so a fast run of Next never shows an older picture over the current one. When
// the shown picture is complete, its neighbour in the direction of travel is decoded at the fitted size (one prefetch),
// so the next Next shows it at once. The decoded pictures held (the shown one and the prefetched one) stay within
// BUDGET; a zoomed-in decode larger than that drops the prefetch.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "gdi/bitmap.h"

namespace luxview {

// The pictures luxview decodes (shell::thumbs::own_picture: PNG, JPEG, BMP, ICO and CUR, WebP when built with it), by name
bool is_picture(const std::string& path);
// A folder's pictures in the Explorer's Name order (natural: case-insensitive, digit runs by value), hidden ones left out
std::vector<std::string> folder_pictures(const std::string& dir);
// The list the command line makes and where it starts: one file is its folder's pictures starting at it (the file alone
// when it is hidden or not a picture by name), one folder its pictures from the first, several arguments those files (each
// folder's pictures in its place). Paths are made absolute.
std::vector<std::string> list_for(const std::vector<std::string>& args, size_t* start);

// The zoom steps, percent of the picture's own size: 5 .. 4000
extern const int ZOOM_STEPS[];
extern const int ZOOM_STEP_COUNT;
double zoom_step_in(double z);          // the next step above z (z itself at the top)
double zoom_step_out(double z);         // the next step below z

// The size of w x h fitted into bw x bh keeping its aspect ratio, never larger than w x h, at least 1 x 1
void fit_into(int w, int h, int bw, int bh, int* ow, int* oh);

// What decodes: decode(path, box_w, box_h, &w, &h) returns the picture fitted into box_w x box_h (straight alpha; null
// when it cannot be read) and the picture's own size; preview(path, &w, &h) a cached thumbnail and the picture's own size
// when the cache knows it (0 when not), null when none is cached. Both run on the loader's thread.
struct Decoder {
    gdi::Bitmap* (*decode)(const std::string& path, int box_w, int box_h, int* w, int* h, void* ctx) = nullptr;
    gdi::Bitmap* (*preview)(const std::string& path, int* w, int* h, void* ctx) = nullptr;
    void* ctx = nullptr;
};
Decoder default_decoder();              // gdi's decoders at the box's scale (one of the shell's decode turns), the cache

class Loader {
public:
    typedef void (*Notify)(void* ctx);  // a result is ready (called on the loader's thread: post a message)
    enum Kind { PREVIEW, CURRENT, PREFETCH };
    struct Result {
        uint32_t gen = 0;
        Kind kind = CURRENT;
        std::string path;
        int box_w = 0, box_h = 0;       // what was asked
        gdi::Bitmap* bmp = nullptr;     // null: none (no thumbnail, or the picture could not be read)
        int w = 0, h = 0;               // the picture's own size (0: unknown)
    };
    Loader(Notify notify, void* ctx, const Decoder& d);
    ~Loader();                          // drops what is queued and joins the thread (after the decode under way)
    // A new picture: a new generation, everything queued dropped, then its thumbnail (preview) and its decode (decode) queued.
    // A decode under way or queued for the same path at least that large (the prefetch) becomes this generation's.
    uint32_t show(const std::string& path, int box_w, int box_h, bool preview, bool decode);
    void request(Kind kind, const std::string& path, int box_w, int box_h);   // in this generation (a larger decode, a prefetch)
    void cancel_prefetch();
    bool take(Result& out);             // the next result of this generation (a prefetch of any); false when none
    uint32_t generation() const;
    bool busy() const;
    int decodes() const;                // decode calls made so far
    // On the loader's thread, for a decoder that can stop early: the loader is ending, or the job it runs was dropped
    static bool stopping();
    struct Impl;
private:
    Impl* impl_;
    Loader(const Loader&);
    Loader& operator=(const Loader&);
};

void free_result(Loader::Result& r);

class Viewer {
public:
    static const size_t BUDGET = 64u << 20;          // the shown picture and the prefetched one at the fitted size
    static const long long MAX_PIXELS = 32ll << 20;   // the largest decode a zoom asks for (128 MB); beyond, it is magnified
    Viewer(Loader::Notify notify, void* ctx, const Decoder& d = default_decoder());
    ~Viewer();

    // the list
    void set_files(const std::vector<std::string>& files, size_t start);
    size_t count() const { return files_.size(); }
    size_t index() const { return index_; }
    std::string path() const { return files_.empty() ? std::string() : files_[index_]; }
    const std::vector<std::string>& files() const { return files_; }
    bool go(size_t i);                  // shows files[i]; false when it is already shown (or out of range)
    bool next();                        // wraps around at the end, as XP's viewer and its slide show do
    bool prev();
    bool first();
    bool last();
    void remove_current();              // the file left (deleted): the next one is shown, the previous at the end

    // the area the picture is fitted into, and the zoom
    void set_viewport(int w, int h);
    int viewport_w() const { return vw_; }
    int viewport_h() const { return vh_; }
    bool best_fit() const { return fit_; }
    void set_best_fit();
    void set_zoom(double z);            // a scale of the picture's own size (1: Actual Size)
    double zoom() const;                // the scale drawn: the fitted one under Best Fit
    bool zoom_in();
    bool zoom_out();
    void rotate(int quarters);          // +1 clockwise, -1 counterclockwise; in the view only, reset by another picture
    int rotation() const { return rot_; }

    // what is shown
    const gdi::Bitmap* bitmap() const { return cur_.bmp; }   // rotated as shown; null while there is none
    int picture_w() const;              // the picture's own size as rotated, 0 while not known
    int picture_h() const;
    bool is_preview() const { return cur_.bmp && cur_.preview; }
    bool failed() const { return failed_; }
    bool loading() const;               // the picture asked for is not there yet at the size wanted
    // UI thread, after the notify: takes the loader's results; true when what is shown changed
    bool pump();
    size_t bytes() const;               // the decoded pictures held
    bool prefetched(const std::string& path) const { return pre_.bmp && pre_.path == path; }
    Loader& loader() { return loader_; }

private:
    struct Pic { std::string path; gdi::Bitmap* bmp = nullptr; int w = 0, h = 0; bool preview = false; int box_w = 0, box_h = 0; };
    void drop(Pic& p);
    void install(Pic& from);            // from becomes the shown picture (rotated as the view is)
    void wanted(int* bw, int* bh) const;   // the decode the view needs, in the picture's own orientation
    bool enough() const;
    void ensure();
    std::string neighbour() const;

    Loader loader_;
    std::vector<std::string> files_;
    size_t index_ = 0;
    int dir_ = 1;
    int vw_ = 800, vh_ = 600;
    bool fit_ = true;
    double zoom_ = 1;
    int rot_ = 0;
    Pic cur_, pre_;
    bool failed_ = false;
    int asked_w_ = 0, asked_h_ = 0;     // the decode under way for the shown picture
    std::string pre_asked_;             // the prefetch under way
};

} // namespace luxview
