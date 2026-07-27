#pragma once

#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

class Tilemap;

/**
 * @brief Pure BFS utilities over a Tilemap's NPC navigation grid.
 * @author Fable 5 (https://github.com/claude)
 * @ingroup World
 *
 * @ref FindPath and @ref FloodReachable both read the tilemap's `GetNavigation(x, y)` grid, which
 * is NPC walkability, not the player collision grid. Both treat an out-of-bounds tile as
 * non-navigable and move in cardinal directions only, so connectivity is 4-way.
 *
 * Both allocate a bit-packed visited mask of `W * H` entries. @ref FindPath allocates a second
 * whole-map array on top of it, a predecessor index per cell at 8 bytes each, so its per-call
 * cost scales with the map and not with the path length. Neither allocation happens before the
 * endpoint navigability check rejects a bad call.
 *
 * @warning Navigation is the only test applied here, and collision is deliberately ignored.
 * PatrolRoute::IsValidTile also rejects any tile carrying a collision flag, which makes these
 * functions more permissive than the patrol generator: a path reported here may cross a tile no
 * NPC can patrol. Treat a result as connectivity of the navigation grid, not as a walkable NPC
 * route.
 *
 * The developer-console commands `nav.path` and `nav.reachable` are the callers.
 *
 * @see PatrolRoute
 */
namespace Pathfinding
{
/**
 * @brief Find the BFS shortest path from @p start to @p goal.
 *
 * Several paths of equal length usually exist on a grid. The one returned is deterministic,
 * decided by the fixed neighbor expansion order +X, -X, +Y, -Y, so the same map and the same
 * endpoints always yield the same sequence.
 *
 * @param tilemap  Supplies the navigation grid.
 * @param start    Starting tile coordinate.
 * @param goal     Destination tile coordinate.
 * @return         The inclusive sequence from @p start to @p goal. Empty when either endpoint
 *                 is out of bounds or non-navigable, or when no 4-connected navigable route
 *                 exists, so an empty result does not single out @p goal. Exactly one element
 *                 when @p start equals @p goal.
 */
[[nodiscard]] std::vector<glm::ivec2> FindPath(const Tilemap& tilemap,
                                               glm::ivec2 start,
                                               glm::ivec2 goal);

/**
 * @brief Count the navigable tiles reachable from @p start.
 *
 * @param tilemap        Supplies the navigation grid.
 * @param start          Tile to flood from.
 * @param outBoundsMin   Receives the inclusive minimum tile coordinate of the reachable set.
 *                       Written only when the count is greater than 0.
 * @param outBoundsMax   Receives the inclusive maximum tile coordinate of the reachable set.
 *                       Written only when the count is greater than 0.
 * @return               Number of reachable navigable tiles.
 */
[[nodiscard]] std::size_t FloodReachable(const Tilemap& tilemap,
                                         glm::ivec2 start,
                                         glm::ivec2& outBoundsMin,
                                         glm::ivec2& outBoundsMax);
}  // namespace Pathfinding
