#pragma once

#include "Billboard.hpp"
#include "CharacterDirection.hpp"

#include <ecs.hpp>

#include <glm/glm.hpp>

#include <string>

class IRenderer;
class Texture;
struct Transform;
struct Elevation;
struct Facing;
struct AnimationState;
struct NpcSprite;

/**
 * @brief Free-function NPC sprite rendering over granular components.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * The half-draw mechanics behind the Y-sort pass: sheet resolution, sprite-cell
 * lookup, and the top/bottom half draws. They operate purely on the granular
 * components (read by reference) plus the @c TextureStore resolved from
 * @c world.globals().find<WorldServices>(), so the Y-sort render pass can draw
 * an NPC from its entity handle without any NPC object. Both the bundle and its
 * @c textures pointer are nullable; a world that publishes neither resolves to a
 * shared empty texture instead of failing. Renderer-touching but only via
 * @ref IRenderer, so it links into the test library.
 */
namespace NpcRender
{
/**
 * @brief Sprite-sheet cell origin (pixels) for a frame + facing (NPC row map).
 *
 * Column is `frame % CharacterConstants::WALK_FRAME_COUNT`; the row comes
 * straight from the facing via the sheet's own order, RIGHT=0, LEFT=1, DOWN=2,
 * UP=3, counted upward from the bottom of the stbi-flipped texture. Unrecognised
 * facings fall back to DOWN.
 *
 * There is deliberately no `requiresYFlip` parameter here, unlike
 * @ref PlayerRender::SpriteCoords - that order is the flipped order, so there is
 * nothing left to permute. PlayerRender needs the flag only because it starts
 * from a logical order ({DOWN, UP, LEFT, RIGHT}) and remaps into this same
 * physical layout. Callers pass the result to CharacterRender with
 * `flipY = false` either way.
 *
 * @param frame Animation frame; wrapped to the walk frame count.
 * @param dir   Facing direction.
 * @return Cell origin in pixels, Y counted as GL rows from the texture bottom.
 */
glm::vec2 SpriteCoords(int frame, CharacterDirection dir);

/**
 * @brief Select the draw sheet (shared tile atlas when bound, else the per-NPC
 * sheet from the TextureStore in globals) and, when atlas-bound, fold the atlas
 * offset into @p spriteCoords.
 *
 * @warning The returned reference is a non-owning borrow, valid for the current
 * draw only: an atlas re-pack or a map reload replaces the target's contents.
 *
 * @return The shared atlas when @c sprite.atlas is bound; otherwise the per-NPC
 *         sheet from the TextureStore in globals; or a shared empty texture when
 *         no store is published - the NPC then draws nothing.
 */
const Texture& ResolveRenderSheet(const ecs::registry& world,
                                  const NpcSprite& sprite,
                                  glm::vec2& spriteCoords);

/**
 * @brief Draw the top or bottom half of an NPC sprite for the Y-sort pass.
 *
 * Reads the five render components; resolves sheet + UVs; defers projection and
 * the sprite-region draw to @ref CharacterRender.
 *
 * @note Submit both halves back-to-back from one render-list entry (see
 * RenderDrawable.cpp). Split across two entries, a tile can sort between the
 * feet and the head.
 *
 * @param world     Registry the sheet / atlas is resolved from.
 * @param renderer  Backend the sprite is submitted to.
 * @param cameraPos Viewport top-left corner in world pixels, not the camera
 *                  centre.
 * @param topHalf   True draws the upper screen band, false the lower one.
 * @param xf        Transform supplying the feet position.
 * @param elev      Elevation; only the cosmetic @c offset lifts the sprite.
 * @param facing    Facing direction, selecting the sheet row.
 * @param anim      Animation state, selecting the walk frame.
 * @param sprite    Per-NPC sprite sheet and atlas offset.
 */
void DrawHalf(const ecs::registry& world,
              IRenderer& renderer,
              glm::vec2 cameraPos,
              bool topHalf,
              const Transform& xf,
              const Elevation& elev,
              const Facing& facing,
              const AnimationState& anim,
              const NpcSprite& sprite);

/**
 * @brief Draw an NPC as a world-space billboard.
 *
 * The 3D counterpart to @ref DrawHalf; one call rather than two, because the
 * depth buffer removes the reason for the top/bottom split (see
 * @ref CharacterRender::DrawBillboard).
 *
 * @param world       Registry the sprite sheet / atlas is resolved from.
 * @param renderer    Backend the quad is submitted to.
 * @param orientation Damped billboard orientation for the Character role.
 * @param xf          Transform supplying the feet position.
 * @param elev        Elevation; only the cosmetic @c offset is used as height,
 *                    never the plane index.
 * @param facing      Facing direction, selecting the sheet row.
 * @param anim        Animation state, selecting the walk frame.
 * @param sprite      Per-NPC sprite sheet and atlas offset.
 */
void Draw3D(const ecs::registry& world,
            IRenderer& renderer,
            const billboard::Orientation& orientation,
            const Transform& xf,
            const Elevation& elev,
            const Facing& facing,
            const AnimationState& anim,
            const NpcSprite& sprite);
}  // namespace NpcRender

/**
 * @brief Pure string utility: the NPC type identifier from a sprite path.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Filename without the @c .png extension. Carries no asset-table state of its
 * own; resolution lives in @ref AssetRegistry.
 */
namespace NpcType
{
/**
 * @brief NPC type identifier for a sprite path.
 *
 * @param path Sprite path, with forward or back slashes.
 * @return The filename with any directory prefix dropped and a trailing `.png`
 *         removed case-insensitively. Any other name is returned unchanged,
 *         including one with a different extension and the bare name `.png`.
 */
std::string FromSpritePath(const std::string& path);
}  // namespace NpcType
