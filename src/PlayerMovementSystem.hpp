#pragma once

#include <glm/glm.hpp>

#include <vector>

struct Transform;
struct Motor;
struct Facing;
struct AnimationState;
struct Elevation;
struct PlayerModes;
struct PlayerInputState;
struct PlayerMovementState;
struct Speed;
struct Hitbox;
class Tilemap;

/**
 * @brief Stateless player movement/animation/facing logic over component bundles.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The free-function "system" form of the former PlayerCharacter's orchestration
 * methods (Move/Update/Stop/UpdateFacing). Each operates only on the component
 * structs passed by reference - no instance, no hidden state - driven over the
 * player entity by @ref PlayerSystem and Game's update/input paths. The collision
 * pipeline is the stateless @ref CollisionSystem free functions; the entity's
 * @ref Hitbox is passed in.
 */
namespace PlayerMovementSystem
{
/// @brief Update facing direction from raw input using the latest-pressed-axis rule.
void UpdateFacing(
    Facing& facing, PlayerInputState& input, int moveDx, int moveDy, glm::vec2 normalizedDir);

/// @brief Advance the velocity-driven animation cadence (idle/walk/run frames).
/// Frame timing scales with actual speed so glide frames still animate.
void UpdateAnimation(AnimationState& anim,
                     const PlayerModes& modes,
                     const Motor& motor,
                     float dt,
                     float animationSpeed);

/// @brief Reset to idle animation and zero the motor (full stop).
void Stop(AnimationState& anim, PlayerInputState& input, PlayerModes& modes, Motor& motor);

/// @brief Canonical feet position (bottom-center) for the tile under @p position.
glm::vec2 CurrentTileCenter(glm::vec2 position, float tileSize);

/// @brief Full per-frame movement step: input -> motor -> collision -> position.
/// Mirrors PlayerCharacter::Move; drives facing/animation and the slide/lane/axis
/// collision pipeline via @p hitbox + CollisionSystem. @p elev supplies the plane.
void Step(Transform& xf,
          Motor& motor,
          Facing& facing,
          AnimationState& anim,
          Elevation& elev,
          PlayerModes& modes,
          PlayerInputState& input,
          PlayerMovementState& movement,
          const Speed& speed,
          const Hitbox& hitbox,
          glm::vec2 direction,
          float dt,
          const Tilemap* tilemap,
          const std::vector<glm::vec2>* npcPositions);
}  // namespace PlayerMovementSystem
