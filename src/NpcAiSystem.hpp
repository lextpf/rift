#pragma once

#include <ecs.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <random>

struct Transform;
struct Elevation;
struct Facing;
struct AnimationState;
struct NpcIdle;
struct Patrol;
struct Speed;
class PatrolRoute;
class Tilemap;

/**
 * @brief Stateless NPC patrol/idle AI over component bundles.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The free-function "system" form of NonPlayerCharacter's AI methods (Update +
 * ReinitializePatrolRoute and their idle/look-around/facing helpers). Each
 * operates only on the component structs passed by reference - no NPC instance,
 * no hidden state - so the same call works whether driven from an ECS view/each
 * loop or a unit test. Randomness is an explicit @c std::mt19937& parameter
 * (the world's engine, owned by Game and published in @c globals() via
 * @ref WorldServices::npcRng); passing a seeded engine makes the idle FSM
 * deterministically testable. The @ref PatrolRoute is a regenerable runtime
 * cache, threaded in by reference rather than stored as a component.
 */
namespace NpcAiSystem
{
/**
 * @brief Advance one frame of NPC patrol + idle AI: walk toward the current
 *        waypoint, advance the route, and run the random pause / look-around FSM.
 *
 * No-op when @p tilemap is null. Elevation is smoothed every frame regardless of
 * movement state; the NPC pauses briefly when its feet box overlaps the player.
 *
 * @param xf             NPC transform; @c position (feet) is integrated toward the waypoint.
 * @param elev           Elevation state; the plane transition is smoothed each frame.
 * @param facing         Facing direction; set from the movement delta or idle look-around.
 * @param anim           Walk-cycle animation state; advanced while moving, reset while idle.
 * @param idle           Idle FSM state (standing-still, pause/look-around timers, stop/wait flags).
 * @param patrol         Patrol tile state (current tile + target waypoint tile).
 * @param route          Regenerable patrol-route cache; supplies waypoints and is
 *                       re-initialized when invalid or when the target tile is blocked.
 * @param speed          Movement speed in px/s (@c speed.value).
 * @param dt             Frame time in seconds.
 * @param tilemap        World tilemap for tile size and walkability queries; null makes
 *                       this a no-op.
 * @param playerPosition Player feet position for overlap avoidance, or null when absent.
 * @param rng            Random engine driving the idle pause / look-around FSM.
 */
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
            std::mt19937& rng);

/**
 * @brief Rebuild the patrol route from the NPC's current tile; updates idle
 *        state to reflect success (resume patrol) or failure (stand + look around).
 *
 * @param idle    Idle state; cleared to resume patrol on success, or set to
 *                stand-still + look-around on failure.
 * @param patrol  Patrol tile state; the route is initialized from @c patrol.tileX / @c tileY.
 * @param route   Route cache; reset and re-initialized in place.
 * @param tilemap World tilemap; a null tilemap fails fast and returns false.
 * @param rng     Seeds the post-success random-pause cooldown.
 * @return true if a new route was built (patrol resumes); false otherwise (NPC stands
 *         still and looks around; also returned when @p tilemap is null).
 */
bool ReinitializePatrolRoute(
    NpcIdle& idle, Patrol& patrol, PatrolRoute& route, const Tilemap* tilemap, std::mt19937& rng);

/**
 * @brief Drive @ref Update + plane-derive over every NPC entity (the orchestration
 *        formerly inline in Game::Update).
 *
 * Iterates the NPC component set via @c each<>, runs the per-NPC AI step, and derives
 * each NPC's logical plane from its pre/post move delta. Symmetric with
 * @ref PlayerSystem::Update.
 *
 * @param world          ECS registry; the NPC component set is iterated in place.
 * @param tilemap        World tilemap, forwarded to each NPC's @ref Update and plane-derive.
 * @param playerPosition Player feet position, forwarded for per-NPC overlap avoidance.
 * @param rng            Shared random engine for every NPC's idle FSM.
 * @param frozenNpcId    @c Identity.instanceId to skip (0 = none, e.g. the active
 *                       dialogue speaker).
 * @param dt             Frame time in seconds.
 */
void UpdateAll(ecs::registry& world,
               const Tilemap& tilemap,
               glm::vec2 playerPosition,
               std::mt19937& rng,
               std::uint64_t frozenNpcId,
               float dt);

/**
 * @brief Stop every NPC overlapping the player's feet box (sets @c NpcIdle.isStopped),
 *        preventing visual overlap.
 *
 * Runs after @ref UpdateAll so it sees final positions. Overlap is exact (no epsilon)
 * against bottom-center-anchored feet boxes.
 *
 * @param world      ECS registry; every NPC is tested and its @c NpcIdle.isStopped updated.
 * @param playerFeet Player feet position (bottom-center anchor) to test NPCs against.
 */
void ApplyPlayerOverlapStop(ecs::registry& world, glm::vec2 playerFeet);
}  // namespace NpcAiSystem
