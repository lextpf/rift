#pragma once

#include "Motor.hpp"

#include <glm/glm.hpp>

/**
 * @brief Stateless momentum-movement kinematics over a @ref Motor component.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The logic half of the player's momentum movement, extracted from the former
 * PlayerMotor class so it operates on a plain @ref Motor by reference.
 * Renderer-free and collision-free: the owner feeds in the desired input
 * direction and gets back the displacement to apply this frame, runs that
 * displacement through its own collision pipeline, and zeroes a blocked axis on
 * the @ref Motor. These free functions are the future ECS movement system.
 */

namespace MotionSystem
{
/**
 * @brief Advance @p motor's velocity from input and return this frame's displacement.
 * @param motor       Motion state to advance (velocity + latched grid stop).
 * @param position    Current world position (feet) - used for grid resolution.
 * @param inputDir    Desired direction; normalized-or-zero. Zero means decelerate.
 * @param targetSpeed Already mode-multiplied target speed (px/s).
 * @param tileSize    Tile size in pixels (for grid-aligned stops).
 * @param dt          Frame time in seconds.
 * @return World-space displacement to apply this frame.
 */
glm::vec2 ComputeDisplacement(Motor& motor,
                              glm::vec2 position,
                              glm::vec2 inputDir,
                              float targetSpeed,
                              float tileSize,
                              float dt);

/**
 * @brief True if the velocity magnitude exceeds the stopped threshold.
 */
bool IsMoving(const Motor& motor);

/**
 * @brief Drop the X velocity component (called when collision blocks X).
 *
 * Also invalidates the latched grid-stop target so the next no-input frame
 * re-latches from the post-collision position (convergent, cannot oscillate).
 */
inline void ZeroAxisX(Motor& motor)
{
    motor.velocity.x = 0.0f;
    motor.hasStopTarget = false;
}

/**
 * @brief Drop the Y velocity component (called when collision blocks Y).
 */
inline void ZeroAxisY(Motor& motor)
{
    motor.velocity.y = 0.0f;
    motor.hasStopTarget = false;
}

/**
 * @brief Zero all velocity and clear the latched stop (stop / teleport).
 */
inline void Reset(Motor& motor)
{
    motor.velocity = glm::vec2(0.0f);
    motor.hasStopTarget = false;
}
}  // namespace MotionSystem
