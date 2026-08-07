#pragma once

#include "Billboard.hpp"
#include "TileStance.hpp"

/**
 * @brief Turns a tile's authored @ref TileStance into render behavior.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * The 3D world needs to know, per tile, whether its art is *floor* (grass,
 * paths, water) or *object* (trees, fences, signs, buildings), and if it is an
 * object, whether it turns toward the camera or holds a grid-locked orientation.
 * Both answers come from one authored value; nothing here infers.
 *
 * @par What is deliberately not consulted
 * The tile's layer, and the tile's neighbors. Both rules were tried and
 * rejected. Standing a tile up for sitting on a foreground layer was wrong
 * because foreground layers carry flat decals too. Deriving surface-vs-pole from
 * adjacency was wrong because two unrelated bushes placed side by side both
 * stopped turning. Every predicate below takes exactly one stance, so neither
 * input can be wired back in without deliberately changing a signature.
 *
 * The load-time migration in @c Tilemap::LoadMapFromJSON does read the layer
 * index and a cell's neighbors, exactly once, to seed the stance of a legacy
 * map. That is a one-off translation of old data, not a rule.
 *
 * @see TileStance, Tilemap::RenderWorld3D
 */
namespace tileRole
{

/// @brief Whether a tile's artwork should be built as an upright billboard.
inline constexpr bool IsUpright(TileStance stance)
{
    return stance != TileStance::Flat;
}

/**
 * @brief Whether a tile is one slice of a multi-tile-tall body.
 *
 * A vertical run of upright tiles in the map is ambiguous, and the two readings
 * look nothing alike once the world is 3D:
 *
 * @verbatim
 *   Structure (stacks)             Wall / Prop going north (does not)
 *   rows y..y+2 are one image      rows y..y+2 are three props
 *
 *      [roof ]                        [#]        <- own row, 1 tall
 *      [wall ]   all on row y+2       [#]        <- own row, 1 tall
 *      [door ]                        [#]        <- own row, 1 tall
 *     ========= ground               ============= ground, receding north
 * @endverbatim
 *
 * Treating every vertical run as a building turns a fence running north-south
 * into a tower - it climbs into the air instead of receding along the ground.
 * Only @ref TileStance::Structure gets a base row and a lift.
 */
inline constexpr bool StacksVertically(TileStance stance)
{
    return stance == TileStance::Structure;
}

/**
 * @brief Whether a tile is a surface rather than a pole.
 *
 * Billboarding only holds for artwork with no real extent across its pivot. A
 * lantern, a stone, a tree trunk is effectively a pole: turning it to face the
 * camera spins it in place and it never leaves the cell it was authored on. A
 * fence panel or a facade is a surface, and turning it drags its far end
 * through world space:
 *
 * @verbatim
 *   pole (Prop)                 surface (Wall / wide Structure)
 *   pivot == the tile           pivot at the center
 *
 *        [#]                    [#][#][#][#]      authored
 *         ^  spins in place          |
 *         |  anchor held             v
 *                              [#][#]                 far ends swing
 *                                    [#][#]           off their anchors
 * @endverbatim
 *
 * A surface therefore keeps its yaw fixed to the grid and only leans with the
 * camera's pitch, so its base stays exactly where it was authored at every
 * camera angle - which is what a wall has to do.
 *
 * @par Consequence
 * A surface does not turn to meet the camera, so orbiting to ninety degrees from
 * it shows it edge-on, and past that its back. That is the honest behavior for a
 * single-sided surface - the alternative is the artwork wandering off its
 * footprint. Tile-map editors of this kind behave the same way: walls are static
 * grid-aligned geometry and only the camera moves.
 */
inline constexpr bool IsSurface(TileStance stance)
{
    return stance == TileStance::Wall || stance == TileStance::Structure;
}

/**
 * @brief Whether a tile holds a grid-locked orientation instead of turning.
 *
 * The single source of truth for pole-vs-surface, so the damping and the
 * renderer's choice between its two pre-resolved orientations cannot drift
 * apart. @ref TileStance::Structure is the one stance whose answer is still
 * measured rather than authored, and deliberately so: this is not inference
 * between separate props but the extent of one authored body, resolved from its
 * @c NoProjectionStructure bounds (or, for tiles left on auto flood-fill, from
 * that body's own contiguous run). A narrow tower - a totem, a tree painted as a
 * tall column - keeps turning, while a facade stays anchored.
 *
 * @param stance                 The tile's authored stance.
 * @param structureWidthInTiles  Width of the enclosing body along X, consulted
 *                               for @ref TileStance::Structure only. Ignoring it
 *                               elsewhere is what makes a @ref TileStance::Prop
 *                               keep turning beside a neighbor and a
 *                               @ref TileStance::Wall stay locked when it stands
 *                               alone.
 */
inline constexpr bool IsGridLocked(TileStance stance, int structureWidthInTiles)
{
    return stance == TileStance::Wall ||
           (stance == TileStance::Structure && structureWidthInTiles > 1);
}

/**
 * @brief Billboard damping profile for an upright tile.
 *
 * All upright tile artwork uses the @c Scenery role, which under-follows the
 * camera's yaw. That residual mismatch is what produces the "follows the camera
 * a bit" read rather than a row of fences pivoting in lockstep. Characters are
 * the only things that follow yaw completely, and they do not come through here.
 */
inline billboard::Damping UprightDamping()
{
    return billboard::DefaultDamping(billboard::Role::Scenery);
}

/**
 * @brief Damping for a tile of the given stance.
 *
 * Only the yaw is ever suppressed. The lean is what gives upright artwork its
 * apparent height and is identical for every stance, so re-marking one tile of a
 * run cannot shear it out of line with its neighbors.
 *
 * @param stance                 The tile's authored stance.
 * @param structureWidthInTiles  Width of the enclosing body, see @ref IsGridLocked.
 */
inline billboard::Damping DampingFor(TileStance stance, int structureWidthInTiles)
{
    billboard::Damping damping = UprightDamping();
    if (IsGridLocked(stance, structureWidthInTiles))
    {
        damping.yawFollow = 0.0f;
    }
    return damping;
}

/**
 * @brief Damping for an upright structure of a given width, in tiles.
 *
 * @param widthInTiles Structure width along X. Values below 1 are treated as 1.
 */
inline billboard::Damping DampingForWidth(int widthInTiles)
{
    return DampingFor(TileStance::Structure, widthInTiles);
}

}  // namespace tileRole
