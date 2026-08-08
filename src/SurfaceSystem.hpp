#pragma once

#include "SupportSurface.hpp"

#include <glm/glm.hpp>

class Tilemap;

/**
 * @brief Resolve movement through Rift's overlapping ground/elevation surfaces.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Ground is implicit beneath every authored elevated cell. The current support
 * therefore provides continuity: a ground character remains underneath a bridge,
 * while an elevation-supported character remains on its deck. Only a ramp edge
 * connects the two surfaces.
 *
 * This is the live plane rule for the whole engine. @c CharacterKinematics::UpdatePlane applies
 * a looser axis test and no production path calls it, so do not treat it as a second source of
 * truth.
 *
 * @par Support graph
 * Each crossed tile boundary is one edge lookup. A probe that stays inside its starting
 * tile is a no-op returning the current support unchanged. Ground never disconnects - it
 * exists under every cell - so only an elevation-supported character can be blocked here.
 *
 * @htmlonly
 * <pre class="mermaid">
 * stateDiagram-v2
 *     direction LR
 *
 *     state "Ground (height 0)" as Ground
 *     state "Elevation (height h)" as Elev
 *     state "connected = false" as Blocked
 *
 *     Ground --> Ground: any crossing; ground runs under everything
 *     Ground --> Elev: ramp low-end, axis-aligned, abs(h) at most MAX_STEP
 *     Elev --> Elev: adjacent elevation, abs(dh) at most MAX_STEP
 *     Elev --> Ground: ramp low-end, axis-aligned, abs(src) at most MAX_STEP
 *     Elev --> Blocked: deck edge with no ramp, or step too large
 * </pre>
 * @endhtmlonly
 *
 * MAX_STEP is @ref CharacterConstants::MAX_STEP_HEIGHT; @c h / @c src are the destination /
 * source cell's authored elevation and @c dh is @c h minus the current support height.
 * "Ramp low-end" means the other cell's elevation is 0: entry requires a bare-ground source
 * cell, exit requires a bare-ground destination cell.
 *
 * @par Axis engagement
 * "Move matches the tile's axis" is strict: an X-axis ramp engages only on pure east/west
 * steps (@c moveDx nonzero and @c moveDy zero), a Y-axis ramp only on pure north/south
 * steps. A diagonal step therefore never enters or leaves an elevation - it is treated as
 * crossing the connector, not walking along it. A tile with @ref ElevationAxis::None has no
 * connector at all, so it can never be entered from ground.
 *
 * @par Step gate
 * @ref CharacterConstants::MAX_STEP_HEIGHT is compared against three different quantities,
 * one per edge kind: ground -&gt; elevation uses the absolute destination height; elevation
 * -&gt; elevation uses the delta between destination and current support height; elevation
 * -&gt; ground uses the absolute source height.
 *
 * @see SupportState, SurfaceTransition, ElevationAxis, CollisionSystem::ProbeMovement
 */
namespace SurfaceSystem
{
/**
 * @brief Resolve the support state at @p to without mutating character state.
 *
 * Long probes are subdivided into steps of at most 45% of the smaller tile dimension, so
 * every crossed tile boundary is evaluated in order and a fast move cannot tunnel past a
 * ramp connector. Resolution stops at the first disconnected step.
 *
 * @param current Support the character stands on now (CharacterKinematics::GetSupport).
 * @param from    Feet position before the move, world pixels (+Y down).
 * @param to      Candidate feet position after the move, world pixels.
 * @param tilemap Supplies per-tile elevation and the derived per-tile elevation axis.
 * @return The support reached. A @c connected == false result is an unsupported step/drop:
 *         the caller must block the whole movement transaction and commit nothing.
 */
SurfaceTransition ResolveMove(SupportState current,
                              glm::vec2 from,
                              glm::vec2 to,
                              const Tilemap& tilemap);

/**
 * @brief Return whether a collision tile belongs to @p support.
 *
 * A cell stores one collision flag and one elevation value. Ground collision belongs to the
 * implicit ground. Elevated collision belongs to the exact authored support height, so a 6px ramp
 * does not snag on a nearby 10px railing before the movement transaction reaches that support.
 *
 * The height match is exact, with no tolerance band. @ref CharacterConstants::MAX_STEP_HEIGHT
 * plays no part here, because it gates surface transitions rather than blocking.
 *
 * @param tilemap Supplies the cell's authored elevation.
 * @param tileX   Tile column of the collision cell.
 * @param tileY   Tile row of the collision cell.
 * @param support Committed or candidate support of the character being tested.
 * @return True when a collision flag authored on that cell applies to a character on
 *         @p support. The flag itself is the caller's check (Tilemap::GetTileCollision).
 */
bool CollisionBelongsTo(const Tilemap& tilemap, int tileX, int tileY, SupportState support);
}  // namespace SurfaceSystem
