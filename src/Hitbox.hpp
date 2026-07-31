#pragma once

#include "CharacterConstants.hpp"

/**
 * @struct Hitbox
 * @brief Feet-anchored collision box dimensions carried per entity.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Per-entity configuration defaulting to the standard one-tile 16x16 character box. Only
 * EntityStore::SpawnPlayer attaches it. NPCs carry no Hitbox today: @ref CharacterCollisionBody
 * stores no dimensions, and NPC-vs-player overlap in NpcAiSystem reads
 * @ref CharacterConstants::HALF_HITBOX_WIDTH and @c HITBOX_HEIGHT directly. The box is plain data
 * on the entity; the logic that consumes it is the stateless @ref CollisionSystem free functions,
 * which take a @c const @c Hitbox& .
 *
 * The box is feet-anchored at bottom-center, matching @ref Transform::position. It extends
 * @c halfWidth to each side of the anchor and @c height straight up. Because +Y points down, "up"
 * means smaller Y, so the box spans `[feet.y - height, feet.y]` vertically.
 *
 * @verbatim
 *                8px               8px
 *          |<--------------+-------------->|
 *          +-------------------------------+   y = feet.y - height   (top edge)
 *          |                               |
 *          |      16 x 16 collision box    |   height = 16
 *          |                               |
 *          +---------------X---------------+   y = feet.y            (bottom edge)
 *                          ^
 *                          feet anchor = Transform::position
 *                          x spans [feet.x - 8, feet.x + 8]
 * @endverbatim
 *
 * @see CollisionSystem, PlayerMovementSystem, CollisionGeometry, CharacterConstants
 */
struct Hitbox
{
    float halfWidth =
        CharacterConstants::HALF_HITBOX_WIDTH;         ///< Half-width left and right of the feet.
    float height = CharacterConstants::HITBOX_HEIGHT;  ///< Box height above the feet.
};
