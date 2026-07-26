#pragma once

#include <glm/glm.hpp>

/**
 * @struct PlayerInputState
 * @brief Per-frame player input and edge-tracking state for facing and motion gating.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Holds the last accepted movement direction, whether the player is in motion, and the
 * previous-frame axis-active flags that drive the rising-edge facing rule. This is distinct from
 * PlayerMovementState, which holds collision and slide hysteresis.
 *
 * @see PlayerMovementSystem, PlayerMovementState
 */
struct PlayerInputState
{
    /**
     * @brief Unit direction of the last movement that was committed.
     *
     * This is not raw input, and a frame that ended up blocked does not update it. It is read only
     * to detect a dominant-axis flip, which resets the wall-slide hysteresis.
     */
    glm::vec2 lastMovementDirection{0.0f, 0.0f};
    /// True while the motor reports motion. It tracks actual velocity rather than key state, so it
    /// stays true through a glide to rest, and it drives animation start and stop transitions.
    bool isMoving{false};
    bool prevAxisXActive{false};  ///< Whether X axis input was non-zero last frame.
    bool prevAxisYActive{false};  ///< Whether Y axis input was non-zero last frame.
};
