#pragma once

#include "PerspectiveTransform.hpp"

#include <algorithm>
#include <cmath>

/**
 * @brief Single-precision mirror of the GLSL perspective math.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * `shaders/Geometry.vert::applyPerspective` runs in 32-bit float; the
 * canonical CPU math in `PerspectiveTransform.hpp` uses 64-bit double for
 * `ProjectPoint` / `ProjectPointSafe`. This header reproduces the GLSL
 * code path on the CPU so tests can pin the precision delta between the
 * two implementations at a known tolerance.
 *
 * The shader version is single source of truth at runtime; this header is
 * only used by `tests/PerspectiveTransformFloatTests.cpp` to guard against
 * regressions when the shader math is edited.
 */
namespace perspectiveTransformFloat
{

struct ParamsF
{
    float centerX;
    float centerY;
    float horizonY;
    float screenHeight;
    float horizonScale;
    float sphereRadiusX;
    float sphereRadiusY;
};

inline ParamsF MakeParamsF(
    float viewWidth, float viewHeight, float horizonY, float horizonScale, float sphereRadius)
{
    ParamsF p{};
    p.centerX = viewWidth * 0.5f;
    p.centerY = viewHeight * 0.5f;
    p.horizonY = horizonY;
    p.screenHeight = viewHeight;
    p.horizonScale = horizonScale;
    p.sphereRadiusX = sphereRadius * static_cast<float>(perspectiveTransform::kGlobeRadiusXScale);
    p.sphereRadiusY = sphereRadius * static_cast<float>(perspectiveTransform::kGlobeRadiusYScale);
    return p;
}

/// Mirrors `applyPerspective(vec2)` in shaders/Geometry.vert. Guards must match
/// (`dNorm > 0.001`, `denom >= 1e-5`, `max(R, 1e-5)`) so the parity test pins
/// what the shader actually executes, not a divergent CPU formulation.
inline void TransformPointFloat(
    float& x, float& y, bool applyGlobe, bool applyVanishing, const ParamsF& p)
{
    if (applyGlobe)
    {
        const float Rx = std::max(1e-5f, p.sphereRadiusX);
        const float Ry = std::max(1e-5f, p.sphereRadiusY);
        const float dx = x - p.centerX;
        const float dy = y - p.centerY;
        const float ndx = dx / Rx;
        const float ndy = dy / Ry;
        const float dNorm = std::sqrt(ndx * ndx + ndy * ndy);

        if (dNorm > 0.001f)
        {
            const float ratio = std::sin(dNorm) / dNorm;
            x = p.centerX + dx * ratio;
            y = p.centerY + dy * ratio;
        }
    }

    if (applyVanishing)
    {
        const float denom = p.screenHeight - p.horizonY;
        if (denom >= 1e-5f)
        {
            float depthNorm = (y - p.horizonY) / denom;
            depthNorm = std::clamp(depthNorm, 0.0f, 1.0f);
            const float scaleFactor = p.horizonScale + (1.0f - p.horizonScale) * depthNorm;

            const float dx = x - p.centerX;
            x = p.centerX + dx * scaleFactor;

            const float dy = y - p.horizonY;
            y = p.horizonY + dy * scaleFactor;
        }
    }
}

}  // namespace perspectiveTransformFloat
