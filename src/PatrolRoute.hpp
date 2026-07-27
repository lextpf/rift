#pragma once

#include <array>
#include <glm/glm.hpp>
#include <vector>

class Tilemap;

/**
 * @class PatrolRoute
 * @brief Generates and manages patrol paths for NPCs.
 * @author Fable 5 (https://github.com/claude)
 * @ingroup World
 *
 * PatrolRoute uses graph traversal algorithms to create movement paths
 * over connected walkable tiles, capped by the requested maximum route
 * length. Routes automatically detect whether they can form closed loops
 * or require ping-pong traversal.
 *
 * @par Algorithm selection
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
 *     classDef fail fill:#4c1d24,stroke:#ef4444,color:#e2e8f0
 *
 *     A[Initialize]:::start --> A2{"Tilemap non-null and<br/>start tile walkable?"}:::decision
 *     A2 -->|No| X["Return false<br/>route left untouched"]:::fail
 *     A2 -->|Yes| B[BFS: Collect<br/>Reachable Tiles]:::process
 *     B --> C{"3+ tiles AND every tile has<br/>exactly 2 BFS-reached neighbors?"}:::decision
 *     C -->|Yes| D[Simple Cycle<br/>Walk the ring]:::result
 *     C -->|No| E[DFS with Backtracking<br/>Visit collected tiles]:::result
 *     D --> F[Loop Mode<br/>A-B-C-A-B-C]:::result
 *     E --> G{"End adjacent to<br/>or equal to start?"}:::decision
 *     G -->|Yes| F
 *     G -->|No| H[Ping-Pong Mode<br/>A-B-C-B-A]:::result
 *     F --> W{"At least 2<br/>waypoints?"}:::decision
 *     H --> W
 *     W -->|Yes| R["Return true"]:::result
 *     W -->|No| Y["Clear waypoints<br/>Return false"]:::fail
 * </pre>
 * @endhtmlonly
 *
 * @par Cycle detection
 * A simple cycle is detected when at least 3 tiles were collected and every
 * one of them has exactly 2 neighbors the BFS reached. This forms a ring that
 * can be walked without backtracking:
 *
 * @verbatim
 *   Simple Cycle (loop mode):      Not a Cycle (DFS branch):
 *
 *       A - B                           A - B
 *       |   |                               |
 *       D - C                               C
 *
 *   A connects to B and D              B connects to A and C
 *   All tiles have 2 neighbors         A has only 1 neighbor
 * @endverbatim
 *
 * The DFS branch is not a synonym for ping-pong mode. A DFS that runs to completion unwinds
 * back to its start tile, so the last waypoint equals the first, @ref IsClosed reports true,
 * and the route runs as a loop whose first and last waypoints are the same tile. Ping-pong
 * is reached only when `maxRouteLength` truncates the DFS mid-descent, leaving a last
 * waypoint that is neither the start nor adjacent to it.
 *
 * The neighbor test uses the BFS visited mask, which marks a tile when it is enqueued. Once
 * `maxRouteLength` stops the BFS, that mask is a strict superset of the collected tiles, so a
 * ring longer than the cap still passes the every-tile-has-2-neighbors test: the route is
 * flagged closed and its wrap step from the last waypoint to the first is not adjacent.
 *
 * @par DFS spanning tree traversal
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
 *         P1[A] --> P2[B] --> P3[D] --> P4["B*"]:::backtrack
 *         P4 --> P5[C] --> P6["B*"]:::backtrack --> P7["A*"]:::backtrack
 *     end
 * </pre>
 * @endhtmlonly
 *
 * The asterisk (*) marks backtrack steps where the NPC returns
 * through previously visited tiles. C is the left arm of the T and D the right arm; D is
 * expanded first because neighbors are probed Right, Left, Down, Up.
 *
 * @par Traversal modes
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
 * @par Walkability is stricter here than in Pathfinding
 * @ref IsValidTile requires navigation and the absence of collision, whereas
 * Pathfinding::FindPath accepts any navigable tile. A tile carrying both flags - a rock
 * dropped on an authored patrol lane - is therefore routable by the console's `nav.path`
 * but unusable as a waypoint. The two are not interchangeable, and a reported path is not
 * evidence that a patrol can follow it.
 *
 * @par Time complexity
 * @f[
 * T_{Initialize} = \Theta(W \cdot H)
 * @f]
 * - **Initialize**: dominated by whole-map bookkeeping, not by `maxRouteLength`. A visited
 *   mask of `W * H` entries is allocated up front, and the non-cycle branch clears the
 *   whole mask again before the DFS. Both costs are paid even for a 3-tile route.
 *   - BFS collects at most `maxRouteLength` tiles, with deque front removal and O(1)
 *     visited-array membership.
 *   - Cycle detection re-queries neighbors per collected tile: O(min(V, maxRouteLength)).
 *   - DFS backtrack steps are linear in the generated waypoint count.
 * - **GetNextWaypoint**: O(1)
 *
 * @par Space complexity
 * - `Theta(W * H)` for the visited mask. The cycle branch allocates a second full-map mask
 *   (`cycleVisited`), so peak space is two whole-map masks - again independent of
 *   `maxRouteLength`.
 * - O(maxRouteLength) for stored waypoints; backtracks are what push a `V`-tile region
 *   toward that bound.
 *
 * @see NpcAiSystem, Pathfinding, Tilemap
 */
class PatrolRoute
{
public:
    /// @brief Default constructor creates an empty (invalid) route.
    PatrolRoute() = default;

    /**
     * @brief Generate a patrol route from a starting tile.
     *
     * Uses BFS to collect reachable tiles, then determines the optimal
     * traversal strategy (cycle walk or DFS with backtracking).
     *
     * Fails when @p tilemap is null, when the start tile is not walkable by @ref IsValidTile,
     * when the map has a degenerate or overflowing size, or when the region yields fewer than
     * 2 waypoints. The first two checks return before anything is written, so a route that was
     * already valid keeps its waypoints, index and mode; the later two leave the route cleared.
     * A caller that needs a guaranteed-invalid route after a failure must call @ref Reset first,
     * as NpcAiSystem::ReinitializePatrolRoute does.
     *
     * @pre @p maxRouteLength >= 1.
     *
     * @param startTileX Starting tile column.
     * @param startTileY Starting tile row.
     * @param tilemap Tilemap for navigation queries; null fails immediately.
     * @param maxRouteLength Upper bound for collected route tiles/steps (default: 100).
     *   Because BFS expands in rings, hitting this bound yields a compact cluster around
     *   the start rather than a long tendril. It bounds the waypoint count, not the
     *   working memory - that scales with the map, not with this. A value of 0 or less is
     *   cast to an unsigned bound, which removes the cap instead of tightening it: the BFS
     *   then floods the whole connected region.
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
     * @brief Read-only access to the generated waypoint list.
     *
     * Used by the developer-console `npc.path` command for inspection.
     */
    [[nodiscard]] const std::vector<glm::ivec2>& GetWaypoints() const { return m_Waypoints; }

    /// @brief Clear all waypoints and return the route to an invalid default state.
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
     * Unlike its siblings @ref IsValidTile and @ref GetValidNeighbors, this helper does not
     * defend itself: it dereferences @p tilemap and indexes @p visited without checks.
     *
     * @pre @p tilemap is non-null, @p visited holds exactly mapWidth * mapHeight entries, and
     * @p current is in bounds and walkable.
     *
     * @param current Current tile being visited.
     * @param visited Set of already-visited tiles.
     * @param path Output path accumulator; appended to, never cleared, so the caller owns any
     *   entries already in it.
     * @param tilemap Tilemap for neighbor queries.
     * @param maxLength Maximum path length.
     */
    void DFSTraversal(glm::ivec2 current,
                      std::vector<bool>& visited,
                      std::vector<glm::ivec2>& path,
                      const Tilemap* tilemap,
                      size_t maxLength);

    /// @brief Result of a neighbor query: up to 4 cardinal neighbors.
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
     * @brief Check if a tile is walkable for patrol purposes.
     *
     * Requires both the navigation flag (authored "NPCs may walk here") and the absence of
     * a collision flag, so a solid object placed on a navigable lane removes that tile from
     * every route. Out-of-bounds and a null tilemap are not walkable.
     *
     * This is strictly stronger than Pathfinding's navigation-only test; see the class
     * overview.
     *
     * @param tileX Tile column.
     * @param tileY Tile row.
     * @param tilemap Tilemap for navigation queries.
     * @return `true` if the tile is in bounds, navigable, and free of collision.
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
