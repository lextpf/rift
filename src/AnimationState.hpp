#pragma once

/**
 * @struct AnimationState
 * @brief Walk-cycle animation accumulator for a character.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The shared animation component carved out of the GameCharacter base. Tracks
 * the active sprite frame, the timing accumulator, and the index into the
 * four-frame walk sequence (@ref CharacterConstants::WALK_SEQUENCE). The
 * advance/reset logic lives in the free functions in CharacterKinematics.hpp.
 *
 * Plain data struct. Flat aggregate so it is usable directly as an ECS
 * component (packed storage).
 *
 * @see CharacterKinematics, Facing
 */
struct AnimationState
{
    int currentFrame{0};        ///< Active sprite sheet frame (column index)
    float animationTime{0.0f};  ///< Accumulator for animation timing
    int walkSequenceIndex{0};   ///< Current index into the walk sequence
};
