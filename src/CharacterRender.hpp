#pragma once

#include "Billboard.hpp"

#include <glm/glm.hpp>

class IRenderer;
class Texture;

/**
 * @brief Shared character sprite draw mechanics (projection + full/half draw).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * The renderer-touching common core of @ref PlayerRender::DrawHalf,
 * @ref NpcRender::DrawHalf and their Draw3D counterparts. The per-entity bits
 * (which sheet, which UV row, atlas offset) stay with the entity; the identical
 * projection math and the single DrawSpriteRegion call live here so both
 * entities share one implementation. Operates only on the virtual
 * @ref IRenderer interface, so it links into the test library like the entity
 * render code does.
 */
namespace CharacterRender
{
/**
 * @enum Part
 * @brief Which slice of the sprite to draw (halves are used by the Y-sort pass).
 * @ingroup Rendering
 *
 * The screen bands are the obvious ones - BottomHalf is the lower half of the
 * sprite rect, TopHalf the upper half. The source offsets look inverted, and
 * that is the trap: BottomHalf samples the cell at offset +0 while TopHalf adds
 * half a cell.
 *
 * That is correct, because character sheets are loaded stbi-flipped (bottom-up)
 * and drawn with `flipY = false`, so `spriteCoords.y` counts rows upward from
 * the bottom of the texture, not downward from the top the way
 * @ref IRenderer::DrawSpriteRegion's `texCoord` parameter is otherwise described.
 * In that space a larger offset means higher on screen, so +0 is exactly the feet.
 *
 * @verbatim
 *   one atlas cell (H = spriteSize.y), v increases upward in GL row space
 *
 *   v = coords.y + H     +------------------+  <- cell top
 *                        |    head/torso    |   Part::TopHalf
 *                        |  source +H/2     |   -> screen y = renderPos.y
 *   v = coords.y + H/2   +------------------+
 *                        |       feet       |   Part::BottomHalf
 *                        |  source +0       |   -> screen y = renderPos.y + H/2
 *   v = coords.y         +------------------+  <- cell bottom = the anchor row
 * @endverbatim
 */
enum class Part
{
    Full,        ///< Whole sprite.
    BottomHalf,  ///< Feet: lower screen band, sampled at source offset +0.
    TopHalf,     ///< Head/torso: upper screen band, sampled at source offset +H/2.
};

/**
 * @brief Map a feet (bottom-center) world position to the sprite's top-left
 * screen render position, applying camera offset and elevation.
 *
 * @param feetWorld       Bottom-center anchor in world pixels.
 * @param cameraPos       World position of the viewport's top-left corner.
 * @param elevationOffset Upward lift in pixels (positive = higher).
 * @param spriteSize      Sprite cell size in pixels.
 * @return Top-left screen position to draw the sprite at.
 */
glm::vec2 ComputeRenderPos(glm::vec2 feetWorld,
                           glm::vec2 cameraPos,
                           float elevationOffset,
                           glm::vec2 spriteSize);

/**
 * @brief Draw a full sprite or a top/bottom half at @p renderPos.
 *
 * See @ref Part for which source band each half takes and why the offsets look
 * inverted.
 *
 * Always draws with `flipY = false`: these sheets (and the atlas offsets baked by
 * PackAdditionalSheets) are already in GL row space.
 *
 * @param renderer      Backend the sprite is submitted to.
 * @param sheet         Sprite sheet or shared atlas to sample from.
 * @param renderPos     Top-left of the FULL sprite rect, not of the half.
 *                      DrawPart derives the half's screen band from it.
 * @param spriteCoords  Cell origin in the sheet, in GL rows from the bottom.
 * @param spriteSize    Full cell size; the halves are spriteSize.y/2 tall.
 * @param part          Which slice to draw.
 */
void DrawPart(IRenderer& renderer,
              const Texture& sheet,
              glm::vec2 renderPos,
              glm::vec2 spriteCoords,
              glm::vec2 spriteSize,
              Part part);

/**
 * @brief Draw a character as a single world-space billboard.
 *
 * The 3D counterpart to @ref DrawPart, and deliberately **one** quad rather than
 * the two halves the flat path emits. The split is not what makes a character
 * atomic in the painter's-algorithm queue: atomicity comes from one Drawable per
 * character, whose thunk emits both halves back-to-back (RenderDrawable.cpp), so
 * no tile can sort between feet and head. Stacking the two bands is exactly
 * equivalent to one full-cell quad anyway (the lower band samples the lower half
 * of the cell, the upper band the upper half), and a depth buffer resolves
 * occlusion per pixel, so the 3D path keeps neither.
 *
 * @pre Submit inside opaque pass A1. The quad is issued with
 * @c renderModes::BlendMode::Alpha and @c renderModes::DepthMode::TestAndWrite,
 * and relies on the shader's @c OPAQUE_ALPHA_CUTOFF discard for per-pixel
 * occlusion. Submitted in the translucent pass it occludes wrongly and silently.
 *
 * @param renderer     Backend the quad is submitted to.
 * @param sheet        Sprite sheet or shared atlas to sample from.
 * @param footCenter   Scene-space bottom-center anchor (the character's feet),
 *                     already lifted by any elevation offset.
 * @param spriteCoords Cell origin in the sheet, in GL rows from the bottom.
 * @param spriteSize   Full cell size in world pixels.
 * @param orientation  Damped billboard orientation, from @c billboard::Orient.
 */
void DrawBillboard(IRenderer& renderer,
                   const Texture& sheet,
                   glm::vec3 footCenter,
                   glm::vec2 spriteCoords,
                   glm::vec2 spriteSize,
                   const billboard::Orientation& orientation);
}  // namespace CharacterRender
