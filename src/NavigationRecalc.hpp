#pragma once

#include <ecs.hpp>

#include <vector>

class Tilemap;
struct NpcRecord;

/**
 * @brief Free-function helpers for navigation map / NPC patrol synchronization.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Free functions rather than Editor methods, so the editor command layer can call them without
 * coupling to Editor. Toggling a tile to non-walkable destroys any NPC standing there, because it
 * no longer has a valid patrol home. Commands that mutate navigation must snapshot those erased
 * NPCs to keep undo lossless.
 *
 * @par Undo-safe mutation flow
 * Editor commands that flip navigation flags follow a three-step pattern so
 * Revert() can restore both the flag state and the displaced NPCs:
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 *     classDef apply fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *     classDef snap  fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 *     classDef rebuild fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *
 *     subgraph Apply
 *         direction LR
 *         A1[Flip nav flags]:::apply --> A2[SnapshotAndEraseNPCsOnNonWalkable]:::snap
 *         A2 --> A3[RebuildPatrolRoutes]:::rebuild
 *         A3 --> A4[Cmd keeps snapshot]
 *     end
 *
 *     subgraph Revert
 *         direction LR
 *         R1[Revert nav flags]:::apply --> R2[RestoreErasedNPCs]:::snap
 *         R2 --> R3[RebuildPatrolRoutes]:::rebuild
 *     end
 *
 *     Apply -. undo .-> Revert
 * </pre>
 * @endhtmlonly
 *
 * @par Usage
 * @code{.cpp}
 * // Apply: command has already written new navigation flags.
 * auto displaced = SnapshotAndEraseNPCsOnNonWalkable(tilemap, npcs);
 * RebuildPatrolRoutes(tilemap, npcs);
 * cmd.storedDisplaced = std::move(displaced);
 *
 * // Revert: command has already restored the old navigation flags.
 * RestoreErasedNPCs(npcs, cmd.storedDisplaced);
 * RebuildPatrolRoutes(tilemap, npcs);
 * @endcode
 *
 * @par Idempotency
 * RebuildPatrolRoutes is safe to call any number of times: it discards and regenerates
 * every route unconditionally, so repeat calls converge on the same state. Nothing is
 * filtered or skipped - an NPC whose route cannot be rebuilt is left standing and looking
 * around, with a warning logged, rather than being passed over. The snapshot/restore pair
 * is symmetric: calling Snapshot on an already-clean state returns an empty vector, and
 * Restore on an empty snapshot is a no-op.
 *
 * @see UndoRedoStack, EditorCommand
 */

/**
 * @brief Move out NPCs whose tile is currently non-walkable; returns them.
 * @ingroup Editor
 *
 * The tilemap is read-only here; the caller is expected to have already
 * applied the navigation flips. Erasure is deferred until after the scan, because
 * destroying entities while iterating their own pool is a fault.
 *
 * @param tilemap Navigation source, already updated by the caller.
 * @param npcs    Registry to erase from; matching NPC entities are destroyed.
 * @return Full records for the erased NPCs, in erase order. Hand this to
 *         @ref RestoreErasedNPCs to make the command's undo lossless; dropping it loses
 *         those NPCs permanently.
 */
std::vector<NpcRecord> SnapshotAndEraseNPCsOnNonWalkable(const Tilemap& tilemap,
                                                         ecs::registry& npcs);

/**
 * @brief Push the snapshotted NPCs back onto npcs and clear the snapshot.
 * @ingroup Editor
 *
 * Respawned NPCs keep their original @c Identity.instanceId, so a dialogue reference held
 * across the undo still resolves to the same NPC.
 *
 * @param npcs     Registry to respawn into.
 * @param snapshot Records from @ref SnapshotAndEraseNPCsOnNonWalkable; emptied on return,
 *                 which makes a second Restore a harmless no-op.
 */
void RestoreErasedNPCs(ecs::registry& npcs, std::vector<NpcRecord>& snapshot);

/**
 * @brief Reinitialize each NPC's patrol route against the current tilemap.
 * @ingroup Editor
 *
 * Every NPC carrying the patrol component set is rebuilt - there is no "already valid"
 * fast path and nothing is skipped. A rebuild that finds no route leaves that NPC standing
 * still and looking around, and logs a warning; the call still succeeds.
 *
 * The shared RNG is taken from @c WorldServices in the registry's globals (the editor
 * command layer has no Game to pass one from); a caller without published services, such
 * as a test world, silently gets a local default-seeded engine instead.
 *
 * @param tilemap Navigation source the routes are regenerated against.
 * @param npcs    Registry whose NPC @c PatrolRoute and @c NpcIdle components are rewritten.
 */
void RebuildPatrolRoutes(Tilemap& tilemap, ecs::registry& npcs);
