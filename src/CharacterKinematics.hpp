#pragma once

#include "AnimationState.hpp"
#include "Elevation.hpp"
#include "ElevationAxis.hpp"
#include "SupportSurface.hpp"

#include <glm/glm.hpp>

class Tilemap;

/**
 * @brief Stateless elevation + walk-animation logic over character components.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The elevation and walk-animation systems: free functions operating on a plain
 * @ref Elevation or @ref AnimationState by reference, called directly by the
 * player and NPC update paths (@ref PlayerSystem / @ref NpcAiSystem).
 *
 * The support/plane rule itself is not here - it lives in @ref SurfaceSystem. What this
 * header owns is the entity-side seam onto it: read the current support (@ref GetSupport),
 * ask SurfaceSystem what a move would reach (@ref ResolveSupport), and commit an accepted
 * answer plus its visual transition (@ref CommitSupport).
 *
 * @see SurfaceSystem, Elevation, SupportState
 */
namespace CharacterKinematics
{
/**
 * @brief Begin a smooth transition of the elevation offset toward @p offset.
 *
 * No-op if @p offset already equals the current target. Starts a smoothstep
 * interpolation from the current offset that @ref UpdateElevation advances.
 *
 * @param elev   Elevation component whose visual transition is restarted.
 * @param offset Target visual Y offset in pixels (normally the new support height).
 */
void SetElevationTarget(Elevation& elev, float offset);

/**
 * @brief Single-tile plane-update rule that no live movement path uses.
 *
 * @warning The live rule is @ref SurfaceSystem::ResolveMove followed by @ref CommitSupport. This
 * function has no production callers, only ElevationZTests, and its axis test is looser than the
 * live one: it accepts a diagonal step onto a ramp, which SurfaceSystem rejects. Do not use it as
 * a shortcut.
 *
 * What it does, given the destination tile's authored axis:
 * - For @ref ElevationAxis::None - commits @p destTileElev unconditionally, bypassing the step
 *   gate entirely (surface becomes Ground when @p destTileElev is 0, Elevation otherwise).
 * - X / Y: engages when @p moveDx (resp. @p moveDy) is nonzero, with no requirement that
 *   the other axis be zero, then commits only if the delta from the current plane is within
 *   @ref CharacterConstants::MAX_STEP_HEIGHT.
 * - Anything else is a no-op, so a perpendicular crossing passes under/over unchanged.
 *
 * @param elev         Elevation component; @c plane / @c surface are written on engagement.
 * @param destTileElev Authored elevation of the destination tile, in pixels.
 * @param tileAxis     Destination tile's engagement axis (Tilemap::GetElevationAxisAt).
 * @param moveDx       Horizontal step sign (-1, 0, +1).
 * @param moveDy       Vertical step sign (-1, 0, +1); +1 is south/down.
 */
void UpdatePlane(Elevation& elev, int destTileElev, ElevationAxis tileAxis, int moveDx, int moveDy);

/**
 * @brief Return the currently committed support state.
 *
 * @param elev Elevation component to read.
 * @return @c surface + @c plane as one @ref SupportState, the form collision compares on.
 */
SupportState GetSupport(const Elevation& elev);

/**
 * @brief Predict the support reached by a movement probe without mutating @p elev.
 *
 * @param elev    Elevation component supplying the current support (read-only).
 * @param before  Feet position before the candidate move, world pixels.
 * @param after   Candidate feet position, world pixels.
 * @param tilemap World tilemap supplying elevations and elevation axes.
 * @return The resolved transition; a @c connected == false result must block the move.
 */
SurfaceTransition ResolveSupport(const Elevation& elev,
                                 glm::vec2 before,
                                 glm::vec2 after,
                                 const Tilemap& tilemap);

/**
 * @brief Atomically commit a previously validated support transition.
 *
 * The only writer of @c plane + @c surface as a pair; it also starts the matching visual
 * transition. Call it after the position move is accepted, never on a rejected probe.
 *
 * @param elev    Elevation component to update.
 * @param support Support taken from an accepted @ref SurfaceTransition.
 */
void CommitSupport(Elevation& elev, SupportState support);

/**
 * @brief Resolve and commit support from a completed external/no-clip move.
 *
 * Normal collision-aware movement uses @ref ResolveSupport before moving and
 * @ref CommitSupport afterward. This helper performs both for callers whose move has
 * already happened: no-clip player movement and the dialogue snap that teleports both
 * participants. A disconnected path is silently ignored, leaving the current support in
 * place, so the entity keeps a coherent surface rather than landing on an unreachable one.
 *
 * @param elev    Elevation component to update in place.
 * @param before  Feet position before the already-applied move.
 * @param after   Feet position after it.
 * @param tilemap World tilemap supplying elevations and elevation axes.
 */
void DerivePlane(Elevation& elev, glm::vec2 before, glm::vec2 after, const Tilemap& tilemap);

/**
 * @brief Advance the smooth elevation transition by one frame (smoothstep).
 *
 * No-op once @c progress reaches 1. The transition is a fixed 0.15 s regardless of height
 * delta, and only touches the visual @c offset - never @c plane or @c surface.
 *
 * @param elev      Elevation component whose visual offset is advanced.
 * @param deltaTime Frame time in seconds.
 */
void UpdateElevation(Elevation& elev, float deltaTime);

/**
 * @brief Advance the walk cycle to the next frame in the [1,0,2,0] sequence.
 *
 * @param anim Animation state; the sequence index wraps and drives @c currentFrame.
 */
void AdvanceWalkAnimation(AnimationState& anim);

/**
 * @brief Snap animation back to the idle pose (frame 0, index 0).
 *
 * @param anim Animation state; also clears the timing accumulator.
 */
void ResetAnimation(AnimationState& anim);
}  // namespace CharacterKinematics
