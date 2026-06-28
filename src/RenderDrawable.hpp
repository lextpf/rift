#pragma once

#include "Tilemap.hpp"

#include <ecs.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

class IRenderer;

/**
 * @enum DrawableClass
 * @brief Tags a @ref Drawable as an entity half or a world tile.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * This is the runtime discriminant the Y-sorted draw loop switches on to decide
 * how an entry is drawn and whether perspective is suspended - NOT the nullness
 * of @ref Drawable::world / @ref Drawable::handle (tiles merely happen to leave
 * those unset). It supersedes the old enum-range test (`type <= NPC_BOTTOM`) that
 * once split entities from tiles. Tiles are NOT entities: they stay value data
 * merged with entity halves by depth in the single Y-sorted pass.
 *
 * @see Drawable, DrawableDepthLess
 */
enum class DrawableClass : std::uint8_t
{
    Entity = 0,  ///< A character half (player/NPC); drawn with perspective suspended.
    Tile = 1,    ///< A Y-sorted world tile; drawn with perspective via RenderSingleTile.
};

/**
 * @struct Drawable
 * @brief One depth-sorted entry - an entity half or a world tile - in the Y-sort pass.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * A Drawable is a type-agnostic entry in the single Y-sorted render list. It is
 * either an entity half (@ref cls is @c Entity, drawn via the @ref drawHalf
 * thunk) or a world tile (@ref cls is @c Tile, drawn via
 * @c Tilemap::RenderSingleTile). Tiles and entity halves are peers merged by
 * depth, so any entity - and, later, an ECS sprite query - can feed the pass
 * without naming concrete types. This supersedes the old enum-keyed @c RenderItem
 * plus per-type dispatch switch.
 *
 * @par Y-Sort Model
 * @c sortY is a world-Y coordinate in pixels (equal to screen-Y up to the
 * constant per-frame camera offset), so one painter's-order pass draws
 * back-to-front by @c sortY:
 * @code
 *   world-Y (== screen-Y minus a constant camera offset)
 *
 *   small sortY  --- top of screen ------------------  drawn FIRST = BEHIND
 *      |
 *      |    [ tile   @  90 ]
 *      |    [ npc    @ 100 ]
 *      v    [ player @ 130 ]
 *
 *   large sortY  --- bottom of screen ---------------  drawn LAST  = IN FRONT
 * @endcode
 * Objects lower on screen (larger @c sortY) sit in front.
 *
 * @par One Unified List
 * Y-sorted tiles and entity halves are peers in one @c std::vector<Drawable>;
 * neither owns the other. Each entity contributes TWO halves so a tile can slot
 * between an entity's feet and head for correct occlusion:
 * @code
 *   [ y-sort tiles       ] --.
 *   [ npc: bottom + top  ] --+-->  vector<Drawable>  --stable_sort-->  draw loop
 *   [ player: bottom+top ] --'         (peers)      (DrawableDepthLess)
 * @endcode
 *
 * @par Entity Halves
 * @ref AddNpcDrawables / @ref AddPlayerDrawables split each character into a
 * bottom half at the feet and a top half above it:
 * @code
 *      +--------+   top half     sortY = feetY - topOffset
 *      |  head  |
 *      +--------+ - - - - - - a tall y-sort tile can sort BETWEEN the halves,
 *      |  feet  |             occluding only the top -> "walk behind" effect
 *      +--------+   bottom half  sortY = feetY
 *         (o)          <- feet anchor = Transform.position.y
 * @endcode
 * The NPC @c topOffset is @c HALF_HITBOX_HEIGHT (8px) so the head sorts slightly
 * behind the feet; the player passes @c topOffset @c = @c 0, so both halves share
 * one depth and never straddle a tile.
 *
 * @par Equal-Depth Tie Order
 * Within the depth band (@ref YSORT_DEPTH_EPSILON) entries stack by @ref tieBias.
 * In the normal branch HIGHER bias draws first (further back):
 * @code
 *   TIE_TILE          (4)   <- back  (drawn first)
 *   TIE_NPC_BOTTOM    (3)
 *   TIE_NPC_TOP       (2)
 *   TIE_PLAYER_BOTTOM (1)
 *   TIE_PLAYER_TOP    (0)   <- front (drawn last)
 * @endcode
 * So at one depth: terrain behind, then NPCs, then the player on top.
 *
 * @par ySortMinus Occlusion
 * A tile flagged @c ySortMinus (e.g. a tall overhang) must occlude an entity
 * whose feet sit just below its base. Branch A of @ref DrawableDepthLess adds
 * @ref YSORT_MINUS_OFFSET (half a tile) to the tile's compare point and FLIPS the
 * tie-break to lower-first, so the tile wins:
 * @verbatim
 *   normal tile vs entity          ysortMinus tile vs entity
 *
 *   tile base   @ 96               tile base   @ 96  (+8 offset)
 *   entity feet @ 100              entity feet @ 100
 *   96  < 100 -> tile BEHIND       104 > 100 -> tile IN FRONT
 *                                  (feet up to 8px below the base
 *                                   still pass behind the tile)
 * @endverbatim
 *
 * @par Perspective Batching
 * The draw loop suspends perspective for entity halves (upright sprites) and
 * enables it for tiles, flipping only at @ref DrawableClass boundaries so
 * contiguous runs stay in one sprite batch. @c SuspendPerspective is ref-counted;
 * the loop is entered and left with perspective suspended:
 * @code
 *   sorted list:  T  T  E  E  E  T  E     (T = tile, E = entity half)
 *   perspective:  on on -- -- -- on --    (-- = suspended)
 *                 flips only at T<->E boundaries; enter & leave suspended
 * @endcode
 *
 * @see DrawableClass, DrawableDepthLess, AddNpcDrawables, AddPlayerDrawables
 */
struct Drawable
{
    float sortY = 0.0f;                         ///< Depth key; smaller sorts further back.
    DrawableClass cls = DrawableClass::Entity;  ///< Entity vs tile; draw-loop discriminant.
    bool isYSortMinus = false;                  ///< Tile occlusion flag; false for entities.
    std::uint8_t tieBias = 0;                   ///< Equal-depth order (see DrawableDepthLess).
    bool topHalf = false;                       ///< Entity half: true=head, false=feet.
    const ecs::registry* world = nullptr;       ///< Registry for the thunk; null for tiles.
    ecs::entity handle{};                       ///< The player/NPC entity (none for tiles).
    /**
     * @brief Type-erased half-draw thunk: `drawHalf(*this, renderer, cameraPos, topHalf)`.
     *
     * Null for tiles. Installed by @ref AddNpcDrawables / @ref AddPlayerDrawables,
     * which store @ref world + @ref handle and re-resolve the entity's render
     * components at draw time (so no @c Component& is ever cached in the Drawable),
     * then dispatch to the free @c NpcRender::DrawHalf / @c PlayerRender::DrawHalf.
     */
    void (*drawHalf)(const Drawable&, IRenderer&, glm::vec2, bool) = nullptr;
    Tilemap::YSortPlusTile tile{};  ///< Tile payload; valid when @ref cls is Tile.
};

/**
 * @name Tie-break biases
 * @brief Equal-depth draw order (carrying the old RenderItem enum's integer
 * values). At one depth a tile draws first (behind), then NPC, then player. In
 * the normal branch of @ref DrawableDepthLess higher bias is drawn first; the
 * @c ySortMinus branch inverts this (see there).
 * @{
 */
inline constexpr std::uint8_t TIE_PLAYER_TOP = 0;
inline constexpr std::uint8_t TIE_PLAYER_BOTTOM = 1;
inline constexpr std::uint8_t TIE_NPC_TOP = 2;
inline constexpr std::uint8_t TIE_NPC_BOTTOM = 3;
inline constexpr std::uint8_t TIE_TILE = 4;
/// @}

/**
 * @name Y-sort tuning constants
 * @brief Calibrated for the project's 16px tiles / ~16px hitboxes.
 * @{
 */
inline constexpr float YSORT_MINUS_OFFSET = 8.0f;   ///< Half-tile front push for ySortMinus tiles.
inline constexpr float YSORT_MINUS_EPSILON = 0.1f;  ///< Near-tie band for ySortMinus vs entity.
inline constexpr float YSORT_DEPTH_EPSILON = 1.0f;  ///< ~1px depth-stability band before tieBias.
/// @}

/**
 * @brief Strict-weak depth ordering for the Y-sorted pass (draws back-to-front).
 *
 * Returns true when @p a should be drawn before @p b (earlier = further back).
 * Smaller @c sortY draws first, so objects lower on screen sit in front. Two
 * epsilon bands stop sub-pixel jitter from reordering the stack, and one branch
 * gives @c ySortMinus tiles their overhang occlusion.
 *
 * @par Branch A: ySortMinus tile vs entity
 * Taken only when exactly one side is a @c ySortMinus tile and the other an
 * entity. That tile's compare point is pushed by +@ref YSORT_MINUS_OFFSET (half a
 * tile, toward the front). If the adjusted depths differ by more than
 * @ref YSORT_MINUS_EPSILON they order by adjusted @c sortY; otherwise the
 * tie-break is **tieBias lower-first**, drawing the tile in front of the entity
 * so the entity passes behind the overhang.
 *
 * @par Branch B: everything else
 * If depths differ by more than @ref YSORT_DEPTH_EPSILON they order by raw
 * @c sortY; otherwise the tie-break is **tieBias higher-first**, giving the fixed
 * equal-depth stack tile -> NPC -> player (front).
 *
 * The two branches deliberately INVERT the tie-break direction - that flip is the
 * whole point of @c ySortMinus: a tile that would lose a normal depth-tie instead
 * wins it and occludes the entity.
 *
 * @par Worked Example
 * @code
 *   entity feet @ 100,  tile base @ 96
 *     normal tile:      96       < 100  ->  tile first   (BEHIND entity)
 *     ySortMinus tile:  96+8=104 > 100  ->  entity first (tile IN FRONT)
 * @endcode
 *
 * @param a First drawable to compare.
 * @param b Second drawable to compare.
 * @return True if @p a should be drawn before @p b.
 * @see Drawable::tieBias, YSORT_MINUS_OFFSET, YSORT_DEPTH_EPSILON
 */
inline bool DrawableDepthLess(const Drawable& a, const Drawable& b)
{
    const bool aIsEntity = (a.cls == DrawableClass::Entity);
    const bool bIsEntity = (b.cls == DrawableClass::Entity);

    if ((a.isYSortMinus && bIsEntity) || (b.isYSortMinus && aIsEntity))
    {
        const float aSortY = a.sortY + (a.isYSortMinus ? YSORT_MINUS_OFFSET : 0.0f);
        const float bSortY = b.sortY + (b.isYSortMinus ? YSORT_MINUS_OFFSET : 0.0f);
        if (std::abs(aSortY - bSortY) > YSORT_MINUS_EPSILON)
        {
            return aSortY < bSortY;
        }
        return a.tieBias < b.tieBias;
    }

    if (std::abs(a.sortY - b.sortY) > YSORT_DEPTH_EPSILON)
    {
        return a.sortY < b.sortY;
    }

    return a.tieBias > b.tieBias;
}

/**
 * @brief Append an NPC's bottom + top half @ref Drawable entries (ECS path).
 *
 * Stores the registry + entity handle only; the draw thunk re-resolves the NPC's
 * render components (Transform / Elevation / Facing / AnimationState / NpcSprite)
 * at draw time - never caching a @c Component& in the Drawable - and calls the
 * free @c NpcRender::DrawHalf. Safe as long as no spawn/despawn touches those
 * pools between render-list build and the draw loop (the Y-sort pass does
 * neither). Defined out-of-line in RenderDrawable.cpp to keep this header free of
 * the component + render includes.
 *
 * @param list          Render list to append the two halves to.
 * @param world         Registry the draw thunk re-resolves components from.
 * @param npc           NPC entity to draw.
 * @param feetPos       Feet anchor in world pixels; the bottom half's `sortY`.
 * @param topOffset     Upward depth offset for the top half (`sortY = feetY - topOffset`).
 * @param bottomTieBias Equal-depth bias for the bottom half (e.g. @ref TIE_NPC_BOTTOM).
 * @param topTieBias    Equal-depth bias for the top half (e.g. @ref TIE_NPC_TOP).
 */
void AddNpcDrawables(std::vector<Drawable>& list,
                     const ecs::registry& world,
                     ecs::entity npc,
                     glm::vec2 feetPos,
                     float topOffset,
                     std::uint8_t bottomTieBias,
                     std::uint8_t topTieBias);

/**
 * @brief Append the player's bottom + top half @ref Drawable entries (ECS path).
 *
 * The player analog of @ref AddNpcDrawables: the thunk re-resolves the player's
 * render components (Transform / Elevation / Facing / AnimationState /
 * PlayerModes / PlayerSprite) and calls the free @c PlayerRender::DrawHalf. Pass
 * @c topOffset @c = @c 0 so both halves share one depth (the player is never
 * split across a tile).
 *
 * @param list          Render list to append the two halves to.
 * @param world         Registry the draw thunk re-resolves components from.
 * @param player        Player entity to draw.
 * @param feetPos       Feet anchor (world px); both halves' `sortY` when topOffset is 0.
 * @param topOffset     Upward depth offset for the top half (0 for the player).
 * @param bottomTieBias Equal-depth bias for the bottom half (@ref TIE_PLAYER_BOTTOM).
 * @param topTieBias    Equal-depth bias for the top half (@ref TIE_PLAYER_TOP).
 */
void AddPlayerDrawables(std::vector<Drawable>& list,
                        const ecs::registry& world,
                        ecs::entity player,
                        glm::vec2 feetPos,
                        float topOffset,
                        std::uint8_t bottomTieBias,
                        std::uint8_t topTieBias);
