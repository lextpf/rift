#include "IRenderer.h"
#include "MathConstants.h"
#include "PerspectiveTransform.h"

#include <cmath>

void IRenderer::RotateCorners(glm::vec2 corners[4], glm::vec2 size, float rotation)
{
    if (std::abs(rotation) > 1e-6f)
    {
        float rad = glm::radians(rotation);
        float cosR = std::cos(rad);
        float sinR = std::sin(rad);
        glm::vec2 center = size * 0.5f;

        for (int i = 0; i < 4; i++)
        {
            glm::vec2 p = corners[i] - center;
            corners[i] =
                glm::vec2(p.x * cosR - p.y * sinR + center.x, p.x * sinR + p.y * cosR + center.y);
        }
    }
}

void IRenderer::ApplyPerspective(glm::vec2 corners[4]) const
{
    if (m_Persp.enabled && !m_PerspectiveSuspended && m_Persp.viewHeight > 0.0f)
    {
        bool hasGlobe =
            (m_Persp.mode == ProjectionMode::Globe || m_Persp.mode == ProjectionMode::Fisheye);
        bool hasVanishing = (m_Persp.mode == ProjectionMode::VanishingPoint ||
                             m_Persp.mode == ProjectionMode::Fisheye);

        perspectiveTransform::Params p;
        p.centerX = static_cast<double>(m_Persp.viewWidth) * 0.5;
        p.centerY = static_cast<double>(m_Persp.viewHeight) * 0.5;
        p.horizonY = static_cast<double>(m_Persp.horizonY);
        p.screenHeight = static_cast<double>(m_Persp.viewHeight);
        p.horizonScale = static_cast<double>(m_Persp.horizonScale);
        double baseR = static_cast<double>(m_Persp.sphereRadius);
        p.sphereRadiusX = baseR * perspectiveTransform::kGlobeRadiusXScale;
        p.sphereRadiusY = baseR * perspectiveTransform::kGlobeRadiusYScale;

        perspectiveTransform::GetTransformCornersFn(hasGlobe, hasVanishing)(corners, p);
    }
}

void IRenderer::SetVanishingPointPerspective(
    bool enabled, float horizonY, float horizonScale, float viewWidth, float viewHeight)
{
    m_Persp.enabled = enabled;
    m_Persp.mode = ProjectionMode::VanishingPoint;
    m_Persp.horizonY = horizonY;
    m_Persp.horizonScale = horizonScale;
    m_Persp.viewWidth = viewWidth;
    m_Persp.viewHeight = viewHeight;
}

void IRenderer::SetGlobePerspective(bool enabled,
                                    float sphereRadius,
                                    float viewWidth,
                                    float viewHeight)
{
    m_Persp.enabled = enabled;
    m_Persp.mode = ProjectionMode::Globe;
    m_Persp.sphereRadius = sphereRadius;
    m_Persp.horizonY = 0.0f;
    m_Persp.horizonScale = 1.0f;
    m_Persp.viewWidth = viewWidth;
    m_Persp.viewHeight = viewHeight;
}

void IRenderer::SetFisheyePerspective(bool enabled,
                                      float sphereRadius,
                                      float horizonY,
                                      float horizonScale,
                                      float viewWidth,
                                      float viewHeight)
{
    m_Persp.enabled = enabled;
    m_Persp.mode = ProjectionMode::Fisheye;
    m_Persp.sphereRadius = sphereRadius;
    m_Persp.horizonY = horizonY;
    m_Persp.horizonScale = horizonScale;
    m_Persp.viewWidth = viewWidth;
    m_Persp.viewHeight = viewHeight;
}

void IRenderer::SuspendPerspective(bool suspend)
{
    m_PerspectiveSuspended = suspend;
}

glm::vec2 IRenderer::ProjectPoint(const glm::vec2& p) const
{
    const auto& s = GetPerspectiveState();
    if (!s.enabled)
        return p;

    double resultX = static_cast<double>(p.x);
    double resultY = static_cast<double>(p.y);

    bool hasGlobe = (s.mode == ProjectionMode::Globe || s.mode == ProjectionMode::Fisheye);
    bool hasVanishing =
        (s.mode == ProjectionMode::VanishingPoint || s.mode == ProjectionMode::Fisheye);

    perspectiveTransform::Params params;
    params.centerX = static_cast<double>(s.viewWidth) * 0.5;
    params.centerY = static_cast<double>(s.viewHeight) * 0.5;
    params.horizonY = static_cast<double>(s.horizonY);
    params.screenHeight = static_cast<double>(s.viewHeight);
    params.horizonScale = static_cast<double>(s.horizonScale);
    double baseR = static_cast<double>(s.sphereRadius);
    params.sphereRadiusX = baseR * perspectiveTransform::kGlobeRadiusXScale;
    params.sphereRadiusY = baseR * perspectiveTransform::kGlobeRadiusYScale;

    perspectiveTransform::GetTransformFn(hasGlobe, hasVanishing)(resultX, resultY, params);

    return glm::vec2(static_cast<float>(resultX), static_cast<float>(resultY));
}

glm::vec2 IRenderer::ComputeBuildingVertex(const glm::vec2& baseLeft,
                                           const glm::vec2& baseRight,
                                           float u,
                                           float v,
                                           float heightWorld) const
{
    const auto& s = GetPerspectiveState();

    // Linear interpolation along base for the base point at parameter u
    glm::vec2 basePoint = baseLeft + u * (baseRight - baseLeft);

    if (!s.enabled)
    {
        // No projection: simple 2D extrusion (v goes upward, negative Y in screen space)
        return glm::vec2(basePoint.x, basePoint.y - v * heightWorld);
    }

    // Project the base point to get its position on the sphere surface
    glm::vec2 projectedBase = ProjectPoint(basePoint);

    if (v < 0.0001f)
    {
        // At base level, just return the projected base (pinned to sphere)
        return projectedBase;
    }

    float height = v * heightWorld;

    // Compute height scale factor based on vanishing point perspective
    // This makes buildings near horizon appear shorter (proper perspective)
    float heightScale = 1.0f;
    bool hasVanishing =
        (s.mode == ProjectionMode::VanishingPoint || s.mode == ProjectionMode::Fisheye);

    if (hasVanishing)
    {
        float horizonY = s.horizonY;
        float screenHeight = s.viewHeight;
        float horizonScale = s.horizonScale;

        float denom = screenHeight - horizonY;
        if (denom > 1e-5f)
        {
            float t = (projectedBase.y - horizonY) / denom;
            t = std::max(0.0f, std::min(1.0f, t));
            heightScale = horizonScale + (1.0f - horizonScale) * t;
        }
    }

    // Keep no-projection structures visually stable in globe mode:
    // use only a fraction of vanishing shrink to avoid "dragging" as camera moves.
    heightScale = 1.0f + (heightScale - 1.0f) * 0.40f;

    // Apply scaled height - rise straight up from projected base
    float scaledHeight = height * heightScale;

    // For globe mode, also compute where this point would be if fully projected
    // (conforming completely to sphere). Blend between rigid and conforming.
    bool hasGlobe = (s.mode == ProjectionMode::Globe || s.mode == ProjectionMode::Fisheye);

    if (hasGlobe)
    {
        // Fully conforming position: project the unprojected height position
        glm::vec2 unprojectedAtHeight(basePoint.x, basePoint.y - height);
        glm::vec2 fullyConforming = ProjectPoint(unprojectedAtHeight);

        // Rigid position: straight up from projected base with scaled height
        glm::vec2 rigidUp(projectedBase.x, projectedBase.y - scaledHeight);

        // Blend: 0 = fully rigid (straight up), 1 = fully conforming (follows sphere).
        // Keep this very low so structures stay anchored while still getting subtle curvature.
        constexpr float kBaseConformBlend = 0.04f;
        float conformBlend = kBaseConformBlend;

        // Fade conforming toward the globe edge where spherical lateral pull is strongest.
        // This keeps far no-projection structures visually anchored instead of drifting.
        double centerX = static_cast<double>(s.viewWidth) * 0.5;
        double centerY = static_cast<double>(s.viewHeight) * 0.5;
        double baseR = static_cast<double>(s.sphereRadius);
        double Rx = std::max(1e-5, baseR * perspectiveTransform::kGlobeRadiusXScale);
        double Ry = std::max(1e-5, baseR * perspectiveTransform::kGlobeRadiusYScale);
        double dx = static_cast<double>(basePoint.x) - centerX;
        double dy = static_cast<double>(basePoint.y) - centerY;
        double dNorm = std::sqrt((dx * dx) / (Rx * Rx) + (dy * dy) / (Ry * Ry));
        constexpr double halfPi = rift::Pi * 0.5;
        float edgeT = static_cast<float>(dNorm / halfPi);
        edgeT = std::max(0.0f, std::min(1.0f, edgeT));

        auto smoothstep = [](float e0, float e1, float x) -> float
        {
            float t = (x - e0) / std::max(1e-5f, e1 - e0);
            t = std::max(0.0f, std::min(1.0f, t));
            return t * t * (3.0f - 2.0f * t);
        };

        float edgeDampen = 1.0f - smoothstep(0.45f, 0.95f, edgeT);
        conformBlend *= edgeDampen;

        // Suppress lateral conforming (X) to prevent "staying in view" drag at globe edges.
        float finalX = rigidUp.x;
        float finalY = rigidUp.y + conformBlend * (fullyConforming.y - rigidUp.y);

        return glm::vec2(finalX, finalY);
    }
    else
    {
        // Vanishing point only: just rise straight up with scaled height
        return glm::vec2(projectedBase.x, projectedBase.y - scaledHeight);
    }
}

bool IRenderer::IsPointBehindSphere(const glm::vec2& p) const
{
    const auto& s = GetPerspectiveState();
    if (!s.enabled)
        return false;

    bool hasGlobe = (s.mode == ProjectionMode::Globe || s.mode == ProjectionMode::Fisheye);
    if (!hasGlobe)
        return false;

    double centerX = static_cast<double>(s.viewWidth) * 0.5;
    double centerY = static_cast<double>(s.viewHeight) * 0.5;
    double baseR = static_cast<double>(s.sphereRadius);
    double Rx = baseR * perspectiveTransform::kGlobeRadiusXScale;
    double Ry = baseR * perspectiveTransform::kGlobeRadiusYScale;

    double dx = static_cast<double>(p.x) - centerX;
    double dy = static_cast<double>(p.y) - centerY;
    double dNorm = std::sqrt((dx * dx) / (Rx * Rx) + (dy * dy) / (Ry * Ry));

    // Visible edge is at normalized angular distance pi/2 in oval-globe space.
    // Points beyond this are on the back of the globe.
    constexpr double halfPi = rift::Pi * 0.5;
    return dNorm > halfPi;
}

bool IRenderer::IsPointInExpandedViewport(const glm::vec2& p) const
{
    const auto& s = GetPerspectiveState();
    if (!s.enabled)
        return false;

    float safeHorizonScale = std::max(s.horizonScale, 0.001f);
    float expansion = 1.0f / safeHorizonScale;
    bool hasGlobe = (s.mode == ProjectionMode::Globe || s.mode == ProjectionMode::Fisheye);
    float cullWidthScale =
        static_cast<float>(perspectiveTransform::GetPerspectiveCullWidthScale(hasGlobe));
    float cullHeightScale =
        static_cast<float>(perspectiveTransform::GetPerspectiveCullHeightScale(hasGlobe));
    float expandedWidth = s.viewWidth * expansion * cullWidthScale;
    float expandedHeight = s.viewHeight * expansion * cullHeightScale;
    float widthPadding = (expandedWidth - s.viewWidth) * 0.5f;
    float heightPadding = (expandedHeight - s.viewHeight) * 0.5f;

    return p.x >= -widthPadding && p.x <= s.viewWidth + widthPadding && p.y >= -heightPadding &&
           p.y <= s.viewHeight + heightPadding;
}

glm::vec2 IRenderer::ProjectPointSafe(const glm::vec2& p) const
{
    if (IsPointInExpandedViewport(p))
        return ProjectPoint(p);
    return p;
}
