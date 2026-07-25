#pragma once

/**
 * @struct MotorParams
 * @brief Tunable acceleration and deceleration parameters for momentum movement.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Kept in its own header so the @ref Motor component and the @ref MotionSystem free functions can
 * share it without depending on each other.
 *
 * Two deceleration regimes coexist. @ref decel predicts where a free stop would land. The
 * resolved deceleration, bounded by @ref minResolvedDecel and @ref maxResolvedDecel, is then
 * solved each frame so the motor lands exactly on the grid-aligned rest point instead.
 *
 * @see MotionSystem, Motor
 */
struct MotorParams
{
    float accel = 800.0f;  ///< Ramp-up rate in px/s^2, tuned for a snappy start.
    /// Natural ramp-down rate in px/s^2, used only to predict where a free stop would land.
    /// The glide settles forward onto the grid; higher values latch a rest point behind the feet.
    float decel = 200.0f;
    float minResolvedDecel = 40.0f;    ///< Lower clamp for grid-resolved deceleration, in px/s^2.
    float maxResolvedDecel = 5000.0f;  ///< Upper clamp for grid-resolved deceleration, in px/s^2.
    float settleSpeed = 60.0f;         ///< Gentle pull of an idle axis onto its grid line, in px/s.
    float stopEpsilon = 0.5f;          ///< Distance to target that counts as arrived, in pixels.
    float speedEpsilon = 1.5f;         ///< Speed that counts as stopped, in px/s.
};
