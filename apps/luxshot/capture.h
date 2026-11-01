// luxshot's capture: the screen, the active window (with luxwm's frame or without) or a dragged rectangle, read from the
// X root window as it is shown (with a compositor, the composited picture). Plain Xlib with its own connection, no
// win32 here (capture_native.cpp): wux has no way to read the screen.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace capture {

enum class Mode { Screen, Window, Region };

struct Shot {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;     // 0xFFRRGGBB, rows top down
    std::string what;                 // "Whole screen", the window's title, "Region"
    int x = 0, y = 0;                 // where it was on the screen
};

// Waits delay seconds, then captures. frame: a window with its frame (_NET_FRAME_EXTENTS). False with *err a sentence
// ("" when the user cancelled a region with Escape or the right button).
bool take(Mode mode, bool frame, double delay, Shot& out, std::string* err);

} // namespace capture
