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

// Symmetric border feather for a normalized coord in [0,1]: 0 at both edges,
// plateau of 1 within `margin`. Windowing alpha by EdgeFeather(nx)*EdgeFeather(ny)
// fades every side to nothing (the Gaussians alone left a ~25/255 lip top/bottom).
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
    const float widthDenom = static_cast<float>(std::max(1, width - 1));
    const float heightDenom = static_cast<float>(std::max(1, height - 1));
    for (int y = 0; y < height; ++y)
    {
        const float ny = static_cast<float>(y) / heightDenom;  // 0 top, 1 bottom
        // A bright core nested inside a broad, faint halo. The second envelope
        // lengthens the translucent tail while the edge window still guarantees
        // that every border reaches full transparency.
        const float dv = (ny - 0.5f) * 2.0f;  // -1 top .. +1 bottom
        const float verticalCore = std::exp(-dv * dv * 2.1f);
        const float verticalHalo = std::exp(-dv * dv * 0.65f);
        const float vertical = verticalCore * 0.84f + verticalHalo * 0.16f;
        const float featherY = EdgeFeather(ny, 0.35f);

        for (int x = 0; x < width; ++x)
        {
            const float nx = static_cast<float>(x) / widthDenom;  // 0..1
            // Barely-visible ray variation keeps the field organic without
            // drawing attention to individual chained slices.
            const float streak = 0.92f + 0.08f * (0.5f + 0.5f * std::sin(nx * 6.0f * kPi));
            const float edge = nx * 2.0f - 1.0f;  // -1..1, 0 at center
            const float horizontalCore = std::exp(-edge * edge * 3.8f);
            const float horizontalHalo = std::exp(-edge * edge * 0.9f);
            const float horizontal = horizontalCore * 0.82f + horizontalHalo * 0.18f;
            const float featherX = EdgeFeather(nx, 0.40f);
            const float alpha = vertical * streak * horizontal * featherX * featherY * 0.68f;
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
    const float widthDenom = static_cast<float>(std::max(1, width - 1));
    const float heightDenom = static_cast<float>(std::max(1, height - 1));
    for (int y = 0; y < height; ++y)
    {
        const float ny = static_cast<float>(y) / heightDenom;  // 0 top, 1 bottom
        // Soft vertical falloff that fades to ~0 at both ends, so the beam reads
        // as a floating, feathered glow rather than a spike rooted to the band.
        const float dv = (ny - 0.5f) * 2.0f;  // -1 top .. +1 bottom
        const float vertical = std::exp(-dv * dv * 1.7f);
        // Feather the floating beam's ends to exactly 0 (no hard top/base cut).
        const float featherY = EdgeFeather(ny, 0.28f);

        for (int x = 0; x < width; ++x)
        {
            const float nx = static_cast<float>(x) / widthDenom;
            const float edge = nx * 2.0f - 1.0f;
            const float horizontal = std::exp(-edge * edge * 2.4f);  // soft oval sides
            // Window the oval's sides to exactly 0 so no edge reads as a hard line.
            const float featherX = EdgeFeather(nx, 0.40f);
            const float alpha = vertical * horizontal * featherX * featherY * 0.28f;
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
