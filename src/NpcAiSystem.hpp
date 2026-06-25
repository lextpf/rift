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
 * @file NpcAiSystem.hpp
 * @brief Stateless NPC patrol/idle AI over component bundles.
 * @ingroup Entities
 *
 * The free-function "system" form of NonPlayerCharacter's AI methods (Update +
 * ReinitializePatrolRoute and their idle/look-around/facing helpers). Each
 * operates only on the component structs passed by reference -- no NPC instance,
 * no hidden state -- so the same call works whether driven from an ECS view/each
 * loop or a unit test. Randomness is an explicit @c std::mt19937& parameter
 * (the world's engine, owned by Game and published in @c globals() via
 * @ref WorldServices::npcRng); passing a seeded engine makes the idle FSM
 * deterministically testable. The @ref PatrolRoute is a regenerable runtime
 * cache, threaded in by reference rather than stored as a component.
 */
namespace NpcAiSystem
{
/// @brief Advance one frame of NPC patrol + idle AI: walk toward the current
/// waypoint, advance the route, and run the random pause / look-around FSM.
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

/// @brief Rebuild the patrol route from the NPC's current tile; updates idle
/// state to reflect success (resume patrol) or failure (stand + look around).
/// @p rng seeds the post-success random pause cooldown.
bool ReinitializePatrolRoute(
    NpcIdle& idle, Patrol& patrol, PatrolRoute& route, const Tilemap* tilemap, std::mt19937& rng);

/// @brief Drive @ref Update + plane-derive over every NPC entity (the orchestration
/// formerly inline in Game::Update). Iterates the NPC component set via @c each<>,
/// skips the NPC whose @c Identity.instanceId equals @p frozenNpcId (0 = none, e.g.
/// the active dialogue speaker), runs the per-NPC AI step, and derives each NPC's
/// logical plane from its pre/post move delta. Symmetric with @ref PlayerSystem::Update.
void UpdateAll(ecs::registry& world,
               const Tilemap& tilemap,
               glm::vec2 playerPosition,
               std::mt19937& rng,
               std::uint64_t frozenNpcId,
               float dt);

/// @brief Stop every NPC overlapping the player's feet box (sets @c NpcIdle.isStopped),
/// preventing visual overlap. Runs after @ref UpdateAll so it sees final positions.
void ApplyPlayerOverlapStop(ecs::registry& world, glm::vec2 playerFeet);
}  // namespace NpcAiSystem
