#pragma once

#include <ecs.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

class IRenderer;
class PatrolRoute;
struct NpcRecord;
struct Patrol;
struct Transform;

/// @file EntityStore.hpp
/// @brief The NPC lifecycle + query seam over the ECS registry.
/// @ingroup World
///
/// NPCs live in the ECS @c registry as a set of granular components tagged with
/// @c NpcTag (Transform, Elevation, Facing, AnimationState, Speed, Identity,
/// NpcSprite, Dialogue, NpcIdle, Patrol, PatrolRoute). There is no NPC class:
/// spawn/despawn/clear, the index/id lookups the console + editor need, and the
/// feet read all route through @ref EntityStore so the create-tuple and the
/// service-from-globals wiring live in one place. Per-frame systems iterate the
/// registry directly via @c world.each<Components...>.

namespace EntityStore
{
/// @brief Spawn a full granular NPC entity from a blueprint. Resolves the sprite
/// sheet (+ accent) and dialogue tree through the services in
/// @c world.globals().get<WorldServices>() (tolerates missing services - falls
/// back like the old per-entity pointers did). Positions at the record's tile,
/// assigns a fresh Identity unless @c record.instanceId is nonzero (undo keeps
/// the id). Optionally uploads the sheet via @p uploadVia (renderer spawn path).
/// @return the new entity, or @c ecs::entity{} on sprite-load failure.
ecs::entity SpawnNpc(ecs::registry& world, const NpcRecord& record, IRenderer* uploadVia = nullptr);

/// @brief Read an NPC entity's components back into a detached blueprint (for
/// editor undo / navigation snapshots). Preserves @c instanceId so the identity
/// survives a later @ref SpawnNpc.
NpcRecord SnapshotNpc(const ecs::registry& world, ecs::entity e);

/// @brief Mint the player entity: the full granular player component set + the
/// empty @c PlayerTag, at @p spawnPos (feet, bottom-center). Needs no services
/// (sheets are bound later by @ref PlayerSystem::SwitchCharacter, which reads the
/// TextureStore/AssetRegistry from globals). @return the player entity.
ecs::entity SpawnPlayer(ecs::registry& world, glm::vec2 spawnPos = glm::vec2(200.0f, 150.0f));

/// @brief Position an NPC's components at a tile (the former SetTilePosition):
/// sets the Patrol cursor + Transform feet (bottom-center of the tile) and resets
/// the PatrolRoute unless @p preserveRoute.
void SetNpcTile(Transform& xf,
                Patrol& patrol,
                PatrolRoute& route,
                int tileX,
                int tileY,
                int tileSize,
                bool preserveRoute = false);

/// @brief Destroy NPC entity @p e (no-op if not alive).
void Remove(ecs::registry& world, ecs::entity e);

/// @brief Destroy every NPC entity.
void Clear(ecs::registry& world);

/// @brief Number of live NPC entities.
std::size_t Count(ecs::registry& world);

/// @brief Collect every NPC entity handle (registry dense order) for
/// index-addressed callers: console commands, editor selection.
std::vector<ecs::entity> Entities(ecs::registry& world);

/// @brief Find an NPC entity by instance id; @c ecs::entity{} if none (id 0
/// included). The registry analog of the old m_NPCs id scan.
ecs::entity FindById(ecs::registry& world, std::uint64_t instanceId);
ecs::entity FindById(const ecs::registry& world, std::uint64_t instanceId);
}  // namespace EntityStore

/// @brief Fill @p out with each NPC's feet (bottom-center) position. @p out is
/// cleared and reused (no per-frame allocation); consumed by player collision.
void BuildNpcFeet(ecs::registry& world, std::vector<glm::vec2>& out);
