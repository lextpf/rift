#pragma once

#include <glm/glm.hpp>

/**
 * @struct PlayerInputState
 * @brief Per-frame player input / edge-tracking state (facing + motion gating).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The input edge-tracking state carved out of PlayerCharacter's loose members:
 * the last movement direction (for facing), whether the player is in motion,
 * and the previous-frame axis-active flags that drive the rising-edge facing
 * rule. Distinct from PlayerMovementState (which holds collision/slide
 * hysteresis); the two last-input notions may be reconciled later.
 *
 * Plain data struct: flat aggregate, usable directly as an ECS component.
 */
struct PlayerInputState
{
    glm::vec2 lastMovementDirection{0.0f, 0.0f};  ///< Previous frame's movement direction.
    bool isMoving{false};                         ///< True if currently in motion.
    bool prevAxisXActive{false};                  ///< Was X axis (A/D) input non-zero last frame?
    bool prevAxisYActive{false};                  ///< Was Y axis (W/S) input non-zero last frame?
};
