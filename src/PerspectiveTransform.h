#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

/**
 * @namespace perspectiveTransform
 * @brief Utility functions for 2D perspective projection effects.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Provides point and quad-corner transformation routines used by the
 * rendering backends to achieve vanishing-point depth scaling and
 * spherical globe curvature on an otherwise flat 2D tile map.
 *
 * @par Projection Modes
 * | Mode            | Globe Curvature | Vanishing Point |
 * |-----------------|-----------------|-----------------|
 * | VanishingPoint  | No              | Yes             |
 * | Globe           | Yes             | No              |
 * | Fisheye         | Yes             | Yes             |
 *
 * @par Globe Curvature
 * Maps each point onto a virtual sphere of radius @f$ R @f$. A point at
 * distance @f$ d @f$ from the screen center is displaced to:
 * @f[
 * d' = R \cdot \sin\!\bigl(\tfrac{d}{R}\bigr)
 * @f]
 * preserving the angle from center while compressing distant points.
 *
 * @par Vanishing Point Scaling
 * Scales each point toward a horizon line to simulate depth:
 * @f[
 * s = h_s + (1 - h_s) \cdot \frac{y - h_y}{H - h_y}
 * @f]
 * where @f$ h_s @f$ is the horizon scale, @f$ h_y @f$ is the horizon Y
 * position, and @f$ H @f$ is the screen height.
 *
 * @see IRenderer::ProjectPoint, IRenderer::SetVanishingPointPerspective,
 *      IRenderer::SetGlobePerspective, IRenderer::SetFisheyePerspective
 */
namespace perspectiveTransform
{

// Oval globe tuning: slightly larger globe with gentler curvature.
inline constexpr double kGlobeRadiusXScale = 1.35;
inline constexpr double kGlobeRadiusYScale = 0.98;

// Baseline perspective cull scales (historical tuning from pre-oval globe).
inline constexpr double kBaseCullWidthScale = 1.5;
inline constexpr double kBaseCullHeightScale = 1.0;
inline constexpr double kCullRefGlobeRadiusXScale = 1.0;
inline constexpr double kCullRefGlobeRadiusYScale = 0.72;

/// Return the world-cull width multiplier for perspective rendering.
inline double GetPerspectiveCullWidthScale(bool hasGlobe)
{
    if (!hasGlobe)
        return kBaseCullWidthScale;

    double ovalFactor = std::max(0.25, kGlobeRadiusXScale / kCullRefGlobeRadiusXScale);
    return kBaseCullWidthScale * ovalFactor;
}

/// Return the world-cull height multiplier for perspective rendering.
inline double GetPerspectiveCullHeightScale(bool hasGlobe)
{
    if (!hasGlobe)
        return kBaseCullHeightScale;

    double ovalFactor = std::max(0.25, kGlobeRadiusYScale / kCullRefGlobeRadiusYScale);
    return kBaseCullHeightScale * ovalFactor;
}

/**
 * @struct Params
 * @brief Configuration for a perspective transformation pass.
 *
 * Populated by the renderer from its current PerspectiveState and passed
 * to TransformPoint / TransformCorners.
 *
 * @see IRenderer::PerspectiveState
 */
struct Params
{
    double centerX;        ///< Screen center X (viewWidth / 2).
    double centerY;        ///< Screen center Y (viewHeight / 2).
    double horizonY;       ///< Y position of the horizon line.
    double screenHeight;   ///< Viewport height in pixels.
    double horizonScale;   ///< Scale factor at the horizon (0-1).
    double sphereRadiusX;  ///< Horizontal globe radius in pixels.
    double sphereRadiusY;  ///< Vertical globe radius in pixels.
};

/**
 * @brief Transform a single point through the active projection.
 *
 * Compile-time specialization eliminates branches in the inner loop.
 * The 4 combinations (globe x vanishing) generate separate code paths
 * with dead branches removed at compile time via `if constexpr`.
 *
 * @tparam ApplyGlobe     Enable spherical globe curvature (step 1).
 * @tparam ApplyVanishing Enable vanishing-point depth scaling (step 2).
 * @param[in,out] x  Screen-space X coordinate (modified in place).
 * @param[in,out] y  Screen-space Y coordinate (modified in place).
 * @param         p  Projection parameters.
 */
template <bool ApplyGlobe, bool ApplyVanishing>
inline void TransformPoint(double& x, double& y, const Params& p)
{
    // Step 1: Apply globe curvature using true spherical projection
    if constexpr (ApplyGlobe)
    {
        double Rx = std::max(1e-5, p.sphereRadiusX);
        double Ry = std::max(1e-5, p.sphereRadiusY);
        double dx = x - p.centerX;
        double dy = y - p.centerY;
        double ndx = dx / Rx;
        double ndy = dy / Ry;
        double dNorm = std::sqrt(ndx * ndx + ndy * ndy);

        if (dNorm > 0.001)
        {
            double ratio = std::sin(dNorm) / dNorm;
            x = p.centerX + dx * ratio;
            y = p.centerY + dy * ratio;
        }
    }

    // Step 2: Apply vanishing point perspective
    if constexpr (ApplyVanishing)
    {
        double denom = p.screenHeight - p.horizonY;
        if (denom < 1e-5)
            return;

        double depthNorm = std::max(0.0, std::min(1.0, (y - p.horizonY) / denom));
        double scaleFactor = p.horizonScale + (1.0 - p.horizonScale) * depthNorm;

        double dx = x - p.centerX;
        x = p.centerX + dx * scaleFactor;

        double dy = y - p.horizonY;
        y = p.horizonY + dy * scaleFactor;
    }
}

/// @brief Function pointer type for a TransformPoint specialization.
using TransformFn = void (*)(double&, double&, const Params&);

/**
 * @brief Compile-time dispatch table indexed by [applyGlobe][applyVanishing].
 *
 * Select the appropriate specialization once per frame, then call it
 * in the per-vertex inner loop without any runtime branch overhead.
 *
 * @code{.cpp}
 * auto fn = kTransformDispatch[hasGlobe][hasVanishing];
 * for (auto& vertex : vertices)
 *     fn(vertex.x, vertex.y, params);  // no branches
 * @endcode
 */
inline constexpr TransformFn kTransformDispatch[2][2] = {
    {TransformPoint<false, false>, TransformPoint<false, true>},
    {TransformPoint<true, false>, TransformPoint<true, true>},
};

/// @brief Select the appropriate TransformPoint specialization at runtime.
inline TransformFn GetTransformFn(bool applyGlobe, bool applyVanishing)
{
    return kTransformDispatch[applyGlobe][applyVanishing];
}

/**
 * @brief Transform the four corners of a quad in place.
 *
 * Convenience wrapper that converts each corner to double precision,
 * calls the appropriate TransformPoint specialization, and converts back.
 *
 * @tparam ApplyGlobe     Enable spherical globe curvature.
 * @tparam ApplyVanishing Enable vanishing-point depth scaling.
 * @param[in,out] corners  Array of 4 screen-space positions [TL, TR, BR, BL].
 * @param         p        Projection parameters.
 */
template <bool ApplyGlobe, bool ApplyVanishing>
inline void TransformCorners(glm::vec2 corners[4], const Params& p)
{
    double dCorners[4][2];
    for (int i = 0; i < 4; i++)
    {
        dCorners[i][0] = static_cast<double>(corners[i].x);
        dCorners[i][1] = static_cast<double>(corners[i].y);
    }

    for (int i = 0; i < 4; i++)
    {
        TransformPoint<ApplyGlobe, ApplyVanishing>(dCorners[i][0], dCorners[i][1], p);
    }

    for (int i = 0; i < 4; i++)
    {
        corners[i].x = static_cast<float>(dCorners[i][0]);
        corners[i].y = static_cast<float>(dCorners[i][1]);
    }
}

/// @brief Function pointer type for a TransformCorners specialization.
using TransformCornersFn = void (*)(glm::vec2[4], const Params&);

/// @brief Compile-time dispatch table for TransformCorners.
inline constexpr TransformCornersFn kTransformCornersDispatch[2][2] = {
    {TransformCorners<false, false>, TransformCorners<false, true>},
    {TransformCorners<true, false>, TransformCorners<true, true>},
};

/// @brief Select the appropriate TransformCorners specialization at runtime.
inline TransformCornersFn GetTransformCornersFn(bool applyGlobe, bool applyVanishing)
{
    return kTransformCornersDispatch[applyGlobe][applyVanishing];
}

}  // namespace perspectiveTransform
