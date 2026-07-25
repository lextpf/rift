#pragma once

#include <glm/glm.hpp>

/**
 * @struct Transform
 * @brief World position of a character, anchored at the feet.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The anchor is bottom-center, where the feet meet the ground. Y-sort depth order and the
 * feet-anchored collision box are both defined against that point, so every consumer assumes it.
 *
 * @see Elevation, Facing, AnimationState
 */
struct Transform
{
    glm::vec2 position{0.0f, 0.0f};  ///< World position of the sprite's bottom-center anchor.
};
