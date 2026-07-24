#pragma once

/**
 * @struct AnimationState
 * @brief Walk-cycle animation accumulator for a character.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Tracks the active sprite frame, the timing accumulator, and the position within the four-frame
 * walk sequence (@ref CharacterConstants::WALK_SEQUENCE).
 *
 * @see CharacterKinematics, Facing
 */
struct AnimationState
{
    int currentFrame{0};        ///< Active sprite sheet frame, as a column index.
    float animationTime{0.0f};  ///< Seconds accumulated since the last frame advance.
    int walkSequenceIndex{0};   ///< Current index into the walk sequence.
};
