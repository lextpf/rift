#pragma once

#include "Billboard.hpp"
#include "CharacterDirection.hpp"

#include <ecs.hpp>

#include <glm/glm.hpp>

class IRenderer;
class Texture;
struct Transform;
struct Elevation;
struct Facing;
struct AnimationState;
struct PlayerModes;
struct PlayerSprite;

/**
 * @brief Free-function player sprite rendering over granular components.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * The player analog of NpcRender: sheet resolution, sprite-cell lookup and the
 * top/bottom half draws. Kept distinct from NpcRender because the player's sheet
 * depends on the active movement mode (bicycle > run > walk), and because its row
 * lookup starts from a logical order ({DOWN=0,UP=1,LEFT=2,RIGHT=3}) that is
 * permuted into the shared physical layout - the rows that come out are the ones
 * @ref NpcRender::SpriteCoords hard-codes. Reads the TextureStore from globals
 * via PlayerSystem.
 */
namespace PlayerRender
{
/**
 * @brief Sprite-sheet cell origin (pixels) for a frame + facing (player row map).
 *
 * Column is `frame % CharacterConstants::WALK_FRAME_COUNT`; row comes from the
 * facing. The logical row order is {DOWN=0, UP=1, LEFT=2, RIGHT=3}.
 *
 * Takes no AnimationType: the row depends only on facing, and the sheet for the
 * active mode is selected separately. Matches NpcRender::SpriteCoords.
 *
 * @par The requiresYFlip row map is a permutation, not an inversion
 * When @p requiresYFlip is true the logical row is remapped through `{2, 3, 1, 0}`
 * - not the `3 - row` a bottom-up flip alone would give. DOWN and UP are swapped
 * relative to that, because the artwork's physical row order is not the logical
 * one. The output is exactly the sheet's GL row order:
 *
 *     logical  DOWN(0) UP(1) LEFT(2) RIGHT(3)
 *     GL row      2      3      1       0
 *
 * which is the same row assignment @ref NpcRender::SpriteCoords hard-codes
 * (RIGHT=0, LEFT=1, DOWN=2, UP=3). Both sheet families share one layout; the
 * player just reaches it through the logical order, the NPC bakes it in.
 *
 * @param frame         Animation frame; wrapped to the walk frame count.
 * @param dir           Facing direction.
 * @param requiresYFlip Apply the GL row permutation. Both shipping backends
 *                      return true from IRenderer::RequiresYFlip, so the default
 *                      `false` (raw logical rows) is effectively untaken - it
 *                      would sample a sheet laid out top-down, which none is.
 * @return Cell origin in pixels; the Y component counts GL rows from the bottom
 *         of the texture when @p requiresYFlip is set.
 */
glm::vec2 SpriteCoords(int frame, CharacterDirection dir, bool requiresYFlip = false);

/**
 * @brief Select the draw sheet for the active mode (bicycle > run > walk) and,
 * when atlas-bound, fold the mode's atlas offset into @p spriteCoords.
 *
 * @warning The returned reference is a non-owning borrow, valid for the current
 * draw only: an atlas re-pack or a map reload replaces the target's contents.
 *
 * @param world        Registry the TextureStore is resolved from, via globals.
 * @param modes        Movement modes; select both the sheet and the atlas offset.
 * @param sprite       Player sheet handles and atlas binding.
 * @param spriteCoords In/out cell origin, advanced by the mode's atlas offset on
 *                     the atlas path only.
 * @return The shared tile atlas when @c sprite.atlas is bound, otherwise the
 *         mode's sheet from the TextureStore in globals, or a shared empty
 *         texture when no store is published - the player then draws nothing.
 */
const Texture& ResolveRenderSheet(const ecs::registry& world,
                                  const PlayerModes& modes,
                                  const PlayerSprite& sprite,
                                  glm::vec2& spriteCoords);

/**
 * @brief Draw the top or bottom half of the player sprite for the Y-sort pass.
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
 * @param modes     Movement modes (bicycle > run > walk sheet selection).
 * @param sprite    Player sprite sheets and atlas offsets.
 */
void DrawHalf(const ecs::registry& world,
              IRenderer& renderer,
              glm::vec2 cameraPos,
              bool topHalf,
              const Transform& xf,
              const Elevation& elev,
              const Facing& facing,
              const AnimationState& anim,
              const PlayerModes& modes,
              const PlayerSprite& sprite);

/**
 * @brief Draw the player as a world-space billboard.
 *
 * The 3D counterpart to @ref DrawHalf, and a single call rather than two: the
 * depth buffer makes the top/bottom split unnecessary (see
 * @ref CharacterRender::DrawBillboard).
 *
 * @param world       Registry the sprite sheet / atlas is resolved from.
 * @param renderer    Backend the quad is submitted to.
 * @param orientation Damped billboard orientation for the Character role.
 * @param xf          Transform supplying the feet position.
 * @param elev        Elevation; only the cosmetic @c offset is used as height,
 *                    never the plane index (see the note in the definition).
 * @param facing      Facing direction, selecting the sheet row.
 * @param anim        Animation state, selecting the walk frame.
 * @param modes       Movement modes (bicycle > run > walk sheet selection).
 * @param sprite      Player sprite sheets and atlas offsets.
 */
void Draw3D(const ecs::registry& world,
            IRenderer& renderer,
            const billboard::Orientation& orientation,
            const Transform& xf,
            const Elevation& elev,
            const Facing& facing,
            const AnimationState& anim,
            const PlayerModes& modes,
            const PlayerSprite& sprite);
}  // namespace PlayerRender
