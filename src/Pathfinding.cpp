#include "Pathfinding.h"

#include "Tilemap.h"

#include <algorithm>
#include <deque>
#include <glm/glm.hpp>
#include <queue>

namespace Pathfinding
{
namespace
{
constexpr glm::ivec2 kDirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

bool InBounds(const Tilemap& m, glm::ivec2 p)
{
    return p.x >= 0 && p.y >= 0 && p.x < m.GetMapWidth() && p.y < m.GetMapHeight();
}

bool Navigable(const Tilemap& m, glm::ivec2 p)
{
    return InBounds(m, p) && m.GetNavigation(p.x, p.y);
}

std::size_t FlatIdx(const Tilemap& m, glm::ivec2 p)
{
    return static_cast<std::size_t>(p.y) * static_cast<std::size_t>(m.GetMapWidth()) +
           static_cast<std::size_t>(p.x);
}
}  // namespace

std::vector<glm::ivec2> FindPath(const Tilemap& tilemap, glm::ivec2 start, glm::ivec2 goal)
{
    if (!Navigable(tilemap, start) || !Navigable(tilemap, goal))
    {
        return {};
    }
    if (start == goal)
    {
        return {start};
    }

    const std::size_t cells = tilemap.MapCellCount();
    std::vector<bool> visited(cells, false);
    std::vector<std::size_t> prev(cells, static_cast<std::size_t>(-1));

    std::queue<glm::ivec2> q;
    q.push(start);
    visited[FlatIdx(tilemap, start)] = true;

    while (!q.empty())
    {
        glm::ivec2 cur = q.front();
        q.pop();
        if (cur == goal)
        {
            break;
        }
        for (auto d : kDirs)
        {
            glm::ivec2 n = cur + d;
            if (!Navigable(tilemap, n))
            {
                continue;
            }
            std::size_t ni = FlatIdx(tilemap, n);
            if (visited[ni])
            {
                continue;
            }
            visited[ni] = true;
            prev[ni] = FlatIdx(tilemap, cur);
            q.push(n);
        }
    }

    if (!visited[FlatIdx(tilemap, goal)])
    {
        return {};
    }

    std::vector<glm::ivec2> path;
    const int w = tilemap.GetMapWidth();
    for (std::size_t i = FlatIdx(tilemap, goal); i != static_cast<std::size_t>(-1); i = prev[i])
    {
        path.push_back({static_cast<int>(i % static_cast<std::size_t>(w)),
                        static_cast<int>(i / static_cast<std::size_t>(w))});
        if (i == FlatIdx(tilemap, start))
        {
            break;
        }
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::size_t FloodReachable(const Tilemap& tilemap,
                           glm::ivec2 start,
                           glm::ivec2& outBoundsMin,
                           glm::ivec2& outBoundsMax)
{
    if (!Navigable(tilemap, start))
    {
        return 0;
    }

    const std::size_t cells = tilemap.MapCellCount();
    std::vector<bool> visited(cells, false);

    std::queue<glm::ivec2> q;
    q.push(start);
    visited[FlatIdx(tilemap, start)] = true;

    outBoundsMin = start;
    outBoundsMax = start;
    std::size_t count = 0;

    while (!q.empty())
    {
        glm::ivec2 cur = q.front();
        q.pop();
        ++count;
        outBoundsMin.x = std::min(outBoundsMin.x, cur.x);
        outBoundsMin.y = std::min(outBoundsMin.y, cur.y);
        outBoundsMax.x = std::max(outBoundsMax.x, cur.x);
        outBoundsMax.y = std::max(outBoundsMax.y, cur.y);
        for (auto d : kDirs)
        {
            glm::ivec2 n = cur + d;
            if (!Navigable(tilemap, n))
            {
                continue;
            }
            std::size_t ni = FlatIdx(tilemap, n);
            if (visited[ni])
            {
                continue;
            }
            visited[ni] = true;
            q.push(n);
        }
    }
    return count;
}
}  // namespace Pathfinding
