#pragma once

#include <vector>

class Tilemap;
class NonPlayerCharacter;

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
 */

/// @brief Move out NPCs whose tile is currently non-walkable; returns them.
/// The tilemap is read-only here; the caller is expected to have already
/// applied the navigation flips.
std::vector<NonPlayerCharacter> SnapshotAndEraseNPCsOnNonWalkable(
    const Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs);

/// @brief Push the snapshotted NPCs back onto npcs and clear the snapshot.
void RestoreErasedNPCs(std::vector<NonPlayerCharacter>& npcs,
                       std::vector<NonPlayerCharacter>& snapshot);

/// @brief Reinitialize each NPC's patrol route against the current tilemap.
/// Idempotent: callable any number of times. Skips NPCs without a valid route.
void RebuildPatrolRoutes(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs);
