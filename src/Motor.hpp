#pragma once

#include "MotorParams.hpp"

#include <glm/glm.hpp>

/**
 * @struct Motor
 * @brief Momentum-movement kinematic state: velocity, tuning, latched grid stop.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The data half of the player's momentum movement. Acceleration, deceleration and grid-aligned
 * stop resolution are the stateless free functions in MotionSystem.hpp, which take a @c Motor by
 * reference.
 *
 * @see MotionSystem, MotorParams
 */
struct Motor
{
    glm::vec2 velocity{0.0f, 0.0f};    ///< Current velocity in pixels/second.
    MotorParams params{};              ///< Tunable acceleration and deceleration parameters.
    glm::vec2 stopTarget{0.0f, 0.0f};  ///< Latched per-axis tile-aligned stop point.
    /**
     * @brief True once a grid-aligned stop is latched.
     *
     * Any axis that collision blocks must clear this, which MotionSystem::ZeroAxisX and
     * MotionSystem::ZeroAxisY both do. A stale target left latched against a wall makes the
     * player climb the corner in visible steps.
     */
    bool hasStopTarget{false};
};
