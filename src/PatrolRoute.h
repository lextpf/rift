#pragma once

#include <array>
#include <glm/glm.hpp>
#include <vector>

class Tilemap;

/**
 * @class PatrolRoute
 * @brief Generates and manages patrol paths for NPCs.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * PatrolRoute uses graph traversal algorithms to create movement paths
 * over connected walkable tiles, capped by the requested maximum route
 * length. Routes automatically detect whether they can form closed loops
 * or require ping-pong traversal.
 *
 * @par Algorithm Selection
 * The initialization process first collects reachable tiles using BFS,
 * then determines the optimal traversal strategy:
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 *     classDef start fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *     classDef decision fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 *     classDef process fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef result fill:#2e1f5e,stroke:#8b5cf6,color:#e2e8f0
 *
 *     A[Initialize]:::start --> B[BFS: Collect<br/>Reachable Tiles]:::process
 *     B --> C{All tiles have<br/>exactly 2 neighbors?}:::decision
 *     C -->|Yes| D[Simple Cycle<br/>Walk the ring]:::result
 *     C -->|No| E[DFS with Backtracking<br/>Visit collected tiles]:::result
 *     D --> F[Loop Mode<br/>A-B-C-A-B-C]:::result
 *     E --> G{End adjacent<br/>to start?}:::decision
 *     G -->|Yes| F
 *     G -->|No| H[Ping-Pong Mode<br/>A-B-C-B-A]:::result
 * </pre>
 * @endhtmlonly
 *
 * @par Cycle Detection
 * A simple cycle is detected when every tile has exactly 2 neighbors
 * within the connected set. This forms a ring that can be walked
 * without backtracking:
 *
 * @verbatim
 *   Simple Cycle (loop mode):      Not a Cycle (ping-pong mode):
 *
 *       A - B                           A - B
 *       |   |                               |
 *       D - C                               C
 *
 *   A connects to B and D              B connects to A and C
 *   All tiles have 2 neighbors         A has only 1 neighbor
 * @endverbatim
 *
 * @par DFS Spanning Tree Traversal
 * For non-cyclic routes, DFS with backtracking ensures every tile
 * is visited in a contiguous path (no teleporting):
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 *     classDef visited fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef backtrack fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 *
 *     subgraph "T-Shaped Map"
 *         direction LR
 *         T1[A]:::visited
 *         T2[B]:::visited
 *         T3[C]:::visited
 *         T4[D]:::visited
 *         T1 --- T2
 *         T3 --- T2
 *         T2 --- T4
 *     end
 *
 *     subgraph "Generated Path"
 *         direction LR
 *         P1[A] --> P2[B] --> P3[C] --> P4["B*"]:::backtrack
 *         P4 --> P5[D] --> P6["B*"]:::backtrack --> P7["A*"]:::backtrack
 *     end
 * </pre>
 * @endhtmlonly
 *
 * The asterisk (*) marks backtrack steps where the NPC returns
 * through previously visited tiles.
 *
 * @par Traversal Modes
 *
 * @htmlonly
 * <pre class="mermaid">
 * stateDiagram-v2
 *     direction LR
 *
 *     classDef loop fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef pingpong fill:#2e1f5e,stroke:#8b5cf6,color:#e2e8f0
 *     classDef waypoint fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *
 *     state "Loop Mode" as Loop {
 *         direction LR
 *         L0: Waypoint 0
 *         L1: Waypoint 1
 *         L2: Waypoint 2
 *         LN: Waypoint N-1
 *         L0 --> L1
 *         L1 --> L2
 *         L2 --> LN: ...
 *         LN --> L0: wrap
 *     }
 *
 *     state "Ping-Pong Mode" as PingPong {
 *         direction LR
 *         P0: Waypoint 0
 *         P1: Waypoint 1
 *         PN: Waypoint N-1
 *         P0 --> P1: forward
 *         P1 --> PN: ...
 *         PN --> P1: reverse
 *         P1 --> P0: ...
 *     }
 *
 *     class Loop loop
 *     class PingPong pingpong
 * </pre>
 * @endhtmlonly
 *
 * - **Loop Mode**: Index wraps: 0, 1, 2, ..., N-1, 0, 1, ...
 * - **Ping-Pong Mode**: Index bounces: 0, 1, ..., N-1, N-2, ..., 1, 0, 1, ...
 *
 * @par Time Complexity
 * - **Initialize**: worst-case O(V^2) with current implementation
 *   - BFS tile discovery is O(V), but queue front-erasure is linear
 *   - Cycle detection uses linear membership checks over connected tiles
 *   - DFS backtrack steps are bounded by O(V)
 * - **GetNextWaypoint**: O(1)
 *
 * @par Space Complexity
 * - O(V) for visited state during traversal
 * - O(2V) worst case for stored waypoints (full backtracks)
 *
 * @see NonPlayerCharacter, Tilemap
 */
class PatrolRoute
{
public:
    /**
     * @brief Default constructor creates an empty (invalid) route.
     */
    PatrolRoute() = default;

    /**
     * @brief Generate a patrol route from a starting tile.
     *
     * Uses BFS to collect reachable tiles, then determines the optimal
     * traversal strategy (cycle walk or DFS with backtracking).
     *
     * @param startTileX Starting tile column.
     * @param startTileY Starting tile row.
     * @param tilemap Tilemap for navigation queries.
     * @param maxRouteLength Upper bound for collected route tiles/steps (default: 100).
     * @return `true` if valid route created (>= 2 waypoints).
     */
    bool Initialize(int startTileX,
                    int startTileY,
                    const Tilemap* tilemap,
                    int maxRouteLength = 100);

    /**
     * @brief Get the next waypoint and advance iteration.
     *
     * Returns current target and moves to next waypoint.
     * Behavior depends on route mode (loop vs ping-pong).
     *
     * @param[out] tileX Target tile column.
     * @param[out] tileY Target tile row.
     * @return `true` if valid waypoint returned.
     */
    bool GetNextWaypoint(int& tileX, int& tileY);

    /**
     * @brief Get current waypoint index.
     * @return Index in waypoint array (0-based).
     */
    int GetCurrentWaypointIndex() const { return m_CurrentWaypointIndex; }

    /**
     * @brief Check if route is valid (has waypoints).
     * @return `true` if route has at least one waypoint.
     */
    bool IsValid() const { return !m_Waypoints.empty(); }

    /**
     * @brief Check if route uses closed loop mode.
     * @return `true` if loop, `false` if ping-pong.
     */
    bool IsClosed() const { return m_IsClosed; }

    /**
     * @brief Get total number of waypoints.
     * @return Waypoint count.
     */
    size_t GetWaypointCount() const { return m_Waypoints.size(); }

    /**
     * @brief Reset iteration to first waypoint.
     */
    void Reset()
    {
        m_Waypoints.clear();
        m_CurrentWaypointIndex = 0;
        m_IsClosed = false;
        m_PingPongForward = true;
    }

private:
    /**
     * @brief DFS traversal that records full path including backtracks.
     *
     * @param current Current tile being visited.
     * @param visited Set of already-visited tiles.
     * @param path Output path accumulator.
     * @param tilemap Tilemap for neighbor queries.
     * @param maxLength Maximum path length.
     */
    void DFSTraversal(glm::ivec2 current,
                      std::vector<bool>& visited,
                      std::vector<glm::ivec2>& path,
                      const Tilemap* tilemap,
                      size_t maxLength);

    /**
     * @brief Result of a neighbor query: up to 4 cardinal neighbors.
     */
    struct NeighborResult
    {
        std::array<glm::ivec2, 4> tiles;
        int count = 0;
    };

    /**
     * @brief Get 4-directional walkable neighbors.
     *
     * @param tileX Tile column.
     * @param tileY Tile row.
     * @param tilemap Tilemap for navigation queries.
     * @return Array of valid neighbor coordinates with count.
     */
    NeighborResult GetValidNeighbors(int tileX, int tileY, const Tilemap* tilemap) const;

    /**
     * @brief Check if a tile is walkable.
     *
     * @param tileX Tile column.
     * @param tileY Tile row.
     * @param tilemap Tilemap for navigation queries.
     * @return `true` if tile is walkable.
     */
    bool IsValidTile(int tileX, int tileY, const Tilemap* tilemap) const;

    /**
     * @brief Check if two tiles are adjacent (Manhattan distance = 1).
     * @param a First tile.
     * @param b Second tile.
     * @return `true` if cardinally adjacent.
     */
    bool AreAdjacent(const glm::ivec2& a, const glm::ivec2& b) const;

    std::vector<glm::ivec2> m_Waypoints;  ///< Patrol waypoints (includes backtracks).
    int m_CurrentWaypointIndex{0};        ///< Current position in waypoint array.
    bool m_IsClosed{false};               ///< True = loop mode, false = ping-pong.
    bool m_PingPongForward{true};         ///< Direction for ping-pong traversal.
};
