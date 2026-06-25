#pragma once

/**
 * @struct NpcTag
 * @brief Empty tag component marking an entity as an NPC.
 * @ingroup Entities
 *
 * Stored as a zero-byte tag (the ECS auto-selects tag storage for empty
 * structs). Lets systems select NPC entities via `view<NpcTag, ...>` /
 * `each<...>` independently of which optional components are present.
 */
struct NpcTag
{
};
