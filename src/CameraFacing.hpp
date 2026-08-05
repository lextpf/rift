#pragma once

#include "CharacterDirection.hpp"

#include <cmath>

/**
 * @brief Maps a character's world-space facing to the sprite row the viewer
 *        should see under an orbiting camera.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Each of the four sheet rows is a screen direction rather than a world
 * direction. The LEFT row is the artwork for "walking leftwards across the
 * screen", not for "facing world-west". A fixed camera makes the two coincide,
 * so the flat pipeline can index the sheet with the stored @c Facing directly;
 * under an orbiting camera they diverge, and a character walking world-west
 * appears to walk *toward* a viewer who has swung round to the west.
 *
 * The stored @c Facing stays world-absolute - dialogue, NPC AI and interaction
 * checks reason about world directions and must not follow the camera. Only the
 * render-time row lookup rotates, the same split the DS games use: project the
 * facing onto the camera's two ground-plane screen axes, take whichever
 * dominates.
 * @verbatim
 *   right = ( cos yaw, -sin yaw)    screen-right
 *   toCam = ( sin yaw,  cos yaw)    screen-down (toward the viewer)
 *
 *   sx = facing . right      sy = facing . toCam
 *   |sy| >= |sx|  ->  sy > 0 ? DOWN : UP
 *   otherwise     ->  sx > 0 ? RIGHT : LEFT
 * @endverbatim
 * At yaw 0 this is exactly the identity, so the flat pipeline is unaffected.
 */
namespace cameraFacing
{

/// @brief World-space unit vector a facing points along (x east, y south).
inline void FacingVector(CharacterDirection dir, float& outX, float& outY)
{
    switch (dir)
    {
        case CharacterDirection::UP:
            outX = 0.0f;
            outY = -1.0f;
            break;
        case CharacterDirection::DOWN:
            outX = 0.0f;
            outY = 1.0f;
            break;
        case CharacterDirection::LEFT:
            outX = -1.0f;
            outY = 0.0f;
            break;
        case CharacterDirection::RIGHT:
            outX = 1.0f;
            outY = 0.0f;
            break;
    }
}

/**
 * @brief The sprite row to draw for a world-space @p facing under @p yawRadians.
 * @param facing     Stored, world-absolute facing; unchanged by this call.
 * @param yawRadians Camera yaw; 0 places the camera due map-south.
 * @return The direction whose sheet row shows the correct side of the character.
 */
inline CharacterDirection ScreenFacing(CharacterDirection facing, float yawRadians)
{
    float fx = 0.0f;
    float fy = 0.0f;
    FacingVector(facing, fx, fy);

    const float sinYaw = std::sin(yawRadians);
    const float cosYaw = std::cos(yawRadians);

    const float screenX = fx * cosYaw - fy * sinYaw;  // dot with screen-right
    const float screenY = fx * sinYaw + fy * cosYaw;  // dot with screen-down

    // Ties favour the vertical rows, matching the flat pipeline on exact diagonals.
    if (std::abs(screenY) >= std::abs(screenX))
    {
        return screenY > 0.0f ? CharacterDirection::DOWN : CharacterDirection::UP;
    }
    return screenX > 0.0f ? CharacterDirection::RIGHT : CharacterDirection::LEFT;
}

}  // namespace cameraFacing
