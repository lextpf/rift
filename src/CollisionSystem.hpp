#pragma once

#include "Hitbox.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <vector>

struct PlayerMovementState;
class Tilemap;

/**
 * @file CollisionSystem.hpp
 * @brief Stateless player collision detection, wall sliding, and idle-snap logic.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The free-function form of the former @c CollisionResolver class. Every entry
 * point takes the entity's @ref Hitbox by const reference plus the per-frame
 * state it needs (the feet position, the logical @c plane, and the slide/snap
 * hysteresis in @ref PlayerMovementState) - there is no stored state, so this is
 * the shape of an ECS collision *system* operating on @c Transform + @c Elevation
 * + @c PlayerMovementState + @c Hitbox components.
 *
 * @par Collision Model
 * All movement modes (walk, run, bicycle) use the same full-AABB collision via
 * CollidesWithTilesStrict(). Speed differs between modes; collision geometry and
 * corner-cutting tolerances do not.
 *
 * @par Pipeline
 * When a strict collision is reported, TrySlideMovement() consults
 * GetCornerSlideDirection() to project the desired motion onto the nearest open
 * corridor and binary-searches for the largest safe step. ApplyLaneSnapping()
 * nudges the player toward tile-center lanes during cardinal movement;
 * HandleStuckRecovery() reports a teleport-out target when embedded in a solid
 * tile (the caller applies it via PlayerSystem::SetPositionRaw so position-snap +
 * motor-reset stay in one place).
 *
 * @see Hitbox, PlayerMovementSystem, Tilemap, CollisionMap
 */
namespace CollisionSystem
{
/// @name Collision Queries
/// @brief Pure queries that test positions without modifying player state.
/// @{

/// @brief Check if a feet position collides with any NPC hitbox.
bool CollidesWithNPC(const Hitbox& hitbox,
                     const glm::vec2& feetPos,
                     const std::vector<glm::vec2>* npcPositions);

/// @brief Full-hitbox collision check against the tilemap (walking mode). A
/// collision tile only blocks when its elevation is at-or-below @p plane.
bool CollidesWithTilesStrict(const Hitbox& hitbox,
                             const glm::vec2& feetPos,
                             const Tilemap* tilemap,
                             int moveDx,
                             int moveDy,
                             bool diagonalInput,
                             int plane = 0);

/// @brief Unified collision check against tiles and NPCs.
bool CollidesAt(const Hitbox& hitbox,
                const glm::vec2& feetPos,
                const Tilemap* tilemap,
                const std::vector<glm::vec2>* npcPositions,
                int moveDx = 0,
                int moveDy = 0,
                bool diagonalInput = false,
                int plane = 0);

/// @brief Find the nearest non-colliding tile center for stuck recovery.
glm::vec2 FindClosestSafeTileCenter(const Hitbox& hitbox,
                                    glm::vec2 playerPos,
                                    int plane,
                                    const Tilemap* tilemap,
                                    const std::vector<glm::vec2>* npcPositions);

/// @brief Calculate exponential smoothing alpha for camera/snap following.
/// Pure (hitbox-independent): @p deltaTime / @p settleTime in seconds, @p epsilon
/// convergence threshold (default 1%). Returns alpha in [0,1] for use with lerp.
float CalculateFollowAlpha(float deltaTime, float settleTime, float epsilon = 0.01f);

/// @}

/// @name Movement Resolution
/// @brief Resolve blocked movement using (and mutating) the supplied
/// PlayerMovementState hysteresis - never any stored player state.
/// @{

/// @brief Attempt axis-aligned slide movement when the direct path is blocked.
/// @return Achievable movement vector (may be zero if fully blocked).
glm::vec2 TrySlideMovement(const Hitbox& hitbox,
                           glm::vec2 playerPos,
                           PlayerMovementState& movement,
                           int plane,
                           glm::vec2 desiredMovement,
                           glm::vec2 normalizedDir,
                           float deltaTime,
                           float currentSpeed,
                           const Tilemap* tilemap,
                           const std::vector<glm::vec2>* npcPositions,
                           int moveDx,
                           int moveDy,
                           bool diagonalInput);

/// @brief Snap the player toward tile-center lanes during cardinal movement.
glm::vec2 ApplyLaneSnapping(const Hitbox& hitbox,
                            glm::vec2 playerPos,
                            glm::vec2 tileCenter,
                            int plane,
                            glm::vec2 desiredMovement,
                            glm::vec2 normalizedDir,
                            float deltaTime,
                            const Tilemap* tilemap,
                            const std::vector<glm::vec2>* npcPositions,
                            int moveDx,
                            int moveDy);

/// @brief Resolve stuck state: return a teleport-out feet position if embedded in
/// a solid tile, else std::nullopt (and update movement.lastSafeTileCenter).
std::optional<glm::vec2> HandleStuckRecovery(const Hitbox& hitbox,
                                             glm::vec2 playerPos,
                                             PlayerMovementState& movement,
                                             int plane,
                                             glm::vec2 currentTileCenter,
                                             const Tilemap* tilemap,
                                             const std::vector<glm::vec2>* npcPositions);

/// @brief Determine slide direction when blocked by a tile corner.
glm::vec2 GetCornerSlideDirection(const Hitbox& hitbox,
                                  glm::vec2 playerPos,
                                  PlayerMovementState& movement,
                                  int plane,
                                  const glm::vec2& testPos,
                                  const Tilemap* tilemap,
                                  int moveDirX,
                                  int moveDirY);

/// @}
}  // namespace CollisionSystem
