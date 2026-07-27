#include "PatrolRoute.hpp"

#include "Logger.hpp"
#include "Tilemap.hpp"

#include <algorithm>
#include <deque>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "NPC";
}  // namespace

bool PatrolRoute::Initialize(int startTileX,
                             int startTileY,
                             const Tilemap* tilemap,
                             int maxRouteLength)
{
    if (!tilemap)
    {
        Logger::Error(LOG_SUBSYSTEM, "PatrolRoute::Initialize: tilemap is null");
        return false;
    }

    if (!IsValidTile(startTileX, startTileY, tilemap))
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "PatrolRoute::Initialize: Starting tile ({}, {}) is not walkable",
                       startTileX,
                       startTileY);
        return false;
    }

    m_Waypoints.clear();
    m_CurrentWaypointIndex = 0;
    m_PingPongForward = true;
    m_IsClosed = false;

    int mapWidth = tilemap->GetMapWidth();
    int mapHeight = tilemap->GetMapHeight();

    if (mapWidth <= 0 || mapHeight <= 0)
        return false;

    size_t mapSize = static_cast<size_t>(mapWidth) * static_cast<size_t>(mapHeight);
    if (mapSize / static_cast<size_t>(mapWidth) != static_cast<size_t>(mapHeight))
        return false;  // overflow

    // Use breadth-first search to collect all walkable tiles reachable from the start.
    // BFS explores in expanding rings outward, so tiles closer to start are found first.
    // So reaching maxRouteLength yields a compact cluster around the start rather than a
    // long tendril in one random direction.
    std::vector<glm::ivec2> connectedTiles;
    std::vector<bool> visited(mapSize, false);
    std::deque<glm::ivec2> bfsQueue;

    glm::ivec2 start(startTileX, startTileY);
    bfsQueue.push_back(start);
    visited[startTileY * mapWidth + startTileX] = true;

    while (!bfsQueue.empty() && connectedTiles.size() < static_cast<size_t>(maxRouteLength))
    {
        // Pop from front (FIFO) - this is what makes it BFS instead of DFS.
        // Popping from the back would process closer tiles last.
        glm::ivec2 current = bfsQueue.front();
        bfsQueue.pop_front();
        connectedTiles.push_back(current);

        auto neighbors = GetValidNeighbors(current.x, current.y, tilemap);
        for (int ni = 0; ni < neighbors.count; ++ni)
        {
            const auto& neighbor = neighbors.tiles[ni];
            int idx = neighbor.y * mapWidth + neighbor.x;
            if (!visited[idx])
            {
                visited[idx] = true;
                bfsQueue.push_back(neighbor);
            }
        }
    }

    // Detect if the connected tiles form a simple cycle (ring shape).
    // A simple cycle has a special property: every tile has exactly 2 neighbors
    // that are also in the set. Think of it like a necklace - each bead touches
    // exactly 2 other beads.
    //
    // Example of a simple cycle:     Example of not a cycle:
    //     A - B                           A - B
    //     |   |                               |
    //     D - C                               C
    //
    // In the cycle, A connects to B and D, B connects to A and C, etc.
    // In the non-cycle, B connects to A and C, but A only connects to B.
    bool isSimpleCycle = connectedTiles.size() >= 3;
    if (isSimpleCycle)
    {
        for (const auto& tile : connectedTiles)
        {
            int neighborCount = 0;
            auto neighbors = GetValidNeighbors(tile.x, tile.y, tilemap);
            for (int ni = 0; ni < neighbors.count; ++ni)
            {
                const auto& neighbor = neighbors.tiles[ni];
                // Check whether the BFS reached this neighbor, using the visited array for
                // O(1) lookup instead of a linear search of connectedTiles. Note that
                // `visited` marks a tile when it is enqueued, so once the maxRouteLength
                // bound stops the loop above it is a strict superset of connectedTiles: the
                // whole unprocessed frontier is marked. Consequence: a ring longer than
                // maxRouteLength still passes this test, so the truncated route is flagged
                // closed and its wrap step from the last waypoint to the first is not
                // adjacent.
                if (visited[neighbor.y * mapWidth + neighbor.x])
                {
                    neighborCount++;
                }
            }

            // Any tile with != 2 neighbors in the set breaks the cycle property.
            // A tile with 1 neighbor is a dead end. A tile with 3+ is a junction.
            if (neighborCount != 2)
            {
                isSimpleCycle = false;
                break;
            }
        }
    }

    if (isSimpleCycle)
    {
        // A cycle is walked by always picking the unvisited neighbor. Each tile has exactly
        // 2 neighbors in the set, and tiles are marked visited on the way, so exactly one
        // valid choice exists until the loop closes.
        std::vector<bool> cycleVisited(
            static_cast<size_t>(mapWidth) * static_cast<size_t>(mapHeight), false);
        glm::ivec2 current = start;
        glm::ivec2 prev(-1, -1);

        while (m_Waypoints.size() < connectedTiles.size())
        {
            m_Waypoints.push_back(current);
            cycleVisited[current.y * mapWidth + current.x] = true;

            // Find the next tile: the BFS must have reached it and the walk must not have
            // taken it yet. `visited` gives O(1) membership instead of a linear search, but
            // it also covers the unprocessed BFS frontier, so on a truncated region the walk
            // can step onto tiles that are not in connectedTiles.
            auto neighbors = GetValidNeighbors(current.x, current.y, tilemap);
            glm::ivec2 next(-1, -1);
            for (int ni = 0; ni < neighbors.count; ++ni)
            {
                const auto& neighbor = neighbors.tiles[ni];
                int nIdx = neighbor.y * mapWidth + neighbor.x;
                if (visited[nIdx] && !cycleVisited[nIdx])
                {
                    next = neighbor;
                    break;
                }
            }

            if (next.x == -1)
            {
                break;
            }

            prev = current;
            current = next;
        }

        // Closed loop means NPC walks: 0 -> 1 -> 2 -> ... -> N-1 -> 0 -> 1 -> ...
        m_IsClosed = true;
    }
    else
    {
        // Not a cycle, so use depth-first search with backtracking.
        // DFS explores as deep as possible before backtracking, which produces
        // a path that visits all tiles but includes "return trips" back through
        // already-visited tiles. This makes the path contiguous (no teleporting).
        std::fill(visited.begin(), visited.end(), false);
        DFSTraversal(start, visited, m_Waypoints, tilemap, static_cast<size_t>(maxRouteLength));

        // Even non-cycles might loop back if the last tile is next to the first.
        if (m_Waypoints.size() >= 2)
        {
            const glm::ivec2& first = m_Waypoints.front();
            const glm::ivec2& last = m_Waypoints.back();
            m_IsClosed = AreAdjacent(last, first) || (last == first);
        }
    }

    if (m_Waypoints.size() < 2)
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "PatrolRoute::Initialize: Route too short ({} waypoints)",
                       m_Waypoints.size());
        m_Waypoints.clear();
        return false;
    }

    Logger::InfoF(LOG_SUBSYSTEM,
                  "Created patrol route: {} waypoints, mode={}, start=({}, {})",
                  m_Waypoints.size(),
                  m_IsClosed ? "loop" : "ping-pong",
                  startTileX,
                  startTileY);

    return true;
}

void PatrolRoute::DFSTraversal(glm::ivec2 current,
                               std::vector<bool>& visited,
                               std::vector<glm::ivec2>& path,
                               const Tilemap* tilemap,
                               size_t maxLength)
{
    // Iterative DFS with explicit stack to avoid stack overflow on large
    // connected regions. Recursive DFS would use O(V) call frames which
    // can exceed the default 1 MB stack on Windows for maps with 10,000+
    // walkable tiles.
    //
    // Each stack frame stores the current tile and the index of the next
    // neighbor to explore. When all neighbors are exhausted the frame pops and a
    // backtrack step is appended, re-adding the parent tile so the NPC walks back
    // through it without teleporting.
    //
    // Example on a T-shaped map:
    //       A
    //       |
    //   C - B - D
    //
    // DFS visits: A, then B, then D (dead end, backtrack to B),
    // then C (dead end, backtrack to B), then back to A.
    // D comes before C because neighbors are probed Right, Left, Down, Up.
    // Path produced: [A, B, D, B, C, B, A]
    //
    // A traversal that runs to completion always ends on its start tile, which is why
    // Initialize flags such a route closed rather than ping-pong.
    struct Frame
    {
        glm::ivec2 tile;
        NeighborResult neighbors;
        int nextNeighbor;
    };

    int mapWidth = tilemap->GetMapWidth();
    if (mapWidth <= 0)
        return;

    std::vector<Frame> stack;
    stack.reserve(64);

    // Seed the traversal with the starting tile.
    int startIndex = current.y * mapWidth + current.x;
    visited[startIndex] = true;
    path.push_back(current);

    Frame startFrame;
    startFrame.tile = current;
    startFrame.neighbors = GetValidNeighbors(current.x, current.y, tilemap);
    startFrame.nextNeighbor = 0;
    stack.push_back(startFrame);

    while (!stack.empty() && path.size() < maxLength)
    {
        Frame& frame = stack.back();

        // Find the next unvisited neighbor from this tile.
        bool foundChild = false;
        while (frame.nextNeighbor < frame.neighbors.count)
        {
            const auto& neighbor = frame.neighbors.tiles[frame.nextNeighbor];
            frame.nextNeighbor++;

            int neighborIndex = neighbor.y * mapWidth + neighbor.x;
            if (!visited[neighborIndex] && path.size() < maxLength)
            {
                // Push the neighbor as a new frame and record it in the path.
                visited[neighborIndex] = true;
                path.push_back(neighbor);

                Frame childFrame;
                childFrame.tile = neighbor;
                childFrame.neighbors = GetValidNeighbors(neighbor.x, neighbor.y, tilemap);
                childFrame.nextNeighbor = 0;
                stack.push_back(childFrame);
                foundChild = true;
                break;
            }
        }

        if (!foundChild)
        {
            // All neighbors of this tile have been explored. Pop the frame
            // and add a backtrack step so the path remains contiguous.
            stack.pop_back();
            if (!stack.empty() && path.size() < maxLength)
            {
                path.push_back(stack.back().tile);
            }
        }
    }
}

bool PatrolRoute::GetNextWaypoint(int& tileX, int& tileY)
{
    if (m_Waypoints.empty())
    {
        return false;
    }

    const auto& waypoint = m_Waypoints[m_CurrentWaypointIndex];
    tileX = waypoint.x;
    tileY = waypoint.y;

    if (m_IsClosed)
    {
        // Closed loop: wrap around using modulo.
        // Index goes 0, 1, 2, ..., N-1, 0, 1, 2, ... forever.
        m_CurrentWaypointIndex =
            (m_CurrentWaypointIndex + 1) % static_cast<int>(m_Waypoints.size());
    }
    else
    {
        // Ping-pong mode: walk forward to the end, then backward to the start, repeat.
        // Index goes 0, 1, 2, ..., N-1, N-2, ..., 1, 0, 1, 2, ... forever.
        if (m_PingPongForward)
        {
            m_CurrentWaypointIndex++;
            if (m_CurrentWaypointIndex >= static_cast<int>(m_Waypoints.size()))
            {
                // Reached the end, turn around. Go to N-2 (not N-1) to avoid
                // repeating the endpoint twice.
                m_CurrentWaypointIndex = static_cast<int>(m_Waypoints.size()) - 2;
                if (m_CurrentWaypointIndex < 0)
                {
                    m_CurrentWaypointIndex = 0;
                }
                m_PingPongForward = false;
            }
        }
        else
        {
            m_CurrentWaypointIndex--;
            if (m_CurrentWaypointIndex < 0)
            {
                // Reached the start, turn around. Go to 1 (not 0) to avoid
                // repeating the startpoint twice - unless there's only 1 waypoint,
                // in which case the index stays at 0 to prevent out-of-bounds access.
                m_CurrentWaypointIndex = (static_cast<int>(m_Waypoints.size()) > 1) ? 1 : 0;
                m_PingPongForward = true;
            }
        }
    }

    return true;
}

PatrolRoute::NeighborResult PatrolRoute::GetValidNeighbors(int tileX,
                                                           int tileY,
                                                           const Tilemap* tilemap) const
{
    NeighborResult result;

    if (!tilemap)
    {
        return result;
    }

    // Check the 4 cardinal directions. The order matters for determinism:
    // checking Right, Left, Down, Up in that fixed order makes the same map always
    // produce the same patrol route.
    const int dx[] = {1, -1, 0, 0};
    const int dy[] = {0, 0, 1, -1};

    for (int i = 0; i < 4; ++i)
    {
        int nx = tileX + dx[i];
        int ny = tileY + dy[i];

        if (IsValidTile(nx, ny, tilemap))
        {
            result.tiles[result.count++] = glm::ivec2(nx, ny);
        }
    }

    return result;
}

bool PatrolRoute::IsValidTile(int tileX, int tileY, const Tilemap* tilemap) const
{
    if (!tilemap)
    {
        return false;
    }

    int mapW = tilemap->GetMapWidth();
    int mapH = tilemap->GetMapHeight();

    if (tileX < 0 || tileY < 0 || tileX >= mapW || tileY >= mapH)
    {
        return false;
    }

    // Navigation flag indicates "NPCs can walk here". This is set manually
    // in the editor to define patrol areas.
    if (!tilemap->GetNavigation(tileX, tileY))
    {
        return false;
    }

    // Collision flag indicates "solid obstacle". Even if navigation is set,
    // a tile with collision is blocked (e.g., a rock placed on a path).
    if (tilemap->GetTileCollision(tileX, tileY))
    {
        return false;
    }

    return true;
}

bool PatrolRoute::AreAdjacent(const glm::ivec2& a, const glm::ivec2& b) const
{
    // Two tiles are adjacent if they differ by exactly 1 in X or Y, but not both.
    // This is Manhattan distance == 1, which corresponds to the 4 cardinal directions.
    // Diagonal tiles (Manhattan distance == 2) are not considered adjacent.
    int dx = std::abs(a.x - b.x);
    int dy = std::abs(a.y - b.y);
    return (dx + dy) == 1;
}
