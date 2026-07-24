#pragma once

#include <cmath>

/**
 * @enum CharacterDirection
 * @brief Cardinal direction a character is facing.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Shared by the player and NPC entities. The value is the player path's logical row index, which
 * @ref PlayerRender::SpriteCoords permutes into the physical sheet row whenever
 * @c IRenderer::RequiresYFlip is true - and both shipping backends return true.
 * @ref NpcRender::SpriteCoords hard-codes that same physical order, so both sheet families share
 * one layout; only the route to it differs.
 *
 * @par Sprite sheet row mapping
 * | Direction | Value (logical row) | Physical sheet row |
 * |-----------|---------------------|--------------------|
 * | DOWN      |                   0 |                  2 |
 * | UP        |                   1 |                  3 |
 * | LEFT      |                   2 |                  1 |
 * | RIGHT     |                   3 |                  0 |
 *
 * @see Facing, CardinalFromDelta, PlayerRender, NpcRender
 */
enum class CharacterDirection
{
    DOWN = 0,  ///< Facing down, toward the camera, along +Y.
    UP = 1,    ///< Facing up, away from the camera, along -Y.
    LEFT = 2,  ///< Facing left, along -X.
    RIGHT = 3  ///< Facing right, along +X.
};

using Direction = CharacterDirection;  ///< Shorthand alias used throughout character-facing code.

/**
 * @brief Map a movement delta to a cardinal facing direction.
 * @ingroup Entities
 *
 * Shared by every "face the way I am moving" and "face the target" site, including NPC patrol
 * facing and dialogue snap facing. The horizontal magnitude is compared with a strict `>`, so an
 * equal-magnitude diagonal resolves to the vertical axis.
 *
 * @param dx  Horizontal component of the delta.
 * @param dy  Vertical component of the delta, positive downward.
 * @return    The matching direction. A zero delta returns UP, so a caller that wants to keep the
 *            current facing must guard against `dx == 0 && dy == 0` itself.
 */
inline CharacterDirection CardinalFromDelta(float dx, float dy)
{
    if (std::abs(dx) > std::abs(dy))
    {
        return (dx > 0.0f) ? CharacterDirection::RIGHT : CharacterDirection::LEFT;
    }
    return (dy > 0.0f) ? CharacterDirection::DOWN : CharacterDirection::UP;
}
