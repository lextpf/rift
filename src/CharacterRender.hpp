#pragma once

#include <glm/glm.hpp>

class IRenderer;
class Texture;

/**
 * @file CharacterRender.hpp
 * @brief Shared character sprite draw mechanics (projection + full/half draw).
 * @ingroup Rendering
 *
 * The renderer-touching common core of the player's and NPC's Render /
 * RenderBottomHalf / RenderTopHalf methods. The per-entity bits (which sheet,
 * which UV row, atlas offset) stay on the entity; the identical projection math
 * and the perspective-suspended DrawSpriteRegion call live here so both entities
 * share one implementation. Operates only on the virtual @ref IRenderer
 * interface, so it links into the test library like the entity render code does.
 */
namespace CharacterRender
{
/// @brief Which slice of the sprite to draw (halves are used by the Y-sort pass).
enum class Part
{
    Full,        ///< Whole sprite.
    BottomHalf,  ///< Lower spriteSize.y/2 band (feet).
    TopHalf,     ///< Upper spriteSize.y/2 band (head/torso).
};

/// @brief Project a feet (bottom-center) world position to the sprite's top-left
/// screen render position, applying camera offset + elevation before projection.
glm::vec2 ComputeRenderPos(IRenderer& renderer,
                           glm::vec2 feetWorld,
                           glm::vec2 cameraPos,
                           float elevationOffset,
                           glm::vec2 spriteSize);

/// @brief Draw a full sprite or a top/bottom half at @p renderPos. Halves take
/// the upper/lower spriteSize.y/2 band of the source region. @p suspendPerspective
/// wraps the draw in a PerspectiveSuspendGuard (the position is already projected;
/// this avoids double-projection).
void DrawPart(IRenderer& renderer,
              const Texture& sheet,
              glm::vec2 renderPos,
              glm::vec2 spriteCoords,
              glm::vec2 spriteSize,
              Part part,
              bool suspendPerspective);
}  // namespace CharacterRender
