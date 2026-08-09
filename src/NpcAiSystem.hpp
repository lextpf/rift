#pragma once

#include "SupportSurface.hpp"

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
 * Patrol stepping, route reinitialization, and the idle, look-around and facing helpers, all as
 * free functions. Each operates only on the component structs passed by reference, with no NPC
 * instance and no hidden state, so the same call works from an ECS each loop or from a unit test.
 *
 * Randomness arrives as an explicit @c std::mt19937& parameter. Game owns the world's engine and
 * publishes it through @ref WorldServices::npcRng, and passing a seeded engine makes the idle
 * state machine deterministically testable.
 *
 * @ref PatrolRoute is an ECS component, created with the NPC in EntityStore::SpawnNpc and
 * iterated by @ref UpdateAll, but it is a regenerable runtime cache rather than authored state.
 * Only the small @ref Patrol tile cursor persists. Editor navigation edits invalidate the route
 * and NavigationRecalc::RebuildPatrolRoutes regenerates it in place, so never hold a reference or
 * a copy of one across frames.
 *
 * @par Idle state machine
 * @ref Update is a chain of early returns over the @ref NpcIdle flags and timers. Each state
 * below names the field tuple that selects that branch:
 *
 * @htmlonly
 * <pre class="mermaid">
 * stateDiagram-v2
 *     state "Patrolling" as Pat
 *     state "Stopped (isStopped)" as Stop
 *     state "Waiting (waitTimer &gt; 0)" as Wait
 *     state "RandomPause (standingStill, randomStandStillTimer &gt; 0)" as Pause
 *     state "Stalled (standingStill, randomStandStillTimer == 0)" as Stall
 *
 *     [*] --> Pat
 *     Pat --> Stop: ApplyPlayerOverlapStop sees an overlap
 *     Stop --> Pat: overlap ends (reassigned every frame)
 *     Pat --> Wait: player overlap, blocked step, or route exhausted
 *     Wait --> Pat: timer reaches 0
 *     Pat --> Pause: 30% roll at a waypoint, 2 to 5 s
 *     Pause --> Pat: timer reaches 0
 *     Pat --> Stall: waypoint blocked, or Initialize failed
 *     Stall --> Pat: ReinitializePatrolRoute succeeds
 * </pre>
 * @endhtmlonly
 *
 * Stalled is the only state with no exit inside @ref Update: it looks around forever until
 * NavigationRecalc::RebuildPatrolRoutes drives a successful @ref ReinitializePatrolRoute.
 * Stopped and Waiting are independent of the standing-still pair, and Stopped wins, because it
 * is tested first.
 *
 * @par Route validity is stricter than pathfinding
 * A route tile must be navigable and free of collision (PatrolRoute::IsValidTile), whereas
 * @ref Pathfinding checks navigation only. A tile carrying both flags is therefore
 * reachable by @c nav.path but unusable as a patrol waypoint, so a console-reported path
 * can describe a route no NPC will ever walk.
 *
 * @see PatrolRoute, Pathfinding, NavigationRecalc
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
 * @note Two failures park the NPC in an indefinite stand-still and look-around state with no
 * timer: a waypoint that has become blocked, and a route that will not rebuild. Nothing inside
 * this function leaves that state. Only a successful @ref ReinitializePatrolRoute clears it, which
 * the editor's navigation rebuild drives.
 *
 * @param xf             NPC transform; @c position (feet) is integrated toward the waypoint.
 * @param elev           Committed support plus its smoothed visual elevation.
 * @param facing         Facing direction; set from the movement delta or idle look-around.
 * @param anim           Walk-cycle animation state; advanced while moving, reset while idle.
 * @param idle           Idle state machine: standing-still, pause and look-around timers, and
 *                       the stop and wait flags.
 * @param patrol         Patrol tile state (current tile + target waypoint tile).
 * @param route          Regenerable patrol-route cache; supplies waypoints. Built on the
 *                       first waypoint arrival while invalid, and discarded (reset to an
 *                       invalid route) when the current target tile turns out blocked.
 * @param speed          Movement speed in px/s (@c speed.value).
 * @param dt             Frame time in seconds.
 * @param tilemap        World tilemap for tile size and walkability queries; null makes
 *                       this a no-op.
 * @param playerBody     Player feet/support for exact-support overlap avoidance, or null.
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
            const CharacterCollisionBody* playerBody,
            std::mt19937& rng);

/**
 * @brief Rebuild the patrol route from the NPC's current tile; updates idle
 *        state to reflect success (resume patrol) or failure (stand + look around).
 *
 * @param idle    Idle state; cleared to resume patrol on success, or set to
 *                stand-still + look-around on failure.
 * @param patrol  Patrol tile state; the route is initialized from @c patrol.tileX / @c tileY.
 *                Read-only here - the cursor is not corrected if it has drifted.
 * @param route   Route cache; reset and re-initialized in place. A failed rebuild leaves it
 *                reset, i.e. invalid.
 * @param tilemap World tilemap; a null tilemap returns false immediately, without touching
 *                @p idle or @p route.
 * @param rng     Seeds the post-success random-pause cooldown.
 * @return true if a new route was built (patrol resumes); false otherwise.
 */
bool ReinitializePatrolRoute(
    NpcIdle& idle, Patrol& patrol, PatrolRoute& route, const Tilemap* tilemap, std::mt19937& rng);

/**
 * @brief Drive support-aware @ref Update over every NPC entity.
 *
 * Iterates with @c each<> over Transform, Elevation, Facing, AnimationState, NpcIdle, Patrol,
 * PatrolRoute, Speed, Identity and NpcTag. The filter is exact and silent: an entity missing any
 * one of the ten never moves and nothing is logged, so a hand-assembled test NPC must carry the
 * full set. Each accepted movement commits its position and support surface together, matching how
 * player movement commits.
 *
 * @param world          ECS registry; the NPC component set is iterated in place.
 * @param tilemap        World tilemap, forwarded to each NPC's @ref Update; the support it
 *                       resolves is committed there, not here.
 * @param playerBody     Player feet/support, forwarded for per-NPC overlap avoidance.
 * @param rng            Shared random engine for every NPC's idle FSM.
 * @param frozenNpcId    @c Identity::instanceId to skip; 0 skips nobody. The active dialogue
 *                       speaker is the usual caller.
 * @param dt             Frame time in seconds.
 */
void UpdateAll(ecs::registry& world,
               const Tilemap& tilemap,
               CharacterCollisionBody playerBody,
               std::mt19937& rng,
               std::uint64_t frozenNpcId,
               float dt);

/**
 * @brief Stop every NPC overlapping the player's feet box (sets @c NpcIdle.isStopped),
 *        preventing visual overlap.
 *
 * Runs after @ref UpdateAll so it sees final positions. The overlap test is exact, with no
 * epsilon, against bottom-center-anchored feet boxes. It counts only when the NPC's committed
 * support matches the player's, so an NPC on a bridge deck does not stop for a player below.
 *
 * @warning This assigns @c isStopped for every NPC on every frame rather than combining with it.
 * Any other hold on that flag, from the dialogue path or the console @c npc.freeze command, is
 * therefore overwritten on the next frame. The dialogue freeze survives only because
 * @ref UpdateAll skips the speaker by @c Identity::instanceId rather than by this flag.
 *
 * @param world      ECS registry; every NPC is tested and its @c NpcIdle.isStopped updated.
 * @param playerBody Player feet/support record to test NPCs against.
 */
void ApplyPlayerOverlapStop(ecs::registry& world, CharacterCollisionBody playerBody);
}  // namespace NpcAiSystem
