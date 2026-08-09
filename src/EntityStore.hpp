#pragma once

#include "SupportSurface.hpp"

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
 * @brief The NPC lifecycle and query seam over the ECS registry.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * An NPC is a set of granular components in the @c registry tagged with @c NpcTag: Transform,
 * Elevation, Facing, AnimationState, Speed, Identity, NpcSprite, Dialogue, NpcIdle, Patrol and
 * PatrolRoute. Spawn, despawn, clear, and the id and index lookups the console and editor need
 * all route through this namespace, which keeps the create-tuple and the service-from-globals
 * wiring in one place. Per-frame systems skip it and iterate the registry directly with
 * @c world.each<Components...>. The feet readers @ref BuildNpcFeet and
 * @ref BuildNpcCollisionBodies are declared at global scope below the namespace, so call them
 * unqualified.
 *
 * @par Services
 * Spawns resolve their sprite sheet, accent color and dialogue tree from the @ref WorldServices
 * published in @c world.globals(). Each lookup tolerates a null service and falls back to a
 * default, so a bare test world with nothing wired still spawns.
 *
 * @par Identity
 * NPCs carry a stable @ref Identity separate from their entity handle, so dialogue, editor and
 * console references survive despawn and undo. The player has no @ref Identity, because its entity
 * handle is the only reference to it.
 *
 * @see WorldServices, NpcRecord, Identity, PlayerSystem
 */
namespace EntityStore
{
/**
 * @name Lifecycle
 * @brief Create, snapshot, reposition and destroy entities.
 * @{
 */

/**
 * @brief Spawn a full granular NPC entity from a blueprint.
 *
 * Resolves the sprite sheet, accent color and dialogue tree through the services in
 * @c world.globals().get<WorldServices>(), falling back to defaults where a service is missing.
 * Positions the entity at the record's tile, and assigns a fresh @ref Identity unless
 * @c record.instanceId is nonzero, which is how undo keeps the original id.
 *
 * A nonzero @c record.instanceId is trusted, never checked for collision. The caller must have
 * despawned the original NPC first; otherwise two live entities share an id and @ref FindById
 * resolves to whichever the pool iterates first.
 *
 * @warning Resolving the dialogue tree writes: when @c record.hasTree is set, the spawn inserts a
 * fresh copy of the tree into the DialogueStore and keeps the new handle. That store never dedups
 * and never erases, so every snapshot-and-respawn cycle of the same NPC - each editor undo or
 * redo of a placement, each navigation-displacement restore - leaves one more orphaned tree
 * behind.
 *
 * @param world      Registry receiving the new entity.
 * @param record     NPC blueprint: type, tile, facing, name, dialogue and optional id.
 * @param uploadVia  Renderer to upload the sprite sheet through. Null skips the upload.
 * @return           The new entity, or @c ecs::entity{} if the sprite failed to load.
 */
ecs::entity SpawnNpc(ecs::registry& world, const NpcRecord& record, IRenderer* uploadVia = nullptr);

/**
 * @brief Read an NPC entity's components back into a detached blueprint.
 *
 * Used by editor undo and navigation snapshots. Preserves @c instanceId, so the identity survives
 * a later @ref SpawnNpc.
 *
 * @pre @p e is alive and carries the full NPC set, at least Dialogue, Patrol, Identity and Facing.
 * The reads are unchecked: the player entity, a dead handle or a partially assembled NPC aborts in
 * a checked build and dereferences null otherwise. Contrast @ref Remove, which is deliberately
 * tolerant of a dead handle.
 *
 * @param world  Registry owning the entity.
 * @param e      NPC entity to snapshot.
 * @return       A detached @ref NpcRecord blueprint.
 */
NpcRecord SnapshotNpc(const ecs::registry& world, ecs::entity e);

/**
 * @brief Mint the player entity: the full player component set plus the empty @c PlayerTag.
 *
 * Needs no services. @ref PlayerSystem::SwitchCharacter binds the sheets later, reading the
 * TextureStore and AssetRegistry from globals.
 *
 * @param world     Registry receiving the player entity.
 * @param spawnPos  Feet position in world pixels, at the sprite's bottom-center.
 * @return          The player entity.
 */
ecs::entity SpawnPlayer(ecs::registry& world, glm::vec2 spawnPos = glm::vec2(200.0f, 150.0f));

/**
 * @brief Position an NPC's components at a tile.
 *
 * Sets the Patrol cursor and moves the Transform feet to the tile's bottom-center.
 *
 * @param xf             Transform to move.
 * @param patrol         Patrol whose current and target tile cursors are updated.
 * @param route          PatrolRoute, reset unless @p preserveRoute is true.
 * @param tileX          Destination tile column.
 * @param tileY          Destination tile row.
 * @param tileSize       Tile size in pixels, used for the feet math.
 * @param preserveRoute  Whether to keep the existing PatrolRoute instead of resetting it.
 */
void SetNpcTile(Transform& xf,
                Patrol& patrol,
                PatrolRoute& route,
                int tileX,
                int tileY,
                int tileSize,
                bool preserveRoute = false);

/**
 * @brief Destroy entity @p e, doing nothing if it is not alive.
 *
 * Named for its NPC callers, but it deliberately does not check the tag: it destroys whichever
 * live entity it is handed, the player entity included. Contrast @ref Clear, which is scoped to
 * @c NpcTag.
 *
 * @param world  Registry owning the entity.
 * @param e      Entity to destroy.
 */
void Remove(ecs::registry& world, ecs::entity e);

/**
 * @brief Destroy every entity carrying @c NpcTag, leaving the player alone.
 * @param world  Registry to clear NPCs from.
 */
void Clear(ecs::registry& world);

/// @}

/**
 * @name Queries
 * @brief Read-only counts and id or index lookups for console and editor callers.
 * @{
 */

/**
 * @brief Count the live NPC entities.
 * @param world  Registry to count in.
 * @return       Number of entities carrying @c NpcTag.
 */
std::size_t Count(ecs::registry& world);

/**
 * @brief Collect every NPC entity handle, for callers that address NPCs by index.
 *
 * Console commands and editor selection address NPCs by position in this list.
 *
 * @warning The order is deterministic for a given registry state, but it is not stable across
 * lifecycle changes. The component pool removes by swap-and-pop, so destroying any NPC moves the
 * last one into the freed slot: every index above the removed one shifts, and the freed slot now
 * names a different NPC. Treat an index as valid only until the next spawn or despawn. Anything
 * that must survive one holds an @c Identity::instanceId and re-resolves it through
 * @ref FindById.
 *
 * @param world  Registry to enumerate.
 * @return       NPC entity handles in registry dense order.
 */
std::vector<ecs::entity> Entities(ecs::registry& world);

/**
 * @brief Find an NPC entity by instance id. Overloaded for mutable and const registries.
 *
 * @param world       Registry to scan.
 * @param instanceId  Stable NPC instance id to match. Id 0 never matches.
 * @return            The matching entity, or @c ecs::entity{} if there is none.
 */
ecs::entity FindById(ecs::registry& world, std::uint64_t instanceId);
ecs::entity FindById(const ecs::registry& world, std::uint64_t instanceId);

/// @}
}  // namespace EntityStore

/**
 * @brief Fill @p out with each NPC's feet position, at the sprite's bottom-center.
 *
 * @p out is cleared and reused, so a caller keeping one vector across frames pays no allocation
 * after the first fill.
 *
 * @warning No production caller. Player collision consumes @ref BuildNpcCollisionBodies instead,
 * which carries each NPC's committed support alongside its feet. Only tests exercise this
 * plain-feet form.
 *
 * @param world  Registry to read NPC transforms from.
 * @param out    Output vector, cleared and then filled with feet positions.
 */
void BuildNpcFeet(ecs::registry& world, std::vector<glm::vec2>& out);

/**
 * @brief Fill @p out with each NPC's feet position and committed support.
 *
 * This is the form player collision consumes; see @c Game::ProcessPlayerMovement. Carrying the
 * support is what stops an NPC standing on a bridge from blocking a character traveling through
 * the implicit ground, or a different elevation height, below it. As with @ref BuildNpcFeet,
 * @p out is cleared and reused.
 *
 * @param world  Registry to read NPC transforms and support from.
 * @param out    Output vector, cleared and then filled with collision bodies.
 */
void BuildNpcCollisionBodies(ecs::registry& world, std::vector<CharacterCollisionBody>& out);
