#pragma once

#include <glm/glm.hpp>

/**
 * @struct PlayerMovementState
 * @brief Plain per-player slide/axis/idle-snap hysteresis state.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Groups the frame-to-frame collision state that CollisionResolver reads and
 * writes while resolving player movement (wall-slide direction commit, axis
 * preference, idle-snap interpolation, last input sign, stuck-recovery anchor).
 *
 * @par Why this exists
 * The fields were previously private members of PlayerCharacter accessed by
 * CollisionResolver through `friend`. Extracting them into a plain struct lets
 * the resolver operate on a data object exposed via a public accessor instead
 * of reaching into the player's internals, and is the shape the future
 * `PlayerMovement` ECS component takes verbatim.
 *
 * Plain data struct: unprefixed @c camelCase fields, no invariants.
 *
 * @see PlayerMovementSystem, CollisionResolver
 */
struct PlayerMovementState
{
    glm::vec2 slideDir{0.0f};    ///< Last chosen wall-slide direction (jitter hysteresis)
    float slideTimer{0.0f};      ///< Time remaining before the slide direction may change
    int axisPref{0};             ///< Axis preference: -1 = prefer Y, +1 = prefer X, 0 = none
    float axisTimer{0.0f};       ///< Time remaining before the axis preference may change
    glm::vec2 snapStart{0.0f};   ///< Position when idle snap began
    glm::vec2 snapTarget{0.0f};  ///< Target position for idle snap
    float snapProgress{1.0f};    ///< Smoothstep idle-snap progress (0 -> 1)
    glm::vec2 lastSafeTileCenter{0.0f};  ///< Last valid tile center (stuck recovery)
    int lastInputX{0};                   ///< Last non-zero A/D sign: -1 left, +1 right
    int lastInputY{0};                   ///< Last non-zero W/S sign: -1 up, +1 down
};
