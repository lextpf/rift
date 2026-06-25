#pragma once

#include "CharacterDirection.hpp"
#include "DialogueTypes.hpp"

#include <cstdint>
#include <string>

/**
 * @struct NpcRecord
 * @brief A detached NPC blueprint: the authored data needed to (re)spawn an NPC
 *        as a set of ECS components, carrying no live registry / GPU state.
 * @ingroup Entities
 *
 * The seam for code that must hold an NPC outside the registry: editor undo
 * (@c PlaceNPCCmd / @c RemoveNPCCmd hold one across despawn), the navigation
 * displacement snapshot, and map load. @ref EntityStore::SpawnNpc turns a record
 * into a full granular entity (Transform / Facing / ... / NpcTag), resolving the
 * sheet + dialogue tree through the services in @c registry.globals();
 * @ref EntityStore::SnapshotNpc reverses it. The runtime-regenerated
 * @c PatrolRoute is deliberately NOT stored - it rebuilds from the tile on the
 * NPC's first AI update. A nonzero @ref instanceId is preserved so a dialogue /
 * console reference to this NPC survives place -> undo -> redo.
 */
struct NpcRecord
{
    std::string type;      ///< Sprite-path lookup key (becomes Dialogue.type).
    std::string name;      ///< Display name (becomes Dialogue.name).
    std::string text;      ///< Simple dialogue line (becomes Dialogue.text; default if empty).
    int tileX = 0;         ///< Spawn tile column.
    int tileY = 0;         ///< Spawn tile row.
    int tileSize = 16;     ///< Tile size in pixels for feet placement.
    DialogueTree tree;     ///< Full branching tree value (re-Add'd to the store on spawn).
    bool hasTree = false;  ///< Whether @ref tree carries a tree to register.
    std::uint64_t instanceId = 0;  ///< 0 = assign a fresh Identity; nonzero = preserve it.
    CharacterDirection facing = CharacterDirection::DOWN;  ///< Captured facing direction.
};
