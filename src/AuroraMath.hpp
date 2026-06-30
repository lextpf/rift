#pragma once

#include <cmath>

#include <glm/glm.hpp>

/// Pure, renderer-free math for the AuroraNight effect. Unit-tested in
/// tests/AuroraMathTests.cpp; no GPU or IRenderer dependency.
namespace AuroraMath
{
/// Saturated 8-stop aurora palette (emerald -> mint -> cyan -> blue -> violet
/// -> magenta -> pink -> teal). `phase` is wrapped to its fractional part, so
/// integer values return the same color (continuous loop).
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

/// Tangent angle (degrees) of the path from `prev` to `next`, for rotating a
/// ribbon segment to follow its warped curve. Returns 0 for a flat ribbon.
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

/// Travelling brightness hot-spot along a band. `segNorm` in [0,1] is the
/// position along the ribbon; the center sweeps with time at `speed`. Returns a
/// Gaussian boost in (0,1] of half-width `width`, peaking at the moving center.
/// `seed` offsets each band's phase so neighbors don't pulse in sync.
inline float SweepBoost(float segNorm, float t, float speed, float width, float seed)
{
    float center = t * speed + seed;
    center -= std::floor(center);  // cycle 0..1 along the band
    const float d = (segNorm - center) / width;
    return std::exp(-d * d);
}
}  // namespace AuroraMath
