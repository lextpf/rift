#pragma once

#include "Tilemap.hpp"

#include <ecs.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

class IRenderer;

/**
 * @enum DrawableClass
 * @brief Whether a Drawable is an entity half or a world tile.
 * @ingroup Rendering
 *
 * Replaces the old RenderItem enum-range test (`type <= NPC_BOTTOM`) for
 * distinguishing entities from tiles in the sort and the perspective-suspend
 * decision. Tiles are NOT entities - they stay value data merged with entities
 * by depth in the Y-sorted pass.
 */
enum class DrawableClass : std::uint8_t
{
    Entity = 0,  ///< A character half (player/NPC); perspective is suspended while drawing.
    Tile = 1,    ///< A Y-sorted tile; drawn with perspective.
};

/**
 * @struct Drawable
 * @brief Type-agnostic entry in the Y-sorted render pass.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Replaces the enum-keyed `RenderItem`: instead of a fixed
 * {PLAYER_TOP,...,TILE} enum and a per-type dispatch switch, an entry is either
 * an entity half (@ref entity non-null, drawn via @ref drawHalf) or a tile
 * (@ref entity null, drawn via Tilemap::RenderSingleTile). This lets any entity -
 * and, later, an ECS sprite query - feed the pass without naming concrete types.
 */
struct Drawable
{
    float sortY = 0.0f;  ///< Depth key. NPC top is pre-offset up; player top is unoffset.
    DrawableClass cls = DrawableClass::Entity;  ///< Entity vs tile.
    bool isYSortMinus = false;  ///< Tile carries ySortMinus (anchored at top); false for entities.
    std::uint8_t tieBias = 0;   ///< Equal-depth tie-break order (lower value drawn later).
    bool topHalf = false;       ///< Entity half to draw (ignored for tiles).
    const ecs::registry* world = nullptr;  ///< Entity path: registry to resolve components from.
    ecs::entity handle{};  ///< Entity path: the player/NPC entity (no_entity for tiles).
    /// @brief Type-erased half-draw thunk: drawHalf(thisDrawable, renderer, cameraPos, topHalf).
    /// Null for tiles. Set by @ref AddNpcDrawables / @ref AddPlayerDrawables, which store
    /// @ref world + @ref handle and re-resolve the entity's render components at draw time
    /// (replacing the former IRenderableHalf vtable - entities need no base class).
    void (*drawHalf)(const Drawable&, IRenderer&, glm::vec2, bool) = nullptr;
    Tilemap::YSortPlusTile tile{};  ///< Tile payload; valid when @ref cls is Tile.
};

/// @name Tie-break biases
/// @brief Equal-depth draw order, preserving the original RenderItem enum's
/// integer ordering: at identical depth a tile draws first (behind), then NPC,
/// then player. Higher bias is drawn first (see DrawableDepthLess).
/// @{
inline constexpr std::uint8_t TIE_PLAYER_TOP = 0;
inline constexpr std::uint8_t TIE_PLAYER_BOTTOM = 1;
inline constexpr std::uint8_t TIE_NPC_TOP = 2;
inline constexpr std::uint8_t TIE_NPC_BOTTOM = 3;
inline constexpr std::uint8_t TIE_TILE = 4;
/// @}

/// @name Y-sort tuning constants
/// @brief Calibrated for the project's 16px tiles / ~16px hitboxes.
/// @{
inline constexpr float YSORT_MINUS_OFFSET = 8.0f;   ///< Half-tile offset for ySortMinus tiles.
inline constexpr float YSORT_MINUS_EPSILON = 0.1f;  ///< Tie band for the ySortMinus/entity compare.
inline constexpr float YSORT_DEPTH_EPSILON = 1.0f;  ///< ~1px sort-stability band on depth.
/// @}

/**
 * @brief Strict-weak depth ordering for the Y-sorted pass.
 *
 * Lower Y draws first (objects lower on screen sit in front). A ySortMinus tile
 * (anchored at its top edge) gets a half-tile offset so it compares fairly
 * against entity feet. Equal-depth ties resolve by @ref Drawable::tieBias.
 *
 * This is a behavior-identical port of the original RenderItem enum comparator;
 * @ref Drawable::tieBias carries the old enum's integer value, and
 * @ref Drawable::isYSortMinus / @ref DrawableClass replace the old type tests.
 *
 * @return True if @p a should be drawn before @p b.
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
 * @brief Append an NPC entity's bottom + top half drawables (ECS path).
 *
 * Stores the registry + entity handle; the draw thunk re-resolves the NPC's
 * render components (Transform/Elevation/Facing/AnimationState/NpcSprite) at draw
 * time (never caching a @c Component& in the Drawable) and calls the free
 * @c NpcRender::DrawHalf. Safe as long as no spawn/despawn touches those pools
 * between render-list build and the draw loop (the Y-sort pass does neither).
 * Defined out-of-line in RenderDrawable.cpp to keep this header free of the
 * component + render includes.
 */
void AddNpcDrawables(std::vector<Drawable>& list,
                     const ecs::registry& world,
                     ecs::entity npc,
                     glm::vec2 feetPos,
                     float topOffset,
                     std::uint8_t bottomTieBias,
                     std::uint8_t topTieBias);

/**
 * @brief Append the player entity's bottom + top half drawables (ECS path).
 *
 * The player analog of @ref AddNpcDrawables: the thunk re-resolves the player's
 * render components (Transform/Elevation/Facing/AnimationState/PlayerModes/
 * PlayerSprite) and calls the free @c PlayerRender::DrawHalf. @p topOffset is 0
 * for the player (both halves share a depth).
 */
void AddPlayerDrawables(std::vector<Drawable>& list,
                        const ecs::registry& world,
                        ecs::entity player,
                        glm::vec2 feetPos,
                        float topOffset,
                        std::uint8_t bottomTieBias,
                        std::uint8_t topTieBias);
