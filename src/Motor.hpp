#pragma once

#include "MotorParams.hpp"

#include <glm/glm.hpp>

/**
 * @struct Motor
 * @brief Momentum-movement kinematic state: velocity, tuning, latched grid stop.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The data half of the player's momentum movement, carved out of the former
 * PlayerMotor class. That logic (acceleration, deceleration, and the grid-aligned
 * stop resolution) is now the stateless free functions in MotionSystem.hpp that
 * operate on a @c Motor.
 *
 * Plain data struct. Flat aggregate (a nested @ref MotorParams aggregate is
 * fine) so it is usable directly as an ECS component (packed storage).
 *
 * @see MotionSystem, MotorParams
 */
struct Motor
{
    glm::vec2 velocity{0.0f, 0.0f};    ///< Current velocity (px/s).
    MotorParams params{};              ///< Tunable accel/decel parameters.
    glm::vec2 stopTarget{0.0f, 0.0f};  ///< Latched per-axis tile-aligned stop point.
    bool hasStopTarget{false};         ///< True once a grid-aligned stop is latched.
};
