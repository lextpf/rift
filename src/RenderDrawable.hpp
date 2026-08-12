#pragma once

#include "SupportSurface.hpp"
#include "Tilemap.hpp"

#include <ecs.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

class IRenderer;

/**
 * @enum DrawableClass
 * @brief Tags a @ref Drawable as a complete entity or a world tile.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * This is the runtime discriminant the depth draw loop switches on to pick the
 * draw path. It is deliberately independent of the queue's authored render phase.
 * Neither branch touches projection state.
 *
 * @see Drawable, DrawableDepthLess
 */
enum class DrawableClass : std::uint8_t
{
    Entity = 0,  ///< A complete character; drawn through @ref Drawable::drawEntity.
    Tile = 1,    ///< A world tile; drawn through Tilemap::RenderSingleTile.
};

/**
 * @enum DrawablePhase
 * @brief Original layer role used by the baseline painter order.
 * @ingroup Rendering
 *
 * Inferred elevated artwork retains its authored background/foreground side of
 * actors. Explicit Y-sort tiles remain peers with actors instead of being
 * flattened into an elevation-wide band.
 */
enum class DrawablePhase : std::uint8_t
{
    Background = 0,  ///< Drawn before every actor.
    YSorted = 1,     ///< Depth-sorted as a peer of actors.
    Foreground = 2,  ///< Drawn after every actor.
};

/**
 * @struct Drawable
 * @brief One atomic character or world tile in the support-aware depth pass.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * A Drawable is a type-erased entry in one render list. @ref phase preserves the
 * tile's original background/Y-sort/foreground role, while @ref sortY gives
 * ordinary authored painter order among compatible peers.
 *
 * @code
 *   Background -> YSorted -> Foreground
 *                     depth = sortY
 * @endcode
 *
 * @ref SortDrawables then adds a small local partial order for one connected
 * elevation footprint. A ground actor inside it precedes all affiliated artwork
 * (underpass); its background surface precedes an elevation-supported actor
 * (deck); explicit Y-sort artwork retains its normal role everywhere and gains
 * only the necessary elevated-actor relationship inside the footprint. An actor
 * beside the ramp has no region relationship, so authored Y-sort remains
 * untouched.
 *
 * @par Equal-depth tie order
 * Within the depth epsilon (@ref YSORT_DEPTH_EPSILON) entries stack by @ref tieBias.
 * In the normal branch higher bias draws first (further back):
 * @code
 *   TIE_TILE   (4)   <- back
 *   TIE_NPC    (3)
 *   TIE_PLAYER (1)   <- front
 * @endcode
 * So at one depth: terrain behind, then NPCs, then the player on top.
 *
 * @par ySortMinus occlusion
 * A tile flagged @c ySortMinus (e.g. a tall overhang) must occlude an entity
 * whose feet sit just below its base. Branch A of @ref DrawableDepthLess adds
 * @ref YSORT_MINUS_OFFSET (half a tile) to the tile's compare point and flips the
 * tie-break to lower-first, so the tile wins:
 * @verbatim
 *   normal tile vs entity          ysortMinus tile vs entity
 *
 *   tile base   @ 96               tile base   @ 96  (+8 offset)
 *   entity feet @ 100              entity feet @ 100
 *   96  < 100 -> tile behind       104 > 100 -> tile in front
 *                                  (feet up to 8px below the base
 *                                   still pass behind the tile)
 * @endverbatim
 *
 * Each character contributes exactly one queue entry. Its thunk may issue
 * multiple sprite submissions, but they are consecutive and therefore atomic
 * with respect to tiles. This also covers hair, hats, equipment, and future
 * sprites of any height without teaching the sorter their bounds.
 *
 * @see DrawableClass, DrawablePhase, DrawableDepthLess, SortDrawables
 */
struct Drawable
{
    float sortY = 0.0f;                         ///< Authored depth key; smaller sorts further back.
    float supportHeight = 0.0f;                 ///< Surface metadata; never a global Y-sort offset.
    DrawableClass cls = DrawableClass::Entity;  ///< Entity vs tile; draw-loop discriminant.
    DrawablePhase phase = DrawablePhase::YSorted;  ///< Authored baseline render role.
    int surfaceRegionId = -1;  ///< Local elevation footprint shared by tiles/actors.
    SupportSurface supportSurface{SupportSurface::Ground};  ///< Actor topology; ignored for tiles.
    bool isYSortMinus = false;             ///< Tile occlusion flag; false for entities.
    std::uint8_t tieBias = 0;              ///< Equal-depth order (see DrawableDepthLess).
    const ecs::registry* world = nullptr;  ///< Registry for the thunk; null for tiles.
    ecs::entity handle{};                  ///< The player/NPC entity (none for tiles).
    /**
     * @brief Type-erased atomic character draw thunk.
     *
     * Null for tiles. Installed by @ref AddNpcDrawable / @ref AddPlayerDrawable,
     * which store @ref world + @ref handle and re-resolve the entity's render
     * components at draw time (so no @c Component& is ever cached in the Drawable),
     * then draw both sprite halves consecutively. No other queue item can be
     * inserted between the character's feet, body, hair, equipment, or any
     * future extension of its sprite.
     */
    void (*drawEntity)(const Drawable&, IRenderer&, glm::vec2) = nullptr;
    Tilemap::DepthSortedTile tile{};  ///< Tile payload; valid when @ref cls is Tile.
};

/**
 * @name Tie-break biases
 * @brief Equal-depth draw order. At one depth a tile draws first (behind), then
 * NPC, then player. In the normal branch of @ref DrawableDepthLess higher bias is
 * drawn first; the @c ySortMinus branch inverts this (see there).
 * @{
 */
inline constexpr std::uint8_t TIE_PLAYER = 1;
inline constexpr std::uint8_t TIE_NPC = 3;
inline constexpr std::uint8_t TIE_TILE = 4;
/// @}

/// @brief Preserve the tile's authored fixed-pass or explicit Y-sort role.
inline DrawablePhase TileDrawablePhase(const Tilemap::DepthSortedTile& tile)
{
    // ySortPlus/ySortMinus is an explicit authored rule, regardless of the
    // tile's fixed layer or elevation ownership. Surface-local constraints may
    // override it only for an actor actually under/on the same structure.
    if (tile.authoredYSort)
    {
        return DrawablePhase::YSorted;
    }
    return tile.isBackground ? DrawablePhase::Background : DrawablePhase::Foreground;
}

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
 * @brief Depth ordering for the Y-sorted pass (draws back-to-front).
 *
 * Returns true when @p a should be drawn before @p b (earlier = further back).
 * @ref Drawable::phase is the primary baseline key. Inside one phase, smaller
 * @ref Drawable::sortY draws first. Plane height is deliberately not folded
 * into this global key: doing that lets elevated artwork override authored
 * Y-sort beside a ramp. Two epsilon bands stop sub-pixel jitter from reordering
 * the stack, and one branch gives @c ySortMinus tiles their authored overhang
 * occlusion. @ref SortDrawables applies local surface relationships after this
 * baseline sort.
 *
 * @par Branch A: ySortMinus tile vs entity
 * Taken only when exactly one side is a @c ySortMinus tile and the other an
 * entity. That tile's compare point is pushed by +@ref YSORT_MINUS_OFFSET (half a
 * tile, toward the front). If the adjusted depths differ by more than
 * @ref YSORT_MINUS_EPSILON they order by adjusted authored depth; otherwise the
 * tie-break is **tieBias lower-first**, drawing the tile in front of the entity
 * so the entity passes behind the overhang.
 *
 * @par Branch B: everything else
 * If depths differ by more than @ref YSORT_DEPTH_EPSILON they order by authored
 * depth; otherwise the tie-break is **tieBias higher-first**, giving the fixed
 * equal-depth stack tile -> NPC -> player (front).
 *
 * The two branches deliberately INVERT the tie-break direction - that flip is the
 * whole point of @c ySortMinus: a tile that would lose a normal depth-tie instead
 * wins it and occludes the entity.
 *
 * @par Worked example
 * @code
 *   entity feet @ 100,  tile base @ 96
 *     normal tile:      96       < 100  ->  tile first   (behind entity)
 *     ySortMinus tile:  96+8=104 > 100  ->  entity first (tile in front)
 * @endcode
 *
 * @warning This is **not** a strict weak ordering. The epsilon bands trade
 * transitivity for depth stability: three entries of equal @ref Drawable::tieBias
 * at sortY 100.0, 100.8 and 101.6 make the first two equivalent and the middle two
 * equivalent, yet order the outer pair. Use it only as the predicate of
 * @c std::stable_sort, where insertion order breaks the remaining ties. Never pass
 * it to @c std::sort, @c std::set or any algorithm that assumes a strict weak
 * ordering - MSVC's debug build asserts "invalid comparator" on such input.
 *
 * @param a First drawable to compare.
 * @param b Second drawable to compare.
 * @return True if @p a should be drawn before @p b.
 * @see Drawable::tieBias, YSORT_MINUS_OFFSET, YSORT_DEPTH_EPSILON
 */
inline bool DrawableYSortLess(const Drawable& a, const Drawable& b)
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

/// @brief Baseline order: authored phase first, then ordinary Y-sort rules.
inline bool DrawableDepthLess(const Drawable& a, const Drawable& b)
{
    if (a.phase != b.phase)
    {
        return a.phase < b.phase;
    }
    return DrawableYSortLess(a, b);
}

/**
 * @brief Append one atomic NPC @ref Drawable (ECS path).
 *
 * Stores the registry + entity handle only; the draw thunk re-resolves the NPC's
 * render components (Transform / Elevation / Facing / AnimationState / NpcSprite)
 * at draw time, then submits both halves consecutively. No component reference is
 * cached in the Drawable.
 *
 * @pre @p world outlives the render list, and @p npc stays alive with all listed
 * components attached until the list is drawn or cleared. The draw loop resolves
 * the stored registry pointer and handle without an @c alive() check, so a despawn
 * or a component removal between @ref SortDrawables and the draw loop is a dangling
 * access.
 *
 * @param list          Render list to append the atomic item to.
 * @param world         Registry the draw thunk re-resolves components from.
 * @param npc           NPC entity to draw.
 * @param feetPos       Feet anchor in world pixels.
 * @param supportSurface Committed logical support used by local constraints.
 * @param supportHeight Committed physical elevation retained as support metadata.
 * @param surfaceRegionId Elevation footprint under the NPC's feet, or -1.
 * @param tieBias       Equal-depth NPC bias.
 */
void AddNpcDrawable(std::vector<Drawable>& list,
                    const ecs::registry& world,
                    ecs::entity npc,
                    glm::vec2 feetPos,
                    SupportSurface supportSurface,
                    float supportHeight,
                    int surfaceRegionId,
                    std::uint8_t tieBias);

/**
 * @brief Append one atomic player @ref Drawable (ECS path).
 *
 * The player analog of @ref AddNpcDrawable - the thunk re-resolves the player's
 * render components (Transform / Elevation / Facing / AnimationState /
 * PlayerModes / PlayerSprite) and draws both sprite halves without returning to
 * the sorted queue between them.
 *
 * @pre The same lifetime rule as @ref AddNpcDrawable: @p world outlives the render
 * list, and @p player stays alive with all listed components attached until the
 * list is drawn or cleared.
 *
 * @param list          Render list to append the atomic item to.
 * @param world         Registry the draw thunk re-resolves components from.
 * @param player        Player entity to draw.
 * @param feetPos       Feet anchor in world pixels.
 * @param supportSurface Committed logical support used by local constraints.
 * @param supportHeight Committed physical elevation retained as support metadata.
 * @param surfaceRegionId Elevation footprint under the player's feet, or -1.
 * @param tieBias       Equal-depth player bias.
 */
void AddPlayerDrawable(std::vector<Drawable>& list,
                       const ecs::registry& world,
                       ecs::entity player,
                       glm::vec2 feetPos,
                       SupportSurface supportSurface,
                       float supportHeight,
                       int surfaceRegionId,
                       std::uint8_t tieBias);

/**
 * @brief Baseline-sort drawables, then apply structure-local plane constraints.
 *
 * A ground actor inside an elevation footprint renders before every tile
 * affiliated with that footprint (underpass). Background surface artwork
 * renders before an elevation-supported actor in the same footprint (deck).
 * Actors outside the footprint add no constraint, so authored Y-sort and layer
 * roles remain authoritative beside a ramp.
 *
 * Every edge below is scoped to one @ref Drawable::surfaceRegionId. An entry whose
 * region id is negative takes part in the baseline sort only.
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 * GA["Ground actor"]:::node
 * EA["Elevated actor"]:::node
 * RT["Region tile"]:::node
 * BT["Background surface tile"]:::node
 * YT["Authored Y-sort tile"]:::node
 * OUT["Actor with surfaceRegionId &lt; 0
 * (no edges)"]:::loose
 * classDef node fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 * classDef loose fill:#3f3f46,stroke:#71717a,color:#e2e8f0
 * GA -->|draws before: underpass| RT
 * BT -->|draws before: deck| EA
 * YT <-->|direction from DrawableYSortLess| EA
 * </pre>
 * @endhtmlonly
 *
 * @note Reorders @p list in place, so any index held across the call is
 * invalidated. A min-heap Kahn pass keeps baseline index order for every
 * unconstrained pair. A cycle leaves the baseline order untouched and reports
 * nothing.
 *
 * @warning Edges are built as actors x tiles per region, so the count is the
 * product of one footprint's actors and its tiles. The pass also allocates several
 * vectors, a hash map and a priority queue per call, and it runs every frame.
 *
 * @param list Render list, reordered in place.
 */
void SortDrawables(std::vector<Drawable>& list);
