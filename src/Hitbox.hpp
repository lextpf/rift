#pragma once

#include "CharacterConstants.hpp"

/**
 * @struct Hitbox
 * @brief Feet-anchored collision box dimensions carried per entity.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The player's collision-box configuration, defaulting to the standard one-tile
 * character hitbox. Replaces the immutable @c m_Hitbox config that used to live
 * inside the @c CollisionResolver class: now it is plain data on the entity and
 * the collision logic is a set of stateless @ref CollisionSystem free functions
 * that take a @c const @c Hitbox& . @c halfWidth extends left/right of the feet,
 * @c height straight up.
 *
 * Flat reflectable aggregate (default member initializers, no ctors/bases) so it
 * is usable directly as a packed ECS component.
 *
 * @see CollisionSystem, PlayerMovementSystem, CollisionGeometry::Hitbox
 */
struct Hitbox
{
    float halfWidth =
        CharacterConstants::HALF_HITBOX_WIDTH;         ///< Half-width left/right of the feet.
    float height = CharacterConstants::HITBOX_HEIGHT;  ///< Box height above the feet.
};
