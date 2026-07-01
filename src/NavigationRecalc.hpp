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
 * Extracted from Editor::RecalculateNPCPatrolRoutes so the editor command
 * layer can call them without coupling to Editor. The NPC erase step is the
 * destructive side-effect of toggling a tile to non-walkable (the NPC there
 * has no valid patrol home anymore); commands that mutate navigation must
 * snapshot the erased NPCs to make undo lossless.
 *
 * @par Undo-Safe Mutation Flow
 * Editor commands that flip navigation flags follow a three-step pattern so
 * Revert() can restore both the flag state AND the displaced NPCs:
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
 * RebuildPatrolRoutes is safe to call any number of times; it skips NPCs
 * without a valid route and recomputes the rest in place. The snapshot/
 * restore pair is symmetric: calling Snapshot on an already-clean state
 * returns an empty vector, and Restore on an empty snapshot is a no-op.
 *
 * @see UndoRedoStack, EditorCommand
 */

/**
 * @brief Move out NPCs whose tile is currently non-walkable; returns them.
 * The tilemap is read-only here; the caller is expected to have already
 * applied the navigation flips.
 */
std::vector<NpcRecord> SnapshotAndEraseNPCsOnNonWalkable(const Tilemap& tilemap,
                                                         ecs::registry& npcs);

/// @brief Push the snapshotted NPCs back onto npcs and clear the snapshot.
void RestoreErasedNPCs(ecs::registry& npcs, std::vector<NpcRecord>& snapshot);

/**
 * @brief Reinitialize each NPC's patrol route against the current tilemap.
 * Idempotent: callable any number of times. Skips NPCs without a valid route.
 */
void RebuildPatrolRoutes(Tilemap& tilemap, ecs::registry& npcs);
