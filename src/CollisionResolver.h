#pragma once

#include <glm/glm.hpp>
#include <vector>

class Tilemap;
class PlayerCharacter;

/**
 * @struct TileOverlapContext
 * @brief Snapshot of a player-hitbox / tile overlap for collision evaluation.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Built once per overlapping tile inside CollidesWithTilesStrict() and passed
 * to the tolerance helpers (ShouldSkipDiagonalTile, ShouldTolerateWallPenetration,
 * ShouldAllowCornerCut) so they can make decisions without redundant recalculation.
 *
 * @par Coordinate Conventions
 * All positions use the bottom-center (feet) convention. The hitbox extends
 * upward from the feet position by `PlayerCharacter::HITBOX_HEIGHT` pixels.
 *
 * @code
 *   +--------+  <- tileMinY          +===+  <- hitbox top
 *   |  tile  |                       |   |
 *   +--------+  <- tileMaxY          +-o-+  <- bottomCenterPos (feet)
 *                                     hitbox
 * @endcode
 */
struct TileOverlapContext
{
    glm::vec2 bottomCenterPos;  ///< Player feet position being tested
    glm::vec2 hitboxCenter;     ///< Center of the player hitbox AABB
    float hitboxArea;           ///< Total hitbox area in pixels squared
    float overlapW;             ///< Horizontal overlap between hitbox and tile (pixels)
    float overlapH;             ///< Vertical overlap between hitbox and tile (pixels)
    float overlapRatio;         ///< Overlap area as a fraction of hitbox area (0-1)
    int tx, ty;                 ///< Tile grid coordinates of the overlapping tile
    float tileMinX, tileMaxX;   ///< Tile AABB horizontal bounds (world pixels)
    float tileMinY, tileMaxY;   ///< Tile AABB vertical bounds (world pixels)
    int playerTileX;            ///< Tile column the player's feet center occupies
    int playerTileY;            ///< Tile row the player's feet center occupies
    int moveDx, moveDy;         ///< Movement direction signs (-1, 0, or +1)
    bool diagonalInput;         ///< True if two directional keys are held simultaneously
    float tileW, tileH;         ///< Tile dimensions in pixels (typically 16x16)
};

/**
 * @class CollisionResolver
 * @brief Player collision detection, wall sliding, and idle snap logic.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * CollisionResolver encapsulates the full collision pipeline that was
 * previously spread across ~15 private methods inside PlayerCharacter.
 * It accesses PlayerCharacter internals via friend access and is stored
 * as a member (`m_Collision`) on the player.
 *
 * @par Collision Modes
 * The resolver supports two collision styles selected by movement speed:
 *
 * | Mode   | Method                  | When Used               | Behavior                 |
 * |--------|-------------------------|-------------------------|--------------------------|
 * | Strict | CollidesWithTilesStrict | Walking                 | Full 16x16 hitbox AABB   |
 * | Center | CollidesWithTilesCenter | Running / Bicycle       | Single center-point test |
 *
 * @par Wall Sliding
 * When a direct move is blocked, TrySlideMovement() decomposes the movement
 * into axis-aligned components and attempts each independently:
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 *     classDef ok fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef blocked fill:#4a2020,stroke:#ef4444,color:#e2e8f0
 *     classDef slide fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *
 *     A["Move(dx, dy)"]:::ok --> B{Blocked?}
 *     B -->|No| C["Apply full movement"]:::ok
 *     B -->|Yes| D["Try X only"]:::slide
 *     D --> E{Blocked?}
 *     E -->|No| F["Slide along X"]:::ok
 *     E -->|Yes| G["Try Y only"]:::slide
 *     G --> H{Blocked?}
 *     H -->|No| I["Slide along Y"]:::ok
 *     H -->|Yes| J["No movement"]:::blocked
 * </pre>
 * @endhtmlonly
 *
 * @par Corner Cutting
 * During strict-mode walking, the resolver applies tolerance rules that allow
 * the player to slide around tile corners rather than getting stuck on them.
 * Three helpers evaluate each tile overlap:
 * - **ShouldSkipDiagonalTile**: Ignores diagonal tiles during cardinal movement
 * - **ShouldTolerateWallPenetration**: Permits shallow wall overlap for corridor sliding
 * - **ShouldAllowCornerCut**: Evaluates whether a corner overlap is small enough to slide past
 *
 * @par Lane Snapping
 * ApplyLaneSnapping() nudges the player toward tile-center lanes during
 * cardinal movement, preventing the "half-tile offset" problem where
 * diagonal movement leaves the player misaligned with corridors.
 *
 * @par Idle Snap
 * HandleIdleSnap() smoothly moves the player to the nearest tile center
 * when they stop moving, using a smoothstep interpolation.
 *
 * @par Ownership
 * CollisionResolver stores a non-owning pointer to its parent PlayerCharacter.
 * The pointer is rebound via Rebind() after move construction/assignment.
 *
 * @see PlayerCharacter, Tilemap, CollisionMap
 */
class CollisionResolver
{
public:
    /**
     * @brief Construct a resolver bound to a player character.
     * @param player The player character this resolver operates on.
     */
    explicit CollisionResolver(PlayerCharacter& player)
        : m_Player(&player)
    {
    }

    /**
     * @brief Rebind to a different PlayerCharacter instance.
     *
     * Called by PlayerCharacter's move constructor/assignment to update
     * the internal pointer after the player object has been relocated.
     *
     * @param player The new player character to bind to.
     */
    void Rebind(PlayerCharacter& player) { m_Player = &player; }

    /// @name Collision Queries
    /// @brief Pure queries that test positions without modifying player state.
    /// @{

    /**
     * @brief Check if a feet position collides with any NPC hitbox.
     * @param feetPos Player feet position to test.
     * @param npcPositions List of NPC feet positions (nullable).
     * @return True if the position overlaps an NPC hitbox.
     */
    bool CollidesWithNPC(const glm::vec2& feetPos,
                         const std::vector<glm::vec2>* npcPositions) const;

    /**
     * @brief Full-hitbox collision check against the tilemap (walking mode).
     *
     * Tests all tiles overlapping the player's 16x16 hitbox, applying
     * corner-cutting tolerance and diagonal-skip rules.
     *
     * @param feetPos Player feet position to test.
     * @param tilemap Tilemap for collision queries.
     * @param moveDx Movement direction X sign (-1, 0, or +1).
     * @param moveDy Movement direction Y sign (-1, 0, or +1).
     * @param diagonalInput True if the player is pressing two direction keys.
     * @return True if the position is blocked.
     */
    bool CollidesWithTilesStrict(const glm::vec2& feetPos,
                                 const Tilemap* tilemap,
                                 int moveDx,
                                 int moveDy,
                                 bool diagonalInput) const;

    /**
     * @brief Center-point collision check (running/bicycle mode).
     *
     * Tests only the single tile under the player's center point,
     * allowing more aggressive corner cutting at higher speeds.
     *
     * @param feetPos Player feet position to test.
     * @param tilemap Tilemap for collision queries.
     * @return True if the center tile is blocked.
     */
    bool CollidesWithTilesCenter(const glm::vec2& feetPos, const Tilemap* tilemap) const;

    /**
     * @brief Unified collision check dispatching to strict or center mode.
     *
     * Entry point for all collision testing. Checks both tile collision
     * and NPC collision, selecting the appropriate tile-check mode.
     *
     * @param feetPos Player feet position to test.
     * @param tilemap Tilemap for collision queries.
     * @param npcPositions List of NPC feet positions (nullable).
     * @param sprintMode True to use center-point collision (running/bicycle).
     * @param moveDx Movement direction X sign.
     * @param moveDy Movement direction Y sign.
     * @param diagonalInput True if pressing two direction keys.
     * @return True if the position is blocked by tiles or NPCs.
     */
    bool CollidesAt(const glm::vec2& feetPos,
                    const Tilemap* tilemap,
                    const std::vector<glm::vec2>* npcPositions,
                    bool sprintMode,
                    int moveDx = 0,
                    int moveDy = 0,
                    bool diagonalInput = false) const;

    /**
     * @brief Check if the player hitbox is penetrating a tile corner diagonally.
     * @param feetPos Player feet position to test.
     * @param tilemap Tilemap for collision queries.
     * @return True if a diagonal corner penetration is detected.
     */
    bool IsCornerPenetration(const glm::vec2& feetPos, const Tilemap* tilemap) const;

    /**
     * @brief Compute ejection vector when sprinting into a tile corner.
     *
     * Finds the nearest safe position by testing small offsets perpendicular
     * to the movement direction.
     *
     * @param tilemap Tilemap for collision queries.
     * @param npcPositions List of NPC feet positions (nullable).
     * @param normalizedDir Normalized movement direction.
     * @return Ejection offset vector to apply to the player position.
     */
    glm::vec2 ComputeSprintCornerEject(const Tilemap* tilemap,
                                       const std::vector<glm::vec2>* npcPositions,
                                       glm::vec2 normalizedDir) const;

    /**
     * @brief Find the nearest non-colliding tile center for stuck recovery.
     *
     * Searches in expanding rings around the player's current position
     * for a tile center that passes collision checks.
     *
     * @param tilemap Tilemap for collision queries.
     * @param npcPositions List of NPC feet positions (nullable).
     * @return Feet position of the nearest safe tile center.
     */
    glm::vec2 FindClosestSafeTileCenter(const Tilemap* tilemap,
                                        const std::vector<glm::vec2>* npcPositions) const;

    /**
     * @brief Calculate exponential smoothing alpha for camera/snap following.
     *
     * Produces frame-rate-independent smoothing: the result scales with
     * delta time so visual speed is consistent regardless of frame rate.
     *
     * @param deltaTime Frame time in seconds.
     * @param settleTime Desired time to reach target (seconds).
     * @param epsilon Convergence threshold (default: 1%).
     * @return Alpha in [0,1] for use with lerp.
     */
    static float CalculateFollowAlpha(float deltaTime, float settleTime, float epsilon = 0.01f);

    /// @}

    /// @name Movement Resolution
    /// @brief Methods that resolve blocked movement by modifying player state.
    /// @{

    /**
     * @brief Attempt axis-aligned slide movement when the direct path is blocked.
     *
     * Decomposes the desired movement into X-only and Y-only components,
     * testing each independently. Returns the best achievable movement.
     * Updates slide hysteresis and axis preference state on the player.
     *
     * @param desiredMovement Original (blocked) movement vector.
     * @param normalizedDir Normalized movement direction.
     * @param deltaTime Frame time in seconds.
     * @param currentSpeed Current movement speed (pixels/second).
     * @param tilemap Tilemap for collision queries.
     * @param npcPositions NPC feet positions (nullable).
     * @param sprintMode Whether center-point collision is used.
     * @param moveDx Movement direction X sign.
     * @param moveDy Movement direction Y sign.
     * @param diagonalInput True if pressing two direction keys.
     * @return Achievable movement vector (may be zero if fully blocked).
     */
    glm::vec2 TrySlideMovement(glm::vec2 desiredMovement,
                               glm::vec2 normalizedDir,
                               float deltaTime,
                               float currentSpeed,
                               const Tilemap* tilemap,
                               const std::vector<glm::vec2>* npcPositions,
                               bool sprintMode,
                               int moveDx,
                               int moveDy,
                               bool diagonalInput);

    /**
     * @brief Snap the player toward tile-center lanes during cardinal movement.
     *
     * When moving along a single axis, gently nudges the perpendicular axis
     * toward the nearest tile center to prevent half-tile misalignment.
     * Updates axis preference state on the player.
     *
     * @param desiredMovement Original movement vector.
     * @param normalizedDir Normalized movement direction.
     * @param deltaTime Frame time in seconds.
     * @param tilemap Tilemap for collision queries.
     * @param npcPositions NPC feet positions (nullable).
     * @param sprintMode Whether center-point collision is used.
     * @param moveDx Movement direction X sign.
     * @param moveDy Movement direction Y sign.
     * @return Movement vector adjusted with lane-snapping correction.
     */
    glm::vec2 ApplyLaneSnapping(glm::vec2 desiredMovement,
                                glm::vec2 normalizedDir,
                                float deltaTime,
                                const Tilemap* tilemap,
                                const std::vector<glm::vec2>* npcPositions,
                                bool sprintMode,
                                int moveDx,
                                int moveDy);

    /**
     * @brief Smooth-snap the player to the nearest tile center when idle.
     *
     * Uses smoothstep interpolation to gently move the player to the
     * nearest tile center after they stop providing input. Modifies
     * the player's position and snap-progress state directly.
     *
     * @param deltaTime Frame time in seconds.
     * @param tilemap Tilemap for collision queries.
     * @param npcPositions NPC feet positions (nullable).
     */
    void HandleIdleSnap(float deltaTime,
                        const Tilemap* tilemap,
                        const std::vector<glm::vec2>* npcPositions);

    /**
     * @brief Determine slide direction when blocked by a tile corner.
     *
     * Given a blocked position, tests perpendicular directions to find
     * a valid slide axis. Uses hysteresis to prevent jittering between
     * slide directions on successive frames.
     *
     * @param testPos Position to test from.
     * @param tilemap Tilemap for collision queries.
     * @param moveDirX Movement direction X sign.
     * @param moveDirY Movement direction Y sign.
     * @return Unit-length slide direction, or zero vector if no slide is possible.
     */
    glm::vec2 GetCornerSlideDirection(const glm::vec2& testPos,
                                      const Tilemap* tilemap,
                                      int moveDirX,
                                      int moveDirY);

    /// @}

private:
    /// @name Tile Overlap Evaluation
    /// @brief Helpers that evaluate individual tile overlaps during strict-mode checks.
    /// @{

    /// @brief Check if a diagonal corner tile should be ignored during cardinal movement.
    /// @param ctx Overlap context for the tile being evaluated.
    bool ShouldSkipDiagonalTile(const TileOverlapContext& ctx) const;

    /// @brief Check if shallow wall penetration should be tolerated (corridor sliding).
    /// @param ctx Overlap context for the tile being evaluated.
    bool ShouldTolerateWallPenetration(const TileOverlapContext& ctx) const;

    /**
     * @brief Evaluate corner cutting and side-wall tolerance for a tile overlap.
     * @param ctx Overlap context for the tile being evaluated.
     * @param tilemap Tilemap for adjacent-tile queries.
     * @param[out] forceCollision Set to true if a definite collision is detected
     *             (e.g., closed corner where both adjacent tiles are solid).
     * @return True if the overlap should be tolerated (skip this tile).
     */
    bool ShouldAllowCornerCut(const TileOverlapContext& ctx,
                              const Tilemap* tilemap,
                              bool& forceCollision) const;

    /// @}

    PlayerCharacter* m_Player;  ///< Non-owning pointer to the parent player character.
};
