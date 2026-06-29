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

/**
 * @brief The NPC lifecycle + query seam over the ECS registry.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * NPCs live in the ECS @c registry as a set of granular components tagged with
 * @c NpcTag (Transform, Elevation, Facing, AnimationState, Speed, Identity,
 * NpcSprite, Dialogue, NpcIdle, Patrol, PatrolRoute). There is no NPC class:
 * spawn/despawn/clear, the index/id lookups the console + editor need, and the
 * feet read all route through @ref EntityStore so the create-tuple and the
 * service-from-globals wiring live in one place. Per-frame systems iterate the
 * registry directly via @c world.each<Components...>.
 *
 * @par Services
 * Spawns resolve their sprite sheet, accent color, and dialogue tree from the
 * @ref WorldServices published in @c world.globals() rather than per-entity
 * back-pointers. The lookup is null-tolerant so tests can run without services
 * wired - missing services fall back exactly as the old per-entity pointers did.
 *
 * @par Identity
 * NPCs carry a stable @ref Identity (instance id) distinct from their entity
 * handle, so dialogue / editor / console references survive despawn and undo.
 * The player has no @ref Identity: it is referenced solely by its entity handle.
 *
 * @see WorldServices, NpcRecord, Identity, PlayerSystem
 */

namespace EntityStore
{
/**
 * @name Lifecycle
 * @brief Create, snapshot, reposition, and destroy entities.
 * @{
 */

/**
 * @brief Spawn a full granular NPC entity from a blueprint.
 *
 * Resolves the sprite sheet (+ accent) and dialogue tree through the services in
 * @c world.globals().get<WorldServices>() (tolerates missing services - falls
 * back like the old per-entity pointers did). Positions at the record's tile and
 * assigns a fresh @ref Identity unless @c record.instanceId is nonzero (so undo
 * keeps the id).
 *
 * @param world     Registry receiving the new entity.
 * @param record    NPC blueprint (type, tile, facing, name, dialogue, optional id).
 * @param uploadVia Optional renderer to upload the sprite sheet through (the
 *                  renderer spawn path); null skips the upload.
 * @return The new entity, or @c ecs::entity{} on sprite-load failure.
 */
ecs::entity SpawnNpc(ecs::registry& world, const NpcRecord& record, IRenderer* uploadVia = nullptr);

/**
 * @brief Read an NPC entity's components back into a detached blueprint (for
 * editor undo / navigation snapshots).
 *
 * Preserves @c instanceId so the identity survives a later @ref SpawnNpc.
 *
 * @param world Registry owning the entity.
 * @param e     NPC entity to snapshot.
 * @return A detached @ref NpcRecord blueprint.
 */
NpcRecord SnapshotNpc(const ecs::registry& world, ecs::entity e);

/**
 * @brief Mint the player entity: the full granular player component set plus the
 * empty @c PlayerTag, at @p spawnPos (feet, bottom-center).
 *
 * Needs no services - sheets are bound later by @ref PlayerSystem::SwitchCharacter,
 * which reads the TextureStore / AssetRegistry from globals.
 *
 * @param world    Registry receiving the player entity.
 * @param spawnPos Feet (bottom-center) spawn position in world pixels.
 * @return The player entity.
 */
ecs::entity SpawnPlayer(ecs::registry& world, glm::vec2 spawnPos = glm::vec2(200.0f, 150.0f));

/**
 * @brief Position an NPC's components at a tile (the former @c SetTilePosition).
 *
 * Sets the Patrol cursor + Transform feet (bottom-center of the tile) and resets
 * the PatrolRoute unless @p preserveRoute.
 *
 * @param xf            Transform to move (feet set to the tile's bottom-center).
 * @param patrol        Patrol whose tile + target cursor are updated.
 * @param route         PatrolRoute reset unless @p preserveRoute is true.
 * @param tileX         Destination tile column.
 * @param tileY         Destination tile row.
 * @param tileSize      Tile size in pixels (feet math).
 * @param preserveRoute Keep the existing PatrolRoute instead of resetting it.
 */
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

/// @}

/**
 * @name Queries
 * @brief Read-only counts and id / index lookups for console + editor callers.
 * @{
 */

/// @brief Number of live NPC entities.
std::size_t Count(ecs::registry& world);

/**
 * @brief Collect every NPC entity handle (registry dense order) for
 * index-addressed callers: console commands, editor selection.
 *
 * @param world Registry to enumerate.
 * @return NPC entity handles in registry dense order.
 */
std::vector<ecs::entity> Entities(ecs::registry& world);

/**
 * @brief Find an NPC entity by instance id (the registry analog of the old
 * @c m_NPCs id scan). Overloaded for mutable and const registries.
 *
 * @param world      Registry to scan.
 * @param instanceId Stable NPC instance id to match (id 0 never matches).
 * @return The matching entity, or @c ecs::entity{} if none.
 */
ecs::entity FindById(ecs::registry& world, std::uint64_t instanceId);
ecs::entity FindById(const ecs::registry& world, std::uint64_t instanceId);

/// @}
}  // namespace EntityStore

/**
 * @brief Fill @p out with each NPC's feet (bottom-center) position.
 *
 * @p out is cleared and reused (no per-frame allocation); consumed by player
 * collision.
 *
 * @param world Registry to read NPC transforms from.
 * @param out   Output vector, cleared then filled with feet positions.
 */
void BuildNpcFeet(ecs::registry& world, std::vector<glm::vec2>& out);
