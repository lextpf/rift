#pragma once

#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

class Tilemap;

/**
 * @brief Pure BFS utilities over a Tilemap's NPC navigation grid.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * `FindPath` and `FloodReachable` operate on the tilemap's `GetNavigation(x,y)`
 * grid (NPC walkability), not the player collision grid. Both treat
 * out-of-bounds tiles as non-navigable. Allocate a visited mask sized to the
 * full map; cardinal-only moves (4-connectivity).
 *
 * Used by the developer-console commands `nav.path` and `nav.reachable`.
 */
namespace Pathfinding
{
/**
 * BFS shortest path from @p start to @p goal, or empty if unreachable.
 * Returns the inclusive sequence start -> ... -> goal.
 */
[[nodiscard]] std::vector<glm::ivec2> FindPath(const Tilemap& tilemap,
                                               glm::ivec2 start,
                                               glm::ivec2 goal);

/**
 * Count the navigable tiles reachable from @p start. Writes the bounding
 * box of reachable tiles into @p outBoundsMin / @p outBoundsMax when count
 * > 0. Bounds are tile coordinates, both inclusive.
 */
[[nodiscard]] std::size_t FloodReachable(const Tilemap& tilemap,
                                         glm::ivec2 start,
                                         glm::ivec2& outBoundsMin,
                                         glm::ivec2& outBoundsMax);
}  // namespace Pathfinding
