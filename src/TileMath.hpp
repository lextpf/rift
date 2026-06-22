#pragma once

#include <cmath>
#include <glm/glm.hpp>

/// @file TileMath.hpp
/// @brief Shared world-position <-> tile-coordinate conversions.
/// @ingroup World
///
/// Rift had ~15 open-coded copies of "which tile is this feet position in" with
/// three deliberately different Y conventions that had drifted apart. These are
/// the single source for each. Pick the one matching the call's intent:
///
/// - @ref TileIndex      bare floor (tile column from X; also a bare-floor row).
/// - @ref StandingTileRow feet nudged up @ref STANDING_EPS so an entity standing
///                        exactly on a boundary counts as the tile above
///                        (NPC patrol + player-interaction convention).
/// - @ref AnchorTileRow   bottom-center anchor shifted up half a tile so it maps
///                        to the tile its centre occupies (tile-center snapping
///                        and collision); @p eps nudges further (callers differ:
///                        0 for WorldToTileCoord, 0.001 for tile-center, the
///                        collision epsilon for the collision pipeline).
/// - @ref TileFeetCenter  inverse: bottom-center feet position at a tile's centre.

namespace TileMath
{
/// @brief Pixel nudge applied by @ref StandingTileRow (a boundary-standing entity
/// belongs to the tile above, not below).
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

/// @brief Bottom-center feet position at the centre of tile (@p tileX, @p tileY).
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
