#pragma once

#include "SupportSurface.hpp"

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
 * Movement orchestration as free functions: the step, the facing update and the stop. Each
 * operates only on the component structs passed by reference, with no instance and no hidden
 * state. @ref PlayerSystem and Game's update and input paths drive them over the player entity.
 * The collision pipeline is the stateless @ref CollisionSystem free functions, and the entity's
 * @ref Hitbox is passed in.
 */
namespace PlayerMovementSystem
{
/**
 * @brief Update facing direction from raw input using the latest-pressed-axis rule.
 *
 * A newly pressed axis (rising edge) wins over one that was already held, so tapping a
 * second direction turns the sprite instead of leaving it facing the old way. When both
 * axes rise together the larger component of @p normalizedDir breaks the tie. With no
 * rising edge the currently faced axis is kept if it is still active; if that axis was
 * released, the other still-held axis takes over, which is why releasing one key of a held
 * diagonal turns the sprite. With no axis active, facing is left unchanged.
 *
 * @param facing        Facing component; @c dir is updated in place.
 * @param input         Input state; @c prevAxisXActive / @c prevAxisYActive are overwritten
 *                      with this frame's axis activity so the next call sees rising edges.
 * @param moveDx        Horizontal input sign (-1, 0, +1).
 * @param moveDy        Vertical input sign (-1, 0, +1); +1 is south/down.
 * @param normalizedDir Unit input direction, used only for the both-axes-rise tie-break.
 */
void UpdateFacing(
    Facing& facing, PlayerInputState& input, int moveDx, int moveDy, glm::vec2 normalizedDir);

/**
 * @brief Advance the velocity-driven animation cadence (idle/walk/run frames).
 *
 * Frame timing scales with real speed so glide frames still animate. The effective frame duration
 * is @p animationSpeed scaled by @c clamp(PLAYER_BASE_SPEED / speed, 0.4, 2.5), so the legs slow
 * through an acceleration ramp and quicken at sprint, capped at 2.5x the base rate.
 * @c modes.animationType chooses between resetting to idle and advancing the walk cycle, and does
 * not affect the timing.
 *
 * @param anim           Animation state; the accumulator and current frame are advanced.
 * @param modes          Read-only; only @c animationType is consulted.
 * @param motor          Read-only; @c velocity magnitude drives the cadence scale.
 * @param dt             Frame time in seconds.
 * @param animationSpeed Base seconds per frame (CharacterConstants::ANIM_FRAME_DURATION).
 */
void UpdateAnimation(AnimationState& anim,
                     const PlayerModes& modes,
                     const Motor& motor,
                     float dt,
                     float animationSpeed);

/**
 * @brief Reset to idle animation and zero the motor (full stop).
 *
 * Also clears the latched grid stop-target, so the next movement re-latches from wherever
 * the player ends up. Does not touch position, facing, or elevation.
 *
 * @param anim  Animation state, snapped to the idle pose.
 * @param input Input state; @c isMoving cleared.
 * @param modes Mode flags; @c animationType set to IDLE (the mode booleans are kept).
 * @param motor Motion state, fully zeroed.
 */
void Stop(AnimationState& anim, PlayerInputState& input, PlayerModes& modes, Motor& motor);

/**
 * @brief Canonical feet position (bottom-center) for the tile under @p position.
 *
 * The column is a bare floor of X; the row uses the anchor convention (feet shifted up half
 * a tile plus a small epsilon), so a character standing exactly on a tile boundary resolves
 * to the tile it visually occupies rather than the one below.
 *
 * @param position Feet position in world pixels.
 * @param tileSize Tile size in pixels; a non-positive value returns the origin.
 * @return Feet position at the center of that tile.
 */
glm::vec2 CurrentTileCenter(glm::vec2 position, float tileSize);

/**
 * @brief Full per-frame movement step: input -> motor -> collision -> position.
 *
 * Drives facing and animation, runs the slide/lane/axis collision pipeline through
 * @p hitbox + @ref CollisionSystem, and is the only place player position and elevation
 * support are advanced during normal play.
 *
 * @par Atomic commit
 * On the collision path, position and support are written together at the very end, and only
 * from the probe that actually accepted the movement (@c acceptedProbe), so a rejected move
 * leaves both untouched rather than half-applied. This is what keeps a character from ending
 * a frame standing at a position its committed support does not reach. Two paths return
 * before that commit and write @p xf on their own: the no-clip path (null @p tilemap)
 * integrates the raw displacement, and stuck recovery teleports the feet out of a solid tile.
 * @p elev is written only by the final commit.
 *
 * @par Pipeline
 * @htmlonly
 * <pre class="mermaid">
 * flowchart TD
 *     IN["input direction"] --> FA["UpdateFacing<br/>Facing, PlayerInputState"]
 *     FA --> MO["ComputeDisplacement<br/>Motor"]
 *     MO --> AN["animation gate<br/>AnimationState, PlayerModes"]
 *     AN --> NC{"tilemap null?"}
 *     NC -->|no-clip| RAW["position += disp<br/>Transform only, support not committed"]
 *     NC -->|world| ID{"displacement ~ 0?"}
 *     ID -->|idle| SR["HandleStuckRecovery<br/>Transform + Motor only if embedded"]
 *     ID -->|moving| P1{"direct probe"}
 *     P1 -->|clear| LS
 *     P1 -->|npc blocked| ZE["displacement = 0"]
 *     P1 -->|tile blocked| CS["corner slide<br/>PlayerMovementState slideDir"]
 *     CS -->|no slide, diagonal| AX["axis split, keep X or Y"]
 *     ZE --> LS
 *     CS --> LS
 *     AX --> LS["lane snap<br/>skipped after a slide or a block"]
 *     LS --> P2{"re-probe"}
 *     P2 -->|blocked| AF["axis fallback<br/>PlayerMovementState axisPref"]
 *     P2 -->|clear| KI["zero blocked axis<br/>Motor"]
 *     AF --> KI
 *     KI --> CM["commit<br/>Transform + Elevation support"]
 * </pre>
 * @endhtmlonly
 *
 * @par Blocked-axis contract
 * A wanted axis that produced no motion has its velocity dropped through
 * @ref MotionSystem::ZeroAxisX or @ref MotionSystem::ZeroAxisY. Otherwise speed accumulates
 * invisibly against the wall. The one exception is a successful corner slide, which redirected
 * that momentum productively: zeroing it there is what makes a corner climb stutter.
 *
 * @param xf       Transform; @c position (feet, world pixels) written only on commit.
 * @param motor    Motion state; velocity and the latched grid stop are advanced every call,
 *                 and a blocked axis is zeroed here.
 * @param facing   Facing; updated from raw input before any collision work.
 * @param anim     Animation state; frame/accumulator updated from actual velocity.
 * @param elev     Elevation; @c plane + @c surface committed only alongside the position.
 * @param modes    Mode flags; read for the speed multiplier, @c animationType written.
 * @param input    Input edge state; axis-active flags, @c isMoving, and the last accepted
 *                 movement direction are written.
 * @param movement Collision hysteresis (slide dir/timer, axis preference/timer, last input
 *                 signs, last safe tile center); read and written throughout.
 * @param speed    Read-only base walk speed (px/s) before mode multipliers.
 * @param hitbox   Read-only feet-anchored box dimensions used by every collision probe.
 * @param direction Raw input direction; magnitude below 0.1 counts as no input. Need not be
 *                 normalized - it is normalized internally.
 * @param dt       Frame time in seconds.
 * @param tilemap  World tilemap, or null for no-clip: the raw displacement is applied and
 *                 the function returns without committing support (Game derives the plane
 *                 afterwards via CharacterKinematics::DerivePlane). Tile size comes from
 *                 the tilemap; the 16px fallback used when it is null matches
 *                 PlayerSystem::SetTilePosition.
 * @param npcBodies Per-frame NPC feet/support snapshot, or null to skip NPC blocking.
 */
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
          const std::vector<CharacterCollisionBody>* npcBodies);
}  // namespace PlayerMovementSystem
