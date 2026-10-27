// Shell icons as win32 image lists, shared by the explorer and the desktop:
// icon theme sources ("file.ico" or "pe:<file>#<id>", shell/icons.h) loaded
// at 16 and 32 px into one small and one large image list, one index per key.
#pragma once
#include <string>
#include "w32/w32.h"
extern "C" {
#include "commctrl.h"
}

namespace uiicons {

void init(int large_px = 32);           // creates the image lists (16 and large_px: 32, or 48 for XP's large desktop icons)
int large_size();
// Follows a changed icons setting (per theme; shell::refresh_icon_theme): every
// index is reloaded in place from the new theme, so views keep their items and
// only repaint. True if the theme changed.
bool sync();
HIMAGELIST small();
HIMAGELIST large();
// The zoom's lists (16 = small(), large_size() = large(), else made on first
// use from the theme's sources at px): the same indices as small()/large()
HIMAGELIST at_size(int px);
int index(const std::string& key);      // loads on first use; "document" for unknown keys; -1 if none
HICON large_icon(const std::string& key);   // a copy of the large list's icon (DestroyIcon it), or null
// A theme source as a HICON/HBITMAP (LoadImage flags as the explorer uses them).
HANDLE load_themed(const std::string& src, UINT type, int cx, int cy, UINT extra);
HICON load_icon(const std::string& key, int size);   // any size from the theme's source (DestroyIcon it)
// The size of the image the theme's icon has for a request of `want` px: want
// when it has that size, else the smallest larger one, else its largest
// (Longhorn 4074's shell32: 48 for most, 256 for folder and document; XP: 48)
int native_size(const std::string& key, int want);

}
