// The desktop wallpaper: an image file (BMP, PNG, JPEG, WebP) composed onto a
// background colour at screen size, as XP's Display Properties modes.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace wallpaper {

struct Image {
    int width = 0, height = 0;
    std::vector<uint32_t> px;     // 0xAARRGGBB, top-down
    bool empty() const { return px.empty(); }
};

bool load(const std::string& path, Image& out, std::string* err = nullptr);

enum class Mode { Center, Stretch, Tile, Fill, Fit };
Mode mode_from(const std::string& name);    // center|stretch|tile|fill|fit (default stretch)

// out: width*height pixels, top-down, opaque. The image (may be empty) is
// placed in rect (x, y, w, h) of it, the rest is `background` (0xRRGGBB).
void compose(const Image& img, Mode mode, uint32_t background, uint32_t* out, int width, int height,
             int x, int y, int w, int h);

}
