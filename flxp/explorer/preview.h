// Pictures for the panes that describe the selection (XP's Details section,
// 4051's band): thumbnails through shell/thumbs in the background, handed out
// as premultiplied DIB sections fitted (resampled) to the box asked for; a
// folder's picture composed as the Icons view composes it.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "w32/w32.h"

namespace preview {

class Pictures {
public:
    // results are announced to `wnd` with `msg`; call take() there
    Pictures(HWND wnd, UINT msg);
    ~Pictures();
    // the pictures to have, each fitted into size x size by the loader (from
    // the thumbnail cache's 128 or 256 px); others are dropped, pending ones
    // cancelled
    void want(const std::vector<std::string>& paths, int size);
    bool take();                                        // true: something arrived
    // the picture fitted into max_w x max_h (never enlarged), kept for the
    // next call; null while it is not there (or cannot be made)
    HBITMAP get(const std::string& path, int max_w, int max_h, int* w, int* h);
    // A folder's picture as the Icons view composes it (thumbview::folder_picture: the folder with up to four pictures
    // of it, or 4074's photo in the open folder), made at size from the same recipe and the same thumbnail cache, in the
    // background (its own loader thread); one folder at a time, "" drops it. get_folder: null while it is not there.
    void want_folder(const std::string& path, const std::string& icon_key, int size, bool longhorn);
    HBITMAP get_folder(const std::string& path, int size, int* w, int* h);
    struct Impl;
private:
    Impl* impl_;
};

// a premultiplied DIB drawn 1:1 with a constant alpha
void blend(HDC dc, HBITMAP bmp, int x, int y, int w, int h, int alpha = 255);
// a filled rectangle blended with `alpha` (frames and shadows)
void fill_alpha(HDC dc, int x, int y, int w, int h, COLORREF c, int alpha);

}
