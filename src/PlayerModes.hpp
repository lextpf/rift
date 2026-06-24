#pragma once

#include "AnimationType.hpp"

/**
 * @struct PlayerModes
 * @brief Player movement-mode flags + developer-console speed multiplier.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The movement-mode state carved out of PlayerCharacter's loose members. The
 * active mode resolves as bicycling > running > walking; @ref noClip and
 * @ref speedMultiplier are developer-console knobs. The mode multipliers
 * (x1.75 running, x2.25 bicycle) are free-function constants in the movement
 * logic, not stored here. @ref animationType is the live animation state the
 * movement logic derives from the active mode + motion each frame.
 *
 * Plain data struct: flat aggregate, usable directly as an ECS component.
 */
struct PlayerModes
{
    bool isRunning{false};        ///< Running mode (1.75x speed).
    bool isBicycling{false};      ///< Bicycle mode (2.25x speed).
    bool noClip{false};           ///< Developer no-clip mode (skip collision).
    float speedMultiplier{1.0f};  ///< Developer speedhack multiplier (1.0 = normal).

    AnimationType animationType{AnimationType::IDLE};  ///< Live animation state (idle/walk/run).
};
