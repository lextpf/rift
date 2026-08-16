#pragma once

#include <cmath>

#include <glm/glm.hpp>

/**
 * @brief Pure, renderer-free math for the Aurora weather effect.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Every function here is a self-contained value transform with no GPU or
 * @ref IRenderer dependency, unit-tested in tests/AuroraMathTests.cpp.
 */
namespace AuroraMath
{
/**
 * @brief Sample the saturated 8-stop aurora palette at a looping @p phase.
 *
 * Stops run emerald -> teal -> cyan -> azure -> violet -> magenta -> rose ->
 * gold and wrap continuously, so integer @p phase values return the same color
 * (a seamless loop).
 *
 * @param phase  Palette position; only the fractional part is used, so the
 *               loop repeats every 1.0.
 * @return       Linearly interpolated RGB color at @p phase.
 */
inline glm::vec3 AuroraColor(float phase)
{
    float p = phase - std::floor(phase);
    constexpr int kStops = 8;
    const glm::vec3 stops[kStops] = {
        {0.15f, 1.00f, 0.45f},  // emerald
        {0.10f, 0.95f, 0.80f},  // teal
        {0.25f, 0.85f, 1.00f},  // cyan
        {0.35f, 0.55f, 1.00f},  // azure blue
        {0.60f, 0.35f, 1.00f},  // violet
        {0.95f, 0.45f, 1.00f},  // magenta
        {1.00f, 0.50f, 0.70f},  // rose
        {1.00f, 0.82f, 0.50f},  // warm gold
    };
    float pos = p * static_cast<float>(kStops);
    int i = static_cast<int>(pos) % kStops;
    int j = (i + 1) % kStops;
    float f = pos - std::floor(pos);
    return glm::mix(stops[i], stops[j], f);
}

/**
 * @brief Tangent angle (degrees) of the path from @p prev to @p next.
 *
 * Used to rotate a ribbon segment so it follows its warped curve.
 *
 * @param prev  Previous point on the ribbon path.
 * @param next  Next point on the ribbon path.
 * @return      Direction angle of @p prev -> @p next in degrees, or 0 for a
 *              zero-length (flat) segment.
 */
inline float TangentAngleDeg(glm::vec2 prev, glm::vec2 next)
{
    const float dx = next.x - prev.x;
    const float dy = next.y - prev.y;
    if (dx == 0.0f && dy == 0.0f)
    {
        return 0.0f;
    }
    constexpr float kRadToDeg = 57.29577951308232f;
    return std::atan2(dy, dx) * kRadToDeg;
}

/**
 * @brief Travelling brightness hot-spot along a band (Gaussian sweep).
 *
 * A single bright center sweeps along the ribbon over time; @p seed offsets
 * each band's phase so neighboring bands don't pulse in sync.
 *
 * @f[
 *   B(s,t) = \exp\left(-\left(\frac{s - \mathrm{frac}(vt+\sigma)}{w}\right)^2\right)
 * @f]
 *
 * with s = @p segNorm, v = @p speed, w = @p width and sigma = @p seed. The
 * distance s - center is linear, not wrap-aware, so the hot-spot does not cross
 * the seam: it fades out near segNorm 1 and re-enters at segNorm 0.
 *
 * @pre @p width > 0. A width of 0 divides by zero and returns NaN.
 *
 * @param segNorm  Position along the ribbon, in [0,1].
 * @param t        Time (seconds) driving the sweep.
 * @param speed    Center travel rate, in band lengths per second (one full
 *                 sweep every 1/speed seconds).
 * @param width    Gaussian 1/e radius of the hot-spot, in band-length units.
 * @param seed     Per-band phase offset so neighbors desync.
 * @return         Brightness boost in (0,1], peaking at the moving center.
 */
inline float SweepBoost(float segNorm, float t, float speed, float width, float seed)
{
    float center = t * speed + seed;
    center -= std::floor(center);  // cycle 0..1 along the band
    const float d = (segNorm - center) / width;
    return std::exp(-d * d);
}
}  // namespace AuroraMath
