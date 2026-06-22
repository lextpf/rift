#pragma once

#include "CharacterDirection.hpp"

/**
 * @struct Facing
 * @brief The cardinal direction a character is facing.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The shared facing component carved out of the GameCharacter base. Kept
 * separate from AnimationState so render UV selection and AI can read facing
 * without pulling animation timing onto the same cache line.
 *
 * Plain data struct. Flat aggregate so it is usable directly as an ECS
 * component (packed storage).
 *
 * @see CharacterDirection, AnimationState
 */
struct Facing
{
    CharacterDirection dir{CharacterDirection::DOWN};  ///< Current facing direction
};
