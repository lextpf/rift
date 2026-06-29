#pragma once

#include "AnimationState.hpp"
#include "Elevation.hpp"
#include "ElevationAxis.hpp"

#include <glm/glm.hpp>

class Tilemap;

/**
 * @brief Stateless elevation + walk-animation logic over character components.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The behavior half of the former GameCharacter base, extracted into free
 * functions that operate on a plain @ref Elevation or @ref AnimationState by
 * reference. These are the elevation/animation systems, called directly by the
 * player and NPC update paths (@ref PlayerSystem / @ref NpcAiSystem).
 */

namespace CharacterKinematics
{
/**
 * @brief Begin a smooth transition of the elevation offset toward @p offset.
 *
 * No-op if @p offset already equals the current target. Starts a smoothstep
 * interpolation from the current offset that @ref UpdateElevation advances.
 */
void SetElevationTarget(Elevation& elev, float offset);

/**
 * @brief Apply the axis-engagement plane-update rule for a destination tile.
 *
 * The plane tracks @p destTileElev only when the tile's elevation axis matches
 * the movement direction (or the tile is ground), and the delta is within
 * @ref CharacterConstants::MAX_STEP_HEIGHT. Perpendicular crossings leave the
 * plane unchanged so the entity passes under/over.
 */
void UpdatePlane(Elevation& elev, int destTileElev, ElevationAxis tileAxis, int moveDx, int moveDy);

/**
 * @brief Derive and apply the logical plane from a movement delta (player + NPC).
 *
 * Computes the dx/dy movement signs from @p before -> @p after (1px deadzone),
 * looks up the destination tile's elevation + engagement axis at @p after, and
 * advances @p elev via @ref UpdatePlane. One shared implementation for the
 * player and every NPC (previously copy-pasted in Game::Update).
 */
void DerivePlane(Elevation& elev, glm::vec2 before, glm::vec2 after, const Tilemap& tilemap);

/**
 * @brief Advance the smooth elevation transition by one frame (smoothstep).
 */
void UpdateElevation(Elevation& elev, float deltaTime);

/**
 * @brief Advance the walk cycle to the next frame in the [1,0,2,0] sequence.
 */
void AdvanceWalkAnimation(AnimationState& anim);

/**
 * @brief Snap animation back to the idle pose (frame 0, index 0).
 */
void ResetAnimation(AnimationState& anim);
}  // namespace CharacterKinematics
