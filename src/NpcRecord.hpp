#pragma once

#include "CharacterDirection.hpp"
#include "DialogueTypes.hpp"

#include <cstdint>
#include <string>

/**
 * @struct NpcRecord
 * @brief A detached NPC blueprint: the authored data needed to spawn an NPC as a set of ECS
 *        components, holding no live registry or GPU state.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * This is the seam for code that must hold an NPC outside the registry. Editor undo keeps one
 * across a despawn in @c PlaceNPCCmd and @c RemoveNPCCmd, and the navigation displacement
 * snapshot and map load both use it.
 *
 * @ref EntityStore::SpawnNpc turns a record into a full entity, resolving the sheet and dialogue
 * tree through the services in @c registry.globals(). @ref EntityStore::SnapshotNpc reverses it.
 *
 * @c PatrolRoute is deliberately absent, because it rebuilds from the NPC's tile on its first AI
 * update. A nonzero @ref instanceId is preserved so a dialogue or console reference to this NPC
 * survives a place, undo and redo cycle.
 */
struct NpcRecord
{
    std::string type;  ///< Sprite-path lookup key; becomes Dialogue::type.
    std::string name;  ///< Display name; becomes Dialogue::name.
    std::string text;  ///< Simple dialogue line; becomes Dialogue::text, or a default if empty.
    int tileX = 0;     ///< Spawn tile column.
    int tileY = 0;     ///< Spawn tile row.
    /**
     * @brief Tile size in pixels, used to turn @ref tileX and @ref tileY back into a feet position
     *        on spawn.
     *
     * @c EntityStore::SnapshotNpc hardcodes 16 instead of reading the project manifest, so on a
     * project with a different tile size a snapshot and respawn round trip misplaces the NPC.
     */
    int tileSize = 16;
    DialogueTree tree;             ///< Full branching tree, registered with the store on spawn.
    bool hasTree = false;          ///< Whether @ref tree carries a tree to register.
    std::uint64_t instanceId = 0;  ///< 0 assigns a fresh Identity; nonzero preserves this one.
    CharacterDirection facing = CharacterDirection::DOWN;  ///< Captured facing direction.
};
