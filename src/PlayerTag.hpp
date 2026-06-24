#pragma once

/**
 * @struct PlayerTag
 * @brief Empty tag component marking the entity as the player.
 * @ingroup Entities
 *
 * Stored as a zero-byte tag (the ECS auto-selects tag storage for empty
 * structs). Lets systems distinguish the player entity from NPCs in shared
 * `view`/`each` queries over the common character components.
 */
struct PlayerTag
{
};
