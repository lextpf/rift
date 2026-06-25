#include "NpcAiSystem.hpp"

#include "AnimationState.hpp"
#include "CharacterConstants.hpp"
#include "CharacterKinematics.hpp"
#include "CollisionGeometry.hpp"
#include "Elevation.hpp"
#include "Facing.hpp"
#include "Identity.hpp"
#include "NpcIdle.hpp"
#include "NpcTag.hpp"
#include "Patrol.hpp"
#include "PatrolRoute.hpp"
#include "Speed.hpp"
#include "Tilemap.hpp"
#include "TileMath.hpp"
#include "Transform.hpp"

#include <cmath>
#include <random>

namespace
{
constexpr float WAYPOINT_REACH_THRESHOLD = 0.5f;  // Pixels to count as "reached".
constexpr float MIN_MOVEMENT_DIST = 0.001f;       // Avoid divide-by-zero.

// Used for random look-around direction picks.
constexpr Direction ALL_DIRECTIONS[] = {
    Direction::LEFT, Direction::RIGHT, Direction::UP, Direction::DOWN};

// Cycle through random facing directions during idle.
void UpdateLookAround(NpcIdle& idle, Facing& facing, float dt, std::mt19937& rng)
{
    idle.lookAroundTimer -= dt;
    if (idle.lookAroundTimer <= 0.0f)
    {
        facing.dir = ALL_DIRECTIONS[std::uniform_int_distribution<int>(0, 3)(rng)];
        idle.lookAroundTimer = 2.0f;
    }
}

// Transition to standing-still idle state.
void EnterStandingStill(NpcIdle& idle,
                        Facing& facing,
                        AnimationState& anim,
                        bool isRandom,
                        float duration,
                        std::mt19937& rng)
{
    idle.standingStill = true;
    // When not random (e.g. no patrol route found), timer stays at 0 so the NPC
    // stays in standing-still/look-around mode indefinitely until a route exists.
    idle.randomStandStillTimer = isRandom ? duration : 0.0f;
    idle.lookAroundTimer = 2.0f;
    CharacterKinematics::ResetAnimation(anim);

    facing.dir = ALL_DIRECTIONS[std::uniform_int_distribution<int>(0, 3)(rng)];
}

// Set facing direction from a tile movement delta (equal-magnitude diagonals
// resolve to vertical; a zero delta leaves the current facing unchanged).
void UpdateDirectionFromMovement(Facing& facing, int dx, int dy)
{
    if (dx != 0 || dy != 0)
    {
        facing.dir = CardinalFromDelta(static_cast<float>(dx), static_cast<float>(dy));
    }
}

bool CheckPlayerCollision(const glm::vec2& newPosition, const glm::vec2* playerPos)
{
    if (!playerPos)
    {
        return false;
    }

    return CollisionGeometry::FeetBoxesOverlap(newPosition,
                                               *playerPos,
                                               CharacterConstants::HALF_HITBOX_WIDTH,
                                               CharacterConstants::HITBOX_HEIGHT,
                                               CharacterConstants::COLLISION_EPS);
}
}  // namespace

namespace NpcAiSystem
{
void Update(Transform& xf,
            Elevation& elev,
            Facing& facing,
            AnimationState& anim,
            NpcIdle& idle,
            Patrol& patrol,
            PatrolRoute& route,
            const Speed& speed,
            float dt,
            const Tilemap* tilemap,
            const glm::vec2* playerPosition,
            std::mt19937& rng)
{
    if (!tilemap)
        return;

    // Smooth elevation transition (must run regardless of movement state)
    CharacterKinematics::UpdateElevation(elev, dt);

    bool isCollidingWithPlayer = false;
    if (playerPosition)
    {
        if (CollisionGeometry::FeetBoxesOverlap(xf.position,
                                                *playerPosition,
                                                CharacterConstants::HALF_HITBOX_WIDTH,
                                                CharacterConstants::HITBOX_HEIGHT,
                                                CharacterConstants::COLLISION_EPS))
        {
            isCollidingWithPlayer = true;
            idle.waitTimer = 0.5f;
        }
    }

    if (idle.isStopped || isCollidingWithPlayer)
    {
        CharacterKinematics::ResetAnimation(anim);
        return;
    }

    if (idle.standingStill)
    {
        CharacterKinematics::ResetAnimation(anim);

        // Random pause: count down timer.
        if (idle.randomStandStillTimer > 0.0f)
        {
            idle.randomStandStillTimer -= dt;
            if (idle.randomStandStillTimer <= 0.0f)
            {
                idle.standingStill = false;
                idle.randomStandStillTimer = 0.0f;
            }
            else
            {
                // Look around while paused.
                UpdateLookAround(idle, facing, dt, rng);
                return;
            }
        }
        else
        {
            // No path available: look around indefinitely.
            UpdateLookAround(idle, facing, dt, rng);
            return;
        }
    }

    const int tileWidth = tilemap->GetTileWidth();
    const int tileHeight = tilemap->GetTileHeight();
    if (tileWidth <= 0 || tileHeight <= 0)
        return;

    patrol.tileX = TileMath::TileIndex(xf.position.x, static_cast<float>(tileWidth));
    // Standing-tile row: feet nudged up so an NPC on a tile boundary registers as
    // the tile above (see TileMath::StandingTileRow).
    patrol.tileY = TileMath::StandingTileRow(xf.position.y, static_cast<float>(tileHeight));

    if (idle.waitTimer > 0.0f)
    {
        idle.waitTimer -= dt;
        if (idle.waitTimer < 0.0f)
            idle.waitTimer = 0.0f;
    }

    if (idle.waitTimer > 0.0f)
        return;

    anim.animationTime += dt;
    if (anim.animationTime >= CharacterConstants::ANIM_FRAME_DURATION)
    {
        anim.animationTime -= CharacterConstants::ANIM_FRAME_DURATION;
        CharacterKinematics::AdvanceWalkAnimation(anim);
    }

    if (route.IsValid() && idle.randomStandStillCheckTimer > 0.0f)
    {
        idle.randomStandStillCheckTimer -= dt;
    }

    glm::vec2 targetPos = TileMath::TileFeetCenter(patrol.targetTileX,
                                                   patrol.targetTileY,
                                                   static_cast<float>(tileWidth),
                                                   static_cast<float>(tileHeight));

    glm::vec2 toTarget = targetPos - xf.position;
    float dist = glm::length(toTarget);

    // Check if we've reached the current waypoint.
    if (dist < WAYPOINT_REACH_THRESHOLD)
    {
        // Verify the target tile is still walkable before snapping.
        int targetTileX = TileMath::TileIndex(targetPos.x, static_cast<float>(tileWidth));
        int targetTileY = TileMath::AnchorTileRow(targetPos.y, static_cast<float>(tileHeight));
        if (tilemap->GetTileCollision(targetTileX, targetTileY))
        {
            // Target blocked - stop and invalidate route to trigger re-initialization.
            EnterStandingStill(idle, facing, anim, false, 0.0f, rng);
            route = PatrolRoute();
            return;
        }

        xf.position = targetPos;

        // Initialize patrol route if needed.
        if (!route.IsValid())
        {
            if (!route.Initialize(patrol.tileX, patrol.tileY, tilemap, 100))
            {
                EnterStandingStill(idle, facing, anim, false, 0.0f, rng);
                return;
            }
            else
            {
                idle.standingStill = false;
                idle.randomStandStillTimer = 0.0f;
                // Wait 5-9.99 seconds before the next random-pause roll. The range
                // prevents NPCs from all pausing in sync.
                idle.randomStandStillCheckTimer =
                    5.0f + std::uniform_int_distribution<int>(0, 499)(rng) / 100.0f;
            }
        }

        // 30% chance to pause at each waypoint when the cooldown expires. This
        // breaks up the mechanical look of constant patrol walking.
        if (route.IsValid() && idle.randomStandStillCheckTimer <= 0.0f)
        {
            idle.randomStandStillCheckTimer =
                5.0f + std::uniform_int_distribution<int>(0, 499)(rng) / 100.0f;
            if (std::uniform_int_distribution<int>(0, 99)(rng) < 30)
            {
                // Pause for 2-4.99 seconds - long enough to look natural, short
                // enough not to stall gameplay.
                float duration = 2.0f + std::uniform_int_distribution<int>(0, 299)(rng) / 100.0f;
                EnterStandingStill(idle, facing, anim, true, duration, rng);
                return;
            }
        }

        // Get next waypoint.
        int nextX, nextY;
        if (route.GetNextWaypoint(nextX, nextY))
        {
            patrol.targetTileX = nextX;
            patrol.targetTileY = nextY;
            UpdateDirectionFromMovement(
                facing, patrol.targetTileX - patrol.tileX, patrol.targetTileY - patrol.tileY);
        }
        else
        {
            idle.waitTimer = 1.0f;
        }
        return;
    }

    if (dist > MIN_MOVEMENT_DIST)
    {
        glm::vec2 dir = toTarget / dist;
        glm::vec2 newPosition = xf.position + dir * speed.value * dt;

        bool wouldCollide = CheckPlayerCollision(newPosition, playerPosition);

        if (!wouldCollide)
        {
            xf.position = newPosition;
            UpdateDirectionFromMovement(facing,
                                        static_cast<int>(dir.x > 0) - static_cast<int>(dir.x < 0),
                                        static_cast<int>(dir.y > 0) - static_cast<int>(dir.y < 0));
        }
        else
        {
            idle.waitTimer = 0.5f;
        }
    }
}

bool ReinitializePatrolRoute(
    NpcIdle& idle, Patrol& patrol, PatrolRoute& route, const Tilemap* tilemap, std::mt19937& rng)
{
    if (!tilemap)
        return false;

    route.Reset();
    bool success = route.Initialize(patrol.tileX, patrol.tileY, tilemap, 100);

    if (success)
    {
        idle.standingStill = false;
        idle.randomStandStillTimer = 0.0f;
        idle.randomStandStillCheckTimer =
            5.0f + std::uniform_int_distribution<int>(0, 499)(rng) / 100.0f;
    }
    else
    {
        idle.standingStill = true;
        idle.randomStandStillTimer = 0.0f;
        idle.lookAroundTimer = 2.0f;
    }

    return success;
}

void UpdateAll(ecs::registry& world,
               const Tilemap& tilemap,
               glm::vec2 playerPosition,
               std::mt19937& rng,
               std::uint64_t frozenNpcId,
               float dt)
{
    world.each<Transform,
               Elevation,
               Facing,
               AnimationState,
               NpcIdle,
               Patrol,
               PatrolRoute,
               Speed,
               Identity,
               NpcTag>(
        [&](Transform& xf,
            Elevation& elev,
            Facing& facing,
            AnimationState& anim,
            NpcIdle& idle,
            Patrol& patrol,
            PatrolRoute& route,
            Speed& speed,
            Identity& id)
        {
            // Freeze the active dialogue speaker (0 = nobody frozen).
            if (frozenNpcId != 0 && id.instanceId == frozenNpcId)
            {
                return;
            }
            const glm::vec2 before = xf.position;
            Update(xf,
                   elev,
                   facing,
                   anim,
                   idle,
                   patrol,
                   route,
                   speed,
                   dt,
                   &tilemap,
                   &playerPosition,
                   rng);
            // Derive the NPC's logical plane from this frame's move (same as the player).
            CharacterKinematics::DerivePlane(elev, before, xf.position, tilemap);
        });
}

void ApplyPlayerOverlapStop(ecs::registry& world, glm::vec2 playerFeet)
{
    // Both use bottom-center anchored 16x16 hitboxes; an NPC is stopped while it
    // overlaps the player (exact, no-epsilon) to prevent visual overlap.
    world.each<Transform, NpcIdle, NpcTag>(
        [&](const Transform& xf, NpcIdle& idle)
        {
            idle.isStopped =
                CollisionGeometry::FeetBoxesOverlap(playerFeet,
                                                    xf.position,
                                                    CharacterConstants::HALF_HITBOX_WIDTH,
                                                    CharacterConstants::HITBOX_HEIGHT,
                                                    /*eps=*/0.0f);
        });
}
}  // namespace NpcAiSystem
