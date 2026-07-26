#pragma once

/**
 * @struct PlayerTag
 * @brief Empty tag component marking the entity as the player.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Stored as a zero-byte tag, because the ECS selects tag storage for empty structs. It lets
 * systems tell the player entity from NPCs in shared `view` and `each` queries over the common
 * character components.
 *
 * Tags are filter-only here: naming one in `each<Transform, PlayerTag>` narrows the archetype but
 * does not pass the tag to the callback, so the lambda takes one parameter fewer than the template
 * list suggests.
 */
struct PlayerTag
{
};
