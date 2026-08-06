#pragma once

#include "CharacterDirection.hpp"

/**
 * @struct Facing
 * @brief The cardinal direction a character is facing.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * World-absolute: dialogue, AI and interaction checks all reason in world directions. The sprite
 * row a rotated camera should show is derived from this at render time through
 * @ref cameraFacing::ScreenFacing, not stored here.
 *
 * Kept separate from AnimationState so render UV selection and AI can read facing without pulling
 * animation timing onto the same cache line.
 *
 * @see CharacterDirection, AnimationState
 */
struct Facing
{
    CharacterDirection dir{CharacterDirection::DOWN};  ///< Current facing direction.
};
