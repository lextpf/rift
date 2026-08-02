#include "IRenderer.hpp"

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

namespace
{
// Maps a "headline 1.0x" call onto the body atlas for backends without a
// separate high-res atlas - still visibly large, just blurry. Vulkan is the only
// consumer today; the OpenGL backend overrides both headline entry points.
//
// 4.0 is the OpenGL backend's HEADLINE_FONT_LOGICAL_PIXEL_SIZE (96) over its
// BODY_FONT_LOGICAL_PIXEL_SIZE (24), duplicated here as a literal. Keep it in step
// with those two constants, or DrawTextLarge and GetTextWidthLarge silently
// mis-size on every backend that does not override them.
constexpr float kHeadlineFallbackScale = 4.0f;
}  // namespace

void IRenderer::DrawTextLarge(const std::string& text,
                              glm::vec2 position,
                              float scale,
                              glm::vec3 color,
                              float outlineSize,
                              float alpha)
{
    DrawText(text, position, scale * kHeadlineFallbackScale, color, outlineSize, alpha);
}

float IRenderer::GetTextWidthLarge(const std::string& text, float scale) const
{
    return GetTextWidth(text, scale * kHeadlineFallbackScale);
}
