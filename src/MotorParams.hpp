#pragma once

/**
 * @struct MotorParams
 * @brief Tunable acceleration/deceleration parameters for momentum movement.
 * @ingroup Entities
 *
 * Carved out into its own header (originally from the former PlayerMotor) so the
 * @ref Motor ECS component and the @ref MotionSystem free functions can share it
 * without depending on each other. Flat aggregate (packed-storage friendly).
 */
struct MotorParams
{
    float accel = 800.0f;              ///< Ramp-up rate (px/s^2): snappy start.
    float decel = 200.0f;              ///< Natural ramp-down rate (px/s^2): glide settles forward
                                       ///< onto the grid (higher values can lurch backward).
    float minResolvedDecel = 40.0f;    ///< Lower clamp for grid-resolved decel (px/s^2).
    float maxResolvedDecel = 5000.0f;  ///< Upper clamp for grid-resolved decel (px/s^2).
    float settleSpeed = 60.0f;         ///< Gentle pull of an idle axis onto its grid line (px/s).
    float stopEpsilon = 0.5f;          ///< Distance-to-target treated as "arrived" (px).
    float speedEpsilon = 1.5f;         ///< Speed treated as "stopped" (px/s).
};
