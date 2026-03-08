#include "IRenderer.h"
#include "PerspectiveTransform.h"

#include <cmath>

void IRenderer::RotateCorners(glm::vec2 corners[4], glm::vec2 size, float rotation)
{
    if (rotation != 0.0f)
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
        perspectiveTransform::Params p;
        p.applyGlobe =
            (m_Persp.mode == ProjectionMode::Globe || m_Persp.mode == ProjectionMode::Fisheye);
        p.applyVanishing = (m_Persp.mode == ProjectionMode::VanishingPoint ||
                            m_Persp.mode == ProjectionMode::Fisheye);
        p.centerX = static_cast<double>(m_Persp.viewWidth) * 0.5;
        p.centerY = static_cast<double>(m_Persp.viewHeight) * 0.5;
        p.horizonY = static_cast<double>(m_Persp.horizonY);
        p.screenHeight = static_cast<double>(m_Persp.viewHeight);
        p.horizonScale = static_cast<double>(m_Persp.horizonScale);
        p.sphereRadius = static_cast<double>(m_Persp.sphereRadius);
        perspectiveTransform::TransformCorners(corners, p);
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
