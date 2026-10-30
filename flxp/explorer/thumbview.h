// Thumbnails in the list view, in two styles.
//
// XP's Thumbnails view (defview dvthumbnail.cpp): the list in LVS_ICON with an
// image list of thumbnail_size squares (default 96), LVS_EX_BORDERSELECT (the
// 1 px outline and the 3 px selection frame, vendored listview.c) and XP's
// icon spacing (cxIcon/4 + size + 1 + 20 across). Pictures are fitted into the
// square on the window colour; everything else shows its 48 px icon centred in
// it (XP's CreateDefaultThumbnail), folders XP's folder thumbnail with up to
// four pictures of their contents.
//
// The Longhorn hosts (set_longhorn; 4051/4074, docs/USAGE.md "Views"): the
// images of every view at its size, on transparency, so a thumbnail is just a
// bigger or smaller icon: a picture in a photo frame that follows its aspect
// (a soft shadow, a 1 px grey line, a mat graded white to light grey; bare
// below 32 px), any other item its own icon at full size, a folder 4074's
// photo standing in the open folder (shell32 #389 back, the photo warped in
// perspective, #390 front) where the build has that art, else the folder icon
// with up to four pictures on it (4051 and XP). The browser sets the list's
// spacing and selection; this only makes the images.
//
// Pictures come from shell/thumbs on worker threads; an item shows its default
// image at once and the thumbnail replaces it when it arrives.
#pragma once
#include <string>
#include <vector>
#include "shell/namespace.h"
#include "w32/w32.h"
extern "C" {
#include "commctrl.h"
}

namespace thumbview {

enum { WM_THUMBS = WM_APP + 0x40 };   // posted to the notify window when results are waiting

void init(HWND list, HWND notify);    // once; reads thumbnail_size
void set_longhorn(bool on);           // the Longhorn style (before enter_at; drops the images made)
bool longhorn();
int size();                           // the squares' size now
int default_size();                   // thumbnail_size
bool active();
void enter();                         // the list is LVS_ICON already (XP's Thumbnails view: thumbnail_size)
// The images at px in the normal slot or (small_slot: Details, and XP's List and
// Small Icons) in LVSIL_SMALL; leaves the other slot to large/small first. True
// when the images changed (fill again).
bool enter_at(int px, bool small_slot, HIMAGELIST large, HIMAGELIST small);
void leave(HIMAGELIST large, HIMAGELIST small = nullptr);   // back to the icon lists
bool busy();                          // thumbnails being made (the command bar's Stop)
void stop();                          // Stop: drop what is pending
// After the list was (re)filled from items (list index = index in items):
// sets each item's image and asks for the missing thumbnails.
void fill(const std::vector<shell::Item>& items);
void folder_changed();                // navigation: drop pending work and the cached images
void on_ready();                      // WM_THUMBS

}
