#include "CharacterRender.hpp"

#include "IRenderer.hpp"

namespace CharacterRender
{
glm::vec2 ComputeRenderPos(IRenderer& renderer,
                           glm::vec2 feetWorld,
                           glm::vec2 cameraPos,
                           float elevationOffset,
                           glm::vec2 spriteSize)
{
    // Screen-space bottom-center, elevation applied BEFORE projection (moves the
    // sprite up on stairs), then converted from bottom-center to top-left.
    glm::vec2 bottomCenter = feetWorld - cameraPos;
    bottomCenter.y -= elevationOffset;
    bottomCenter = renderer.ProjectPointSafe(bottomCenter);
    return bottomCenter - glm::vec2(spriteSize.x * 0.5f, spriteSize.y);
}

void DrawPart(IRenderer& renderer,
              const Texture& sheet,
              glm::vec2 renderPos,
              glm::vec2 spriteCoords,
              glm::vec2 spriteSize,
              Part part,
              bool suspendPerspective)
{
    glm::vec2 drawPos = renderPos;
    glm::vec2 drawSize = spriteSize;
    glm::vec2 drawCoords = spriteCoords;

    if (part == Part::BottomHalf)
    {
        // Lower band: shift the destination down by half, sample the same UV top.
        const float halfH = spriteSize.y * 0.5f;
        drawPos.y += halfH;
        drawSize.y = halfH;
    }
    else if (part == Part::TopHalf)
    {
        // Upper band: keep the destination top, sample the upper half of the cell.
        const float halfH = spriteSize.y * 0.5f;
        drawSize.y = halfH;
        drawCoords.y += halfH;
    }

    // The atlas offset baked by PackAdditionalSheets is already in GL-row space, so
    // the renderer's UV math lands on the packed region with flipY off.
    constexpr bool useAtlasFlip = false;

    if (suspendPerspective)
    {
        // Position is already projected; don't double-project.
        IRenderer::PerspectiveSuspendGuard guard(renderer);
        renderer.DrawSpriteRegion(
            sheet, drawPos, drawSize, drawCoords, drawSize, 0.0f, glm::vec3(1.0f), useAtlasFlip);
    }
    else
    {
        renderer.DrawSpriteRegion(
            sheet, drawPos, drawSize, drawCoords, drawSize, 0.0f, glm::vec3(1.0f), useAtlasFlip);
    }
}
}  // namespace CharacterRender
