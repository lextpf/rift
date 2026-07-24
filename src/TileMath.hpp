#pragma once

#include <cmath>
#include <glm/glm.hpp>

/**
 * @brief Shared world-position <-> tile-coordinate conversions.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * "Which tile is this feet position in" has three deliberately different answers
 * depending on who is asking, and these are the single source for each. They
 * differ only in how the Y coordinate is nudged before the floor, so pick by the
 * call's intent rather than by which one happens to be nearest:
 *
 * - @ref TileIndex      bare floor (tile column from X; also a bare-floor row).
 * - @ref StandingTileRow feet nudged up @ref STANDING_EPS so an entity standing
 *                        exactly on a boundary counts as the tile above
 *                        (NPC patrol + player-interaction convention).
 * - @ref AnchorTileRow   bottom-center anchor shifted up half a tile so it maps
 *                        to the tile its center occupies (tile-center snapping
 *                        and collision); @p eps nudges further (callers differ:
 *                        0 for WorldToTileCoord, 0.001 for tile-center, the
 *                        collision epsilon for the collision pipeline).
 * - @ref TileFeetCenter  inverse: bottom-center feet position at a tile's center.
 *
 * Worked example, 16px tiles. World Y grows downward, so row N covers [16N, 16N+16).
 *
 * @verbatim
 *   world Y
 *      0  +---------------------+  row 0
 *         |                     |
 *     16  +---------------------+  row 1   <- feet A (y = 16.0), on the boundary
 *     20  :  . . . . . . . . .  :          <- feet B (y = 20.0)
 *     24  :  . . . . . . . . .  :          <- feet C (y = 24.0), mid-tile
 *     32  +---------------------+  row 2   <- feet D (y = 32.0), on the boundary
 *
 *     feetY   TileIndex   StandingTileRow   AnchorTileRow
 *      16.0       1              0                0
 *      20.0       1              1                0
 *      24.0       1              1                1
 *      32.0       2              1                1
 * @endverbatim
 *
 * A and D show the boundary case: TileIndex drops into the row below, while
 * StandingTileRow keeps the entity on the row it is standing on top of. B shows why
 * AnchorTileRow is separate again - the feet are already inside row 1, but the half-tile
 * shift puts the sprite body's center at y = 12, so the entity still occupies row 0.
 * Picking the wrong one of the three is the off-by-one-row bug this file exists to stop.
 */

namespace TileMath
{
/**
 * @brief Pixel nudge applied by @ref StandingTileRow (a boundary-standing entity
 * belongs to the tile above, not below).
 */
inline constexpr float STANDING_EPS = 0.1f;

/// @brief Bare floor index: tile column from world X, or a bare-floor row from Y.
inline int TileIndex(float coord, float tileSize)
{
    return static_cast<int>(std::floor(coord / tileSize));
}

/// @brief "Standing" tile row: feet Y nudged up @ref STANDING_EPS.
inline int StandingTileRow(float feetY, float tileHeight)
{
    return static_cast<int>(std::floor((feetY - STANDING_EPS) / tileHeight));
}

/// @brief "Anchor" tile row: bottom-center anchor shifted up half a tile (+ @p eps).
inline int AnchorTileRow(float feetY, float tileHeight, float eps = 0.0f)
{
    return static_cast<int>(std::floor((feetY - tileHeight * 0.5f - eps) / tileHeight));
}

/// @brief Bottom-center feet position at the center of tile (@p tileX, @p tileY).
inline glm::vec2 TileFeetCenter(int tileX, int tileY, float tileWidth, float tileHeight)
{
    return glm::vec2(tileX * tileWidth + tileWidth * 0.5f, tileY * tileHeight + tileHeight);
}

/// @brief Square-tile overload of @ref TileFeetCenter.
inline glm::vec2 TileFeetCenter(int tileX, int tileY, float tileSize)
{
    return TileFeetCenter(tileX, tileY, tileSize, tileSize);
}
}  // namespace TileMath
