#pragma once

/**
 * @struct NpcTag
 * @brief Empty tag component marking an entity as an NPC.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Stored as a zero-byte tag, because the ECS selects tag storage for empty structs. It lets
 * systems select NPC entities regardless of which optional components those entities carry.
 *
 * Tags are filter-only here: naming one in `each<Transform, NpcTag>` narrows the archetype but
 * does not pass the tag to the callback, so the lambda takes one parameter fewer than the template
 * list suggests.
 */
struct NpcTag
{
};
