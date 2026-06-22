#pragma once

#include <glm/glm.hpp>

/**
 * @struct Transform
 * @brief World position (bottom-center / feet) of a character.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The shared position component carved out of the GameCharacter base. The
 * convention is bottom-center anchoring (where the feet touch the ground),
 * matching the Y-sort and collision invariants used across the engine.
 *
 * Plain data struct: unprefixed @c camelCase fields, no invariants. Flat
 * aggregate so it is usable directly as an ECS component (packed storage).
 *
 * @see Elevation, Facing, AnimationState
 */
struct Transform
{
    glm::vec2 position{0.0f, 0.0f};  ///< World position (bottom-center of sprite)
};
