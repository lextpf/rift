#pragma once

#include "Hitbox.hpp"
#include "SupportSurface.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <vector>

struct PlayerMovementState;
class Tilemap;

/**
 * @brief Stateless player collision detection, wall sliding, and idle-snap logic.
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Entities
 *
 * Every collision and movement entry point takes the entity's @ref Hitbox by const reference plus
 * the per-frame state it needs: the feet position, the committed support surface, and the slide
 * and snap hysteresis in @ref PlayerMovementState. @ref CalculateFollowAlpha is the exception: it
 * is a pure timing helper with no geometry. Nothing is stored between calls, so these functions
 * form an ECS collision system over the @c Transform, @c Elevation, @c PlayerMovementState and
 * @c Hitbox components.
 *
 * @par Collision model
 * All movement modes (walk, run, bicycle) use the same full-AABB collision via
 * CollidesWithTilesStrict(). Speed differs between modes; collision geometry and
 * corner-cutting tolerances do not. The tolerance thresholds in the .cpp are calibrated
 * for 16px tiles and a 16x16 hitbox and do not scale with a different tile size.
 *
 * @par Tolerance cascade
 * An overlapping tile is not a collision by itself. Every overlapping tile runs through a
 * short cascade of permissive checks before a hard collision is reported, and each phase
 * reasons about a different geometric situation with its own tolerance budget. The phase
 * names are the file-local helpers in the .cpp, in the order they run:
 * @verbatim
 *                   +----------------------------------------+
 *   moveDx,moveDy   | CollidesWithTilesStrict                |
 *   diagonalInput   |   for each overlapping tile:           |
 *   feetPos         |     1) ShouldSkipDiagonalTile          |--+
 *                   |        cardinal grazing past a corner  |  |
 *                   |     2) ShouldTolerateWallPenetration   |  |
 *                   |        sliding along a wall face       |  +--> "no
 *                   |     3) ShouldAllowCornerCut            |  |     collision"
 *                   |        exposed convex corner with an   |  |     (tile skipped)
 *                   |        escape route, or <=15% overlap  |  |
 *                   |        with a side wall during pure    |  |
 *                   |        cardinal motion                 |  |
 *                   |     4) overlap <= 1% of hitbox area    |--+
 *                   |                                        |
 *                   |     3a) forceCollision: closed convex  |--+
 *                   |         corner under diagonal input    |  +--> "blocked"
 *                   |     5) overlap > 1% of hitbox area     |--+
 *                   +----------------------------------------+
 * @endverbatim
 * Phase 3 also owns the hard-block path 3a: its @c forceCollision out-parameter reports a
 * collision that bypasses the 1% area floor, so a sliver overlap is tolerated everywhere except
 * there.
 *
 * @par Support gating
 * A collision tile blocks only characters on its own surface and exact height
 * (@ref SurfaceSystem::CollisionBelongsTo), and two characters collide only when their
 * @ref SupportState matches exactly. This is what lets one character walk under a bridge
 * while another walks across it, using a single collision grid.
 *
 * @par Pipeline
 * When a strict collision is reported, TrySlideMovement() consults
 * GetCornerSlideDirection() to project the desired motion onto the nearest open
 * corridor and binary-searches for the largest safe step. ApplyLaneSnapping()
 * nudges the player toward tile-center lanes during cardinal movement;
 * HandleStuckRecovery() reports a teleport-out target when embedded in a solid
 * tile, which the caller is responsible for applying.
 *
 * @par No-clip (null pointers)
 * A null @p tilemap means "skip world blocking" and a null @p npcBodies means "skip NPC
 * blocking"; the player's developer no-clip mode passes null for both. Each entry point
 * degrades differently, so read the per-function note rather than assuming a uniform
 * fallback: the queries report "no collision", @ref ProbeMovement passes the current
 * support through as connected, @ref ApplyLaneSnapping returns its input untouched,
 * @ref HandleStuckRecovery reports nothing to recover, and @ref FindClosestSafeTileCenter
 * echoes the position back.
 *
 * @see Hitbox, PlayerMovementSystem, SurfaceSystem, Tilemap
 */
namespace CollisionSystem
{
/**
 * @name Collision queries
 * @brief Pure queries that test positions without modifying player state.
 * @{
 */

/**
 * @brief Check if a feet position collides with any NPC hitbox on the same support.
 *
 * An NPC whose @ref SupportState differs from @p support is skipped entirely rather than
 * tolerated, so an NPC standing on a bridge deck never blocks a character walking the ground
 * beneath it, and the reverse holds too. Both boxes shrink by
 * @ref CharacterConstants::COLLISION_EPS, so bodies resting edge-to-edge do not collide.
 *
 * Both boxes are sized from @p hitbox. @ref CharacterCollisionBody carries a feet anchor and a
 * support only, so an NPC never contributes dimensions of its own to this test.
 *
 * @param hitbox    Dimensions of the moving character's feet-anchored box.
 * @param feetPos   Candidate feet position to test, world pixels.
 * @param support   The moving character's exact surface + height; must match to collide.
 * @param npcBodies Per-frame NPC snapshot, or null/empty to skip NPC blocking (no-clip).
 * @return True when some same-support NPC box overlaps the candidate box.
 */
bool CollidesWithNPC(const Hitbox& hitbox,
                     const glm::vec2& feetPos,
                     SupportState support,
                     const std::vector<CharacterCollisionBody>* npcBodies);

/**
 * @brief Full-hitbox collision check against the tilemap (walking mode). A
 * collision tile only blocks characters on the same ground/elevation support
 * and, for elevation, the same exact height.
 *
 * "Strict" names the geometry rather than the tolerance. The whole AABB is tested, but a short
 * cascade of permissive checks runs first: diagonal grazing, wall-face penetration and convex
 * corner cutting. The movement-direction arguments feed that cascade, so passing 0 and 0 is a
 * harsher test than passing the real direction.
 *
 * @param hitbox        Dimensions of the feet-anchored box.
 * @param feetPos       Candidate feet position, world pixels.
 * @param tilemap       World tilemap, or null to skip world blocking (returns false).
 * @param moveDx        Horizontal movement sign (-1, 0, +1) for the tolerance cascade.
 * @param moveDy        Vertical movement sign (-1, 0, +1); +1 is down.
 * @param diagonalInput True when the player held two directions, which disables the
 *                      cardinal-only tolerances.
 * @param support       Support the character is (or would be) standing on. Defaults to
 *                      {Ground, 0}; omitting it therefore tests ground-owned collision only,
 *                      which is wrong rather than conservative for a character standing on an
 *                      elevation support.
 * @return True when a blocking tile overlaps after every tolerance has been applied.
 */
bool CollidesWithTilesStrict(const Hitbox& hitbox,
                             const glm::vec2& feetPos,
                             const Tilemap* tilemap,
                             int moveDx,
                             int moveDy,
                             bool diagonalInput,
                             SupportState support = {});

/**
 * @brief Unified collision check against tiles and NPCs.
 *
 * Position-only: it does not resolve the support transition, so it cannot detect an unsupported
 * step or drop. Use @ref ProbeMovement for anything that will be committed.
 *
 * @warning No production caller. The movement path uses @ref ProbeMovement, which also resolves
 * the support transition; this position-only form is exercised only by tests.
 *
 * @param hitbox        Dimensions of the feet-anchored box.
 * @param feetPos       Candidate feet position, world pixels.
 * @param tilemap       World tilemap, or null to skip world blocking.
 * @param npcBodies     Per-frame NPC snapshot, or null to skip NPC blocking.
 * @param moveDx        Horizontal movement sign for the tolerance cascade.
 * @param moveDy        Vertical movement sign for the tolerance cascade.
 * @param diagonalInput True when two directions are held.
 * @param support       Support used to gate both tiles and NPCs. Defaults to {Ground, 0};
 *                      omitting it therefore tests ground-owned collision only, which is wrong
 *                      rather than conservative for a character standing on an elevation
 *                      support.
 * @return True when either the tilemap or an NPC blocks the position.
 */
bool CollidesAt(const Hitbox& hitbox,
                const glm::vec2& feetPos,
                const Tilemap* tilemap,
                const std::vector<CharacterCollisionBody>* npcBodies,
                int moveDx = 0,
                int moveDy = 0,
                bool diagonalInput = false,
                SupportState support = {});

/**
 * @struct MovementProbeResult
 * @brief Complete result of a movement probe before any state is committed.
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Entities
 */
struct MovementProbeResult
{
    /// Support the move would reach. When @c transition.connected is false the move crossed
    /// an unsupported step/drop and @c tileBlocked is set too, so IsBlocked() covers it.
    SurfaceTransition transition{};
    bool tileBlocked{false};  ///< A world tile on the resolved support blocks the target.
    bool npcBlocked{false};   ///< A same-support NPC body blocks the target.

    /// True when the move must be rejected for any reason. Callers keep the exact probe
    /// that accepted a movement so its resolved support can be committed with the position.
    bool IsBlocked() const { return !transition.connected || tileBlocked || npcBlocked; }
};

/**
 * @brief Resolve candidate support and collision as one read-only transaction.
 *
 * Order matters. The support transition resolves first, and the tile test then runs against the
 * resolved candidate support rather than the current one. That is what lets a character step onto
 * a ramp without the ramp's own railing blocking the first step. A disconnected transition
 * short-circuits and leaves @c npcBlocked false.
 *
 * @param hitbox         Dimensions of the feet-anchored box.
 * @param origin         Feet position the move starts from, world pixels.
 * @param target         Candidate feet position, world pixels.
 * @param currentSupport Support currently committed on the entity.
 * @param tilemap        World tilemap, or null: the transition then passes
 *                       @p currentSupport through as connected and no tile test runs.
 * @param npcBodies      Per-frame NPC snapshot, or null to skip NPC blocking.
 * @param moveDx         Horizontal movement sign for the tolerance cascade.
 * @param moveDy         Vertical movement sign for the tolerance cascade.
 * @param diagonalInput  True when two directions are held.
 * @return The probe result; commit position and support together only when it is unblocked.
 */
MovementProbeResult ProbeMovement(const Hitbox& hitbox,
                                  glm::vec2 origin,
                                  glm::vec2 target,
                                  SupportState currentSupport,
                                  const Tilemap* tilemap,
                                  const std::vector<CharacterCollisionBody>* npcBodies,
                                  int moveDx,
                                  int moveDy,
                                  bool diagonalInput);

/**
 * @brief Find the nearest non-colliding tile center for stuck recovery.
 *
 * Searches a fixed 5x5 tile window around the feet. On an elevation support, candidate
 * tiles whose authored elevation differs from the support height are rejected, so recovery
 * cannot drop a character off its deck.
 *
 * @param hitbox    Dimensions of the feet-anchored box.
 * @param playerPos Current (embedded) feet position, world pixels.
 * @param support   Support to keep the character on while searching.
 * @param tilemap   World tilemap, or null - @p playerPos is then echoed back unchanged.
 * @param npcBodies Per-frame NPC snapshot, or null to ignore NPCs while searching.
 * @return Feet position at the bottom-center of the best tile, or @p playerPos when the
 *         whole window is blocked (the caller must tolerate a no-op result).
 */
glm::vec2 FindClosestSafeTileCenter(const Hitbox& hitbox,
                                    glm::vec2 playerPos,
                                    SupportState support,
                                    const Tilemap* tilemap,
                                    const std::vector<CharacterCollisionBody>* npcBodies);

/**
 * @brief Exponential smoothing factor for the lane-snap correction.
 *
 * Independent of any hitbox, and frame-rate independent: the same wall-clock settle time results
 * at any frame rate. Feed the return value to a lerp. @ref ApplyLaneSnapping is the only
 * production caller.
 *
 * @param deltaTime  Frame time in seconds; negatives are clamped to 0.
 * @param settleTime Seconds to converge to within @p epsilon; clamped away from 0.
 * @param epsilon    Residual fraction remaining after @p settleTime (default 1%).
 * @return Blend factor in [0, 1].
 *
 * @see rift::ExpApproachAlpha - the identical general-purpose helper in MathUtils.hpp that the
 *      camera and the editor use. Tuning changed here does not reach them.
 */
float CalculateFollowAlpha(float deltaTime, float settleTime, float epsilon = 0.01f);

/// @}

/**
 * @name Movement resolution
 * @brief Resolve blocked movement using (and mutating) the supplied
 * PlayerMovementState hysteresis - never any stored player state.
 * @{
 */

/**
 * @brief Attempt axis-aligned slide movement when the direct path is blocked.
 *
 * An unblocked direct probe returns @p desiredMovement untouched. Only a tile block slides: an
 * NPC block stops the character and clears the hysteresis. Otherwise the perpendicular offset is
 * probed outward a pixel at a time up to 16 px, clamped to @p currentSpeed * @p deltaTime and then
 * to 75% of the forward distance, and finally binary-searched for the largest safe forward
 * fraction. Both the preferred slide side and its opposite are tried before the function gives up.
 *
 * The returned vector is usually not that search result: it is first mixed 35% of the way from
 * @p desiredMovement toward the result, and the mix is returned when it is still clear. When the
 * forward path never clears but the perpendicular step does, that step alone is returned.
 *
 * @param hitbox          Dimensions of the feet-anchored box.
 * @param playerPos       Current feet position, world pixels.
 * @param movement        Slide hysteresis; read and written (direction + commit timer).
 * @param support         Support the character is currently on.
 * @param desiredMovement This frame's blocked displacement.
 * @param normalizedDir   Accepted but unused: the slide plane is derived from
 *                        @p desiredMovement's dominant axis, not from this.
 * @param deltaTime       Frame time in seconds; with @p currentSpeed caps the slide step.
 * @param currentSpeed    Mode-multiplied target speed (px/s); caps the perpendicular shove.
 * @param tilemap         World tilemap, or null - the direct probe then succeeds and
 *                        @p desiredMovement comes back unchanged.
 * @param npcBodies       Per-frame NPC snapshot, or null to skip NPC blocking.
 * @param moveDx          Horizontal movement sign for the tolerance cascade.
 * @param moveDy          Vertical movement sign for the tolerance cascade.
 * @param diagonalInput   True when two directions are held.
 * @return Achievable movement vector (may be zero if fully blocked).
 */
glm::vec2 TrySlideMovement(const Hitbox& hitbox,
                           glm::vec2 playerPos,
                           PlayerMovementState& movement,
                           SupportState support,
                           glm::vec2 desiredMovement,
                           glm::vec2 normalizedDir,
                           float deltaTime,
                           float currentSpeed,
                           const Tilemap* tilemap,
                           const std::vector<CharacterCollisionBody>* npcBodies,
                           int moveDx,
                           int moveDy,
                           bool diagonalInput);

/**
 * @brief Snap the player toward tile-center lanes during cardinal movement.
 *
 * Only the axis perpendicular to travel is corrected, and only while that same perpendicular axis
 * carries no motion of its own beyond a small threshold. Any real diagonal component returns the
 * input untouched. The correction is exponentially smoothed toward @p tileCenter over a 0.3 s
 * settle and clamped to 1.2 px per frame, so it ratchets into tight gaps instead of snapping. A
 * blocked correction is retried as a perpendicular-only step before it is dropped.
 *
 * @param hitbox          Dimensions of the feet-anchored box.
 * @param playerPos       Current feet position, world pixels.
 * @param tileCenter      Feet position at the center of the occupied tile (the lane).
 * @param support         Support the character is currently on.
 * @param desiredMovement This frame's displacement, returned with the correction folded in.
 * @param normalizedDir   Unit direction; selects which axis counts as travel.
 * @param deltaTime       Frame time in seconds, for the smoothing alpha.
 * @param tilemap         World tilemap, or null - @p desiredMovement is then returned
 *                        unchanged (no-clip needs no lane discipline).
 * @param npcBodies       Per-frame NPC snapshot, or null to skip NPC blocking.
 * @param moveDx          Horizontal movement sign for the tolerance cascade.
 * @param moveDy          Vertical movement sign for the tolerance cascade.
 * @return @p desiredMovement with at most one perpendicular correction added.
 */
glm::vec2 ApplyLaneSnapping(const Hitbox& hitbox,
                            glm::vec2 playerPos,
                            glm::vec2 tileCenter,
                            SupportState support,
                            glm::vec2 desiredMovement,
                            glm::vec2 normalizedDir,
                            float deltaTime,
                            const Tilemap* tilemap,
                            const std::vector<CharacterCollisionBody>* npcBodies,
                            int moveDx,
                            int moveDy);

/**
 * @brief Resolve stuck state: return a teleport-out feet position if embedded in
 * a solid tile, else std::nullopt (and update movement.lastSafeTileCenter).
 *
 * This reports only and moves nothing. The caller decides how to apply the result, and
 * PlayerMovementSystem::Step also resets the motor itself, so a teleport that skipped this
 * function would have to repeat that step.
 *
 * The reported position is already a tile bottom-center, so its Y sits exactly on the boundary
 * between the safe row and the row below. A caller that re-derives a tile row from it must use
 * @ref TileMath::AnchorTileRow; a bare floor (@ref TileMath::TileIndex) selects the row below and
 * places the feet one whole tile past the validated tile.
 *
 * @param hitbox            Dimensions of the feet-anchored box.
 * @param playerPos         Current feet position, world pixels.
 * @param movement          Hysteresis state; @c lastSafeTileCenter is written when not
 *                          stuck, so the field trails the last known-good tile.
 * @param support           Support to keep the character on while recovering.
 * @param currentTileCenter Feet position at the center of the occupied tile.
 * @param tilemap           World tilemap, or null - returns @c std::nullopt immediately,
 *                          without updating @c lastSafeTileCenter.
 * @param npcBodies         Per-frame NPC snapshot, or null to ignore NPCs while searching.
 * @return Teleport-out feet position when embedded in a blocking tile, else nothing.
 */
std::optional<glm::vec2> HandleStuckRecovery(const Hitbox& hitbox,
                                             glm::vec2 playerPos,
                                             PlayerMovementState& movement,
                                             SupportState support,
                                             glm::vec2 currentTileCenter,
                                             const Tilemap* tilemap,
                                             const std::vector<CharacterCollisionBody>* npcBodies);

/**
 * @brief Determine slide direction when blocked by a tile corner.
 *
 * Returns a unit perpendicular direction, or zero when sliding does not apply: a flat wall with
 * no perpendicular opening, a nearest corner further than 0.75 tiles away, a corner authored as
 * cut-blocked, or the wedged case where neither candidate side clears the 10 px outward probe.
 *
 * @warning @p moveDirX and @p moveDirY are accepted but ignored, and the implementation leaves
 * them unnamed. The forward direction comes from @p testPos minus @p playerPos instead, so a
 * caller cannot steer this by passing a different direction. The parameters remain only to keep
 * the call signature stable.
 *
 * @param hitbox    Dimensions of the feet-anchored box.
 * @param playerPos Current feet position, world pixels.
 * @param movement  Slide hysteresis; read for the tie-break and written with the choice. The
 *                  ~120 ms commit timer is armed only when the direction changes
 *                  significantly. On a flat wall or a too-distant corner the stored
 *                  direction is cleared instead, and only once that timer has expired.
 * @param support   Support the character is currently on.
 * @param testPos   Blocked candidate position; its offset from @p playerPos is the forward
 *                  direction, and the dominant axis of that offset picks the slide plane.
 * @param tilemap   World tilemap, or null - returns a zero vector.
 * @param moveDirX  Ignored.
 * @param moveDirY  Ignored.
 * @return Unit slide direction perpendicular to travel, or zero to stop.
 */
glm::vec2 GetCornerSlideDirection(const Hitbox& hitbox,
                                  glm::vec2 playerPos,
                                  PlayerMovementState& movement,
                                  SupportState support,
                                  const glm::vec2& testPos,
                                  const Tilemap* tilemap,
                                  int moveDirX,
                                  int moveDirY);

/// @}
}  // namespace CollisionSystem
