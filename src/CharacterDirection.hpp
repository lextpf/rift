#pragma once

#include <cmath>

/**
 * @enum CharacterDirection
 * @brief Cardinal direction a character is facing.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Unified direction enum shared by the player and NPC entities. Values map
 * directly to sprite sheet row offsets for animation lookup, though the player
 * and NPC render paths each apply their own row-mapping table on top.
 *
 * @par Sprite Sheet Row Mapping
 * | Direction | Value | Player Row | NPC Row |
 * |-----------|-------|------------|---------|
 * | DOWN      |     0 | 0          | 2       |
 * | UP        |     1 | 1          | 3       |
 * | LEFT      |     2 | 2          | 1       |
 * | RIGHT     |     3 | 3          | 0       |
 *
 * @par Type Alias
 * `Direction` is provided as a shorthand alias used throughout the player and
 * NPC code.
 *
 * @see Facing, CardinalFromDelta, PlayerRender, NpcRender
 */
enum class CharacterDirection
{
    DOWN = 0,  ///< Facing down (towards camera, +Y direction)
    UP = 1,    ///< Facing up (away from camera, -Y direction)
    LEFT = 2,  ///< Facing left (-X direction)
    RIGHT = 3  ///< Facing right (+X direction)
};

using Direction = CharacterDirection;  ///< Shorthand alias for character-facing code

/**
 * @brief Map a movement delta to a cardinal facing direction.
 * @ingroup Entities
 *
 * The shared primitive behind every "face the way I'm moving / face the target"
 * site (NPC patrol facing, dialogue-snap facing). Equal-magnitude diagonals
 * resolve to the **vertical** axis (DOWN/UP) - the tie-break every call site
 * uses (strict `>` on the horizontal magnitude). A zero delta returns UP;
 * callers that want to keep the current facing must guard `dx == dy == 0`.
 */
inline CharacterDirection CardinalFromDelta(float dx, float dy)
{
    if (std::abs(dx) > std::abs(dy))
    {
        return (dx > 0.0f) ? CharacterDirection::RIGHT : CharacterDirection::LEFT;
    }
    return (dy > 0.0f) ? CharacterDirection::DOWN : CharacterDirection::UP;
}
