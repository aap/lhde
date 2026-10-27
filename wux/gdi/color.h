// Colours. Memory layout matches 32bpp BGRA surfaces on little-endian, i.e. a
// uint32_t pixel is 0xAARRGGBB.
#pragma once
#include <stdint.h>

namespace gdi {

struct Color {
    uint8_t b, g, r, a;
};

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return Color{b, g, r, a}; }
constexpr uint32_t pack(Color c) {
    return (uint32_t)c.a << 24 | (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b;
}
constexpr Color unpack(uint32_t p) {
    return Color{(uint8_t)p, (uint8_t)(p >> 8), (uint8_t)(p >> 16), (uint8_t)(p >> 24)};
}
inline bool operator==(Color a, Color b) { return pack(a) == pack(b); }
inline bool operator!=(Color a, Color b) { return pack(a) != pack(b); }

constexpr Color black = rgb(0, 0, 0);
constexpr Color white = rgb(255, 255, 255);

} // namespace gdi
