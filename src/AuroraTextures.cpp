#include "AuroraTextures.hpp"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kPi = 3.14159265358979323846f;

unsigned char ToByte(float a)
{
    return static_cast<unsigned char>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
}

// Classic Hermite smoothstep on a t clamped to [0,1] (3t^2 - 2t^3).
float Smoothstep01(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Symmetric border-feather window for a normalized coordinate in [0,1]: exactly
// 0 at both edges, ramping smoothly up to a flat plateau of 1 within `margin`
// of each border. Multiplying the alpha envelope by EdgeFeather(nx)*EdgeFeather(ny)
// forces the texture to fade to nothing on every side, so overlapping segments
// melt into the sky with no hard seam. (The Gaussian profiles alone left a
// residual ~25/255 lip along the top and bottom rows - a visible hard edge.)
float EdgeFeather(float n, float margin)
{
    return Smoothstep01(n / margin) * Smoothstep01((1.0f - n) / margin);
}
}  // namespace

namespace AuroraTextures
{
std::vector<unsigned char> BuildCurtainPixels(int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
    {
        const float ny = static_cast<float>(y) / static_cast<float>(height);  // 0 top, 1 bottom
        // Centered soft-oval vertical falloff (fades to ~0 at both ends) so each
        // segment reads as an organic oval blob rather than a rectangle.
        const float dv = (ny - 0.5f) * 2.0f;  // -1 top .. +1 bottom
        const float vertical = std::exp(-dv * dv * 1.8f);
        // Force the top and bottom rows to exactly 0 so the ribbon's silhouette
        // feathers into the sky instead of cutting off at the Gaussian's tail.
        const float featherY = EdgeFeather(ny, 0.18f);

        for (int x = 0; x < width; ++x)
        {
            const float nx = static_cast<float>(x) / static_cast<float>(width);  // 0..1
            // Faint vertical ray striations (low amplitude -> no strobe on chaining).
            const float streak = 0.85f + 0.15f * (0.5f + 0.5f * std::sin(nx * 6.0f * kPi));
            // Gaussian horizontal feather: alpha ~0 well before the border so
            // overlapping segments melt together with no seam line.
            const float edge = nx * 2.0f - 1.0f;  // -1..1, 0 at center
            const float horizontal = std::exp(-edge * edge * 4.0f);
            // Window the sides to exactly 0 too; heavy segment overlap fills the gap.
            const float featherX = EdgeFeather(nx, 0.22f);
            const float alpha =
                vertical * streak * horizontal * featherX * featherY * 0.65f;  // translucent cap
            const int idx = (y * width + x) * 4;
            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = ToByte(alpha);
        }
    }
    return pixels;
}

std::vector<unsigned char> BuildBeamPixels(int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
    {
        const float ny = static_cast<float>(y) / static_cast<float>(height);  // 0 top, 1 bottom
        // Soft vertical falloff that fades to ~0 at BOTH ends, so the beam reads
        // as a floating, feathered glow rather than a spike rooted to the band.
        const float dv = (ny - 0.5f) * 2.0f;  // -1 top .. +1 bottom
        const float vertical = std::exp(-dv * dv * 2.2f);
        // Feather the floating beam's ends to exactly 0 (no hard top/base cut).
        const float featherY = EdgeFeather(ny, 0.16f);

        for (int x = 0; x < width; ++x)
        {
            const float nx = static_cast<float>(x) / static_cast<float>(width);
            const float edge = nx * 2.0f - 1.0f;
            const float horizontal = std::exp(-edge * edge * 3.0f);  // soft oval sides
            // Window the oval's sides to exactly 0 so no edge reads as a hard line.
            const float featherX = EdgeFeather(nx, 0.30f);
            const float alpha = vertical * horizontal * featherX * featherY * 0.55f;
            const int idx = (y * width + x) * 4;
            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = ToByte(alpha);
        }
    }
    return pixels;
}

}  // namespace AuroraTextures
