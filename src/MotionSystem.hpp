#pragma once

#include "Motor.hpp"

#include <glm/glm.hpp>

/**
 * @brief Stateless momentum-movement kinematics over a @ref Motor component.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The logic half of the player's momentum movement, operating on a plain
 * @ref Motor by reference. Renderer-free and collision-free: the owner feeds in
 * the desired input direction and gets back the displacement to apply this
 * frame, runs that displacement through its own collision pipeline, and zeroes a
 * blocked axis on the @ref Motor. These free functions are the ECS movement
 * system. @ref ComputeDisplacement, @ref IsMoving and the axis-zero helpers are driven
 * only by @ref PlayerMovementSystem::Step; @ref Reset is additionally called by
 * @ref PlayerMovementSystem::Stop and by PlayerSystem::SetTilePosition and
 * PlayerSystem::SetPositionRaw after a teleport.
 *
 * @par Two regimes
 * With input, each velocity axis moves toward @c inputDir * targetSpeed at a fixed acceleration,
 * which is grid-agnostic. Without input, the motor resolves its stop onto the tile grid instead
 * of coasting to an arbitrary sub-tile position, and that is what leaves the player at rest
 * aligned with the map.
 *
 * @see Motor, MotorParams, PlayerMovementSystem
 */
namespace MotionSystem
{
/**
 * @brief Advance @p motor's velocity from input and return this frame's displacement.
 *
 * @par No-input stop resolution
 * The first no-input frame latches a per-axis rest point: it predicts where a free
 * deceleration would stop (@c v^2 / 2a beyond the current position) and aligns that
 * prediction to the grid. The latch is held across frames - it is cleared only by an input
 * frame, by @ref ZeroAxisX / @ref ZeroAxisY / @ref Reset, or once the motor is both stopped
 * and producing no further displacement. Re-latching every frame would chase a moving
 * target and never settle.
 *
 * Each axis then decelerates to land exactly on its rest point, with the deceleration
 * solved from the distance still to travel and clamped to a sane band:
 * @f[
 * a_{eff} = \mathrm{clamp}\!\left(\frac{v^{2}}{2d},\; a_{min},\; a_{max}\right),
 * \qquad v' = \max\!\left(0,\; v - a_{eff}\,\Delta t\right),
 * \qquad \Delta x = \frac{v + v'}{2}\,\Delta t
 * @f]
 * where @f$d@f$ is the remaining distance to the aligned rest point along the current
 * heading. The trapezoidal @f$\Delta x@f$ (rather than @f$v'\Delta t@f$) is what makes the
 * landing exact at typical frame times; an overshoot is clamped to land on the target. An
 * axis already below @c speedEpsilon is instead eased onto its line at @c settleSpeed.
 *
 * @par The two axes rest on different grid lines
 * @verbatim
 *   One 16x16 tile. The feet anchor is bottom-center, so each axis rests on a
 *   different family of grid lines. The asymmetry follows from the anchor
 *   convention, so do not change one axis to match the other.
 *
 *        x=16              x=32
 *          +---------------+       y=16
 *          |               |
 *          |       .       |       . = tile center, x = 24
 *          |               |
 *          +-------X-------+       y=32
 *                  ^
 *                  feet anchor rests at x = 24 (tile center)
 *                                   and y = 32 (tile bottom edge)
 *
 *     AlignedRestX(x) = round((x - w/2) / w) * w + w/2   ->  8, 24, 40, ...
 *     AlignedRestY(y) = round( y        / h) * h         ->  0, 16, 32, ...
 * @endverbatim
 *
 * @warning The returned displacement is a request, not a result. The caller must run it through
 * collision and then zero any axis that was blocked. Otherwise velocity keeps accumulating
 * against the wall and the player climbs the corner in visible steps on release.
 *
 * @param motor       Motion state to advance (velocity + latched grid stop).
 * @param position    Current world position (feet) - used for grid resolution.
 * @param inputDir    Desired direction; normalized-or-zero. Zero means decelerate.
 * @param targetSpeed Already mode-multiplied target speed (px/s). Ignored on a no-input
 *                    frame - deceleration is governed by MotorParams, not by this.
 * @param tileSize    Tile size in pixels (for grid-aligned stops).
 * @param dt          Frame time in seconds; a non-positive value returns zero and leaves
 *                    @p motor untouched.
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
 *
 * @param motor Motion state to test; the threshold is its own @c params.speedEpsilon.
 * @return True while the motor is still considered to be moving.
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
 *
 * Symmetric with @ref ZeroAxisX - it likewise invalidates the latched grid-stop target so
 * the next no-input frame re-latches from the post-collision position. The latch is a
 * single flag covering both axes, so either call drops both rest points.
 */
inline void ZeroAxisY(Motor& motor)
{
    motor.velocity.y = 0.0f;
    motor.hasStopTarget = false;
}

/**
 * @brief Zero all velocity and clear the latched stop (stop / teleport).
 *
 * Required after any position change the motor did not produce, such as a teleport, a tile snap
 * or stuck recovery. A stale rest point would otherwise drag the character back toward a grid
 * line near its previous position.
 */
inline void Reset(Motor& motor)
{
    motor.velocity = glm::vec2(0.0f);
    motor.hasStopTarget = false;
}
}  // namespace MotionSystem
