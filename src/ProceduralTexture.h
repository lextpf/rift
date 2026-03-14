#pragma once

#include <array>
#include <cstdint>
#include <vector>

/**
 * @brief RGBA pixel value returned by procedural texture generators.
 *
 * @see GeneratePixels
 */
using Pixel = std::array<uint8_t, 4>;

/**
 * @brief Generate an RGBA pixel buffer by evaluating a functor at every texel.
 *
 * Allocates `w * h * 4` bytes in @p pixels and fills each texel by calling
 * @p fn(x, y, w, h), which must return a `Pixel` (std::array<uint8_t, 4>).
 *
 * @tparam PixelFn Callable with signature `Pixel(int x, int y, int w, int h)`.
 * @param[out] pixels Output buffer (resized automatically).
 * @param      w      Texture width in texels.
 * @param      h      Texture height in texels.
 * @param      fn     Per-texel color function.
 *
 * @par Example
 * @code{.cpp}
 * std::vector<unsigned char> pixels;
 * GeneratePixels(pixels, 64, 64, [](int x, int y, int w, int h) -> Pixel {
 *     float dx = x - w / 2.0f, dy = y - h / 2.0f;
 *     float d = std::sqrt(dx * dx + dy * dy) / (w / 2.0f);
 *     auto a = static_cast<uint8_t>(std::exp(-d * d * 3.0f) * 255);
 *     return {255, 255, 255, a};
 * });
 * @endcode
 */
template <typename PixelFn>
void GeneratePixels(std::vector<unsigned char>& pixels, int w, int h, PixelFn fn)
{
    pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            auto [r, g, b, a] = fn(x, y, w, h);
            int i = (y * w + x) * 4;
            pixels[i + 0] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            pixels[i + 3] = a;
        }
    }
}
