#include "PatrolRoute.h"
#include "Tilemap.h"

#include <algorithm>
#include <deque>
#include <iostream>

bool PatrolRoute::Initialize(int startTileX,
                             int startTileY,
                             const Tilemap* tilemap,
                             int maxRouteLength)
{
    if (!tilemap)
    {
        std::cerr << "PatrolRoute::Initialize: tilemap is null" << std::endl;
        return false;
    }

    if (!IsValidTile(startTileX, startTileY, tilemap))
    {
        std::cerr << "PatrolRoute::Initialize: Starting tile (" << startTileX << ", " << startTileY
                  << ") is not walkable" << std::endl;
        return false;
    }

    m_Waypoints.clear();
    m_CurrentWaypointIndex = 0;
    m_PingPongForward = true;
    m_IsClosed = false;

    int mapWidth = tilemap->GetMapWidth();
    int mapHeight = tilemap->GetMapHeight();

    // Use breadth-first search to collect all walkable tiles reachable from the start.
    // BFS explores in expanding rings outward, so tiles closer to start are found first.
    // This means if we hit maxRouteLength, we get a compact cluster around the start
    // rather than a long tendril in one random direction.
    std::vector<glm::ivec2> connectedTiles;
    std::vector<bool> visited(static_cast<size_t>(mapWidth) * static_cast<size_t>(mapHeight),
                              false);
    std::deque<glm::ivec2> bfsQueue;

    glm::ivec2 start(startTileX, startTileY);
    bfsQueue.push_back(start);
    visited[startTileY * mapWidth + startTileX] = true;

    while (!bfsQueue.empty() && connectedTiles.size() < static_cast<size_t>(maxRouteLength))
    {
        // Pop from front (FIFO) - this is what makes it BFS instead of DFS.
        // If we popped from back, closer tiles would be processed last.
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
    // Example of a simple cycle:     Example of NOT a cycle:
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
                // Check if this neighbor is part of our collected set
                // using the visited array for O(1) lookup instead of linear search.
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
        // For a cycle, we walk around the ring by always picking the unvisited neighbor.
        // Since each tile has exactly 2 neighbors in the set, and we mark tiles visited
        // as we go, there's always exactly one valid choice (until we complete the loop).
        std::vector<bool> cycleVisited(
            static_cast<size_t>(mapWidth) * static_cast<size_t>(mapHeight), false);
        glm::ivec2 current = start;
        glm::ivec2 prev(-1, -1);

        while (m_Waypoints.size() < connectedTiles.size())
        {
            m_Waypoints.push_back(current);
            cycleVisited[current.y * mapWidth + current.x] = true;

            // Find the next tile: must be in our set and not yet visited.
            // Use the visited array for O(1) set membership instead of linear search.
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
        std::cerr << "PatrolRoute::Initialize: Route too short (" << m_Waypoints.size()
                  << " waypoints)" << std::endl;
        m_Waypoints.clear();
        return false;
    }

    std::cout << "Created patrol route: " << m_Waypoints.size()
              << " waypoints, mode=" << (m_IsClosed ? "loop" : "ping-pong") << ", start=("
              << startTileX << ", " << startTileY << ")" << std::endl;

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
    // neighbor to explore. When all neighbors are exhausted, we pop the
    // frame and add a backtrack step (re-adding the parent tile to the
    // path so the NPC walks back through it without teleporting).
    //
    // Example on a T-shaped map:
    //       A
    //       |
    //   C - B - D
    //
    // DFS visits: A, then B, then C (dead end, backtrack to B),
    // then D (dead end, backtrack to B), then back to A.
    // Path produced: [A, B, C, B, D, B, A]
    struct Frame
    {
        glm::ivec2 tile;
        NeighborResult neighbors;
        int nextNeighbor;
    };

    int mapWidth = tilemap->GetMapWidth();

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
                // repeating the startpoint twice -- unless there's only 1 waypoint,
                // in which case we stay at index 0 to prevent out-of-bounds access.
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
    // if we always check Right, Left, Down, Up in that order, then the same
    // map will always produce the same patrol route.
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
