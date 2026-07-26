#pragma once

#include "AnimationType.hpp"

/**
 * @struct PlayerModes
 * @brief Player movement-mode flags plus the developer-console speed multiplier.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The active mode resolves as bicycling over running over walking. @ref noClip and
 * @ref speedMultiplier are developer-console knobs.
 *
 * The mode multipliers are not stored here. They are the named constants
 * @ref CharacterConstants::RUN_SPEED_MULTIPLIER and
 * @ref CharacterConstants::BICYCLE_SPEED_MULTIPLIER, which PlayerMovementSystem::Step applies over
 * the entity's @ref Speed, applying the mode multiplier first and @ref speedMultiplier second.
 *
 * @see CharacterConstants, PlayerMovementSystem, AnimationType
 */
struct PlayerModes
{
    bool isRunning{false};    ///< Running mode, at RUN_SPEED_MULTIPLIER (1.75x).
    bool isBicycling{false};  ///< Bicycle mode, at BICYCLE_SPEED_MULTIPLIER (2.25x). Beats running.
    /**
     * @brief Developer no-clip mode.
     *
     * The movement systems never read this. Game reads it and passes a null tilemap and null NPC
     * list into PlayerSystem::Move, and that is what disables world and NPC blocking.
     */
    bool noClip{false};
    float speedMultiplier{1.0f};  ///< Developer speed multiplier; 1.0 = normal.

    /// Live animation state, which the movement logic derives each frame from the active mode and
    /// the current motion.
    AnimationType animationType{AnimationType::IDLE};
};
