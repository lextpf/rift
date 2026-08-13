#include "CharacterRender.hpp"

#include "IRenderer.hpp"
#include "RenderModes.hpp"
#include "SceneMath.hpp"

namespace CharacterRender
{
void DrawBillboard(IRenderer& renderer,
                   const Texture& sheet,
                   glm::vec3 footCenter,
                   glm::vec2 spriteCoords,
                   glm::vec2 spriteSize,
                   const billboard::Orientation& orientation)
{
    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    billboard::MakeQuad(footCenter, spriteSize, orientation, corners);

    // flipY = false for the same reason DrawPart uses it: these sheets, and the
    // atlas offsets baked by PackAdditionalSheets, are already in GL row space.
    // Characters draw in the opaque pass - their sprites are hard-edged cutouts,
    // so the alpha test gives them correct per-pixel occlusion with no sorting.
    renderer.DrawQuad3D(sheet,
                        corners,
                        spriteCoords,
                        spriteSize,
                        glm::vec4(1.0f),
                        renderModes::BlendMode::Alpha,
                        renderModes::DepthMode::TestAndWrite,
                        /*flipY=*/false);
}

glm::vec2 ComputeRenderPos(glm::vec2 feetWorld,
                           glm::vec2 cameraPos,
                           float elevationOffset,
                           glm::vec2 spriteSize)
{
    // Screen-space bottom-center, elevation raising the sprite on stairs, then
    // converted from bottom-center to top-left.
    glm::vec2 bottomCenter = feetWorld - cameraPos;
    bottomCenter.y -= elevationOffset;
    return bottomCenter - glm::vec2(spriteSize.x * 0.5f, spriteSize.y);
}

void DrawPart(IRenderer& renderer,
              const Texture& sheet,
              glm::vec2 renderPos,
              glm::vec2 spriteCoords,
              glm::vec2 spriteSize,
              Part part)
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

    renderer.DrawSpriteRegion(
        sheet, drawPos, drawSize, drawCoords, drawSize, 0.0f, glm::vec3(1.0f), useAtlasFlip);
}
}  // namespace CharacterRender
