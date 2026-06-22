#pragma once

/**
 * @enum AnimationType
 * @brief Animation state machine states.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Determines which sprite sheet to use and animation timing:
 * - IDLE: Standing still, uses walking sheet frame 0
 * - WALK: Walking animation at base speed
 * - RUN: Running animation at 50% of normal frame duration (2x faster)
 */
enum class AnimationType
{
    IDLE = 0,  ///< Standing still (single frame)
    WALK = 1,  ///< Walking animation (4-step sequence [1,0,2,0])
    RUN = 2    ///< Running/sprinting animation (same sequence, faster timing)
};
