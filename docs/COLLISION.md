# Collision & Pathfinding

This document covers Rift's collision detection, physics, and NPC navigation systems.

## Collision System Overview

\htmlonly
<pre class="mermaid">
graph LR
    classDef map fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef entity fill:#134e3a,stroke:#10b981,color:#e2e8f0
    classDef query fill:#4a3520,stroke:#f59e0b,color:#e2e8f0

    subgraph World["World Data"]
        Tilemap["Tilemap"]:::map
        Collision["CollisionMap"]:::map
        Navigation["NavigationMap"]:::map
    end

    subgraph Entities["Entities"]
        Player["PlayerCharacter"]:::entity
        NPC["NonPlayerCharacter"]:::entity
    end

    subgraph Queries["Collision Queries"]
        Tile["Tile Collision"]:::query
        Entity["Entity vs Entity"]:::query
        Path["Pathfinding"]:::query
    end

    Tilemap --> Collision
    Tilemap --> Navigation
    Player --> Tile
    NPC --> Tile
    Player --> Entity
    NPC --> Entity
    NPC --> Path
    Tile --> Collision
    Path --> Navigation
</pre>
\endhtmlonly

## Tile-Based Collision

### Collision Map

Each tile in the world has a collision flag (true = blocked, false = passable):

```cpp
bool isBlocked = tilemap.GetCollision(tileX, tileY);
```

The collision map is a parallel array to the tile layers:

$$
collisionIndex = tileY \times mapWidth + tileX
$$

### Entity Hitboxes

All entities use **axis-aligned bounding boxes (AABB)** anchored at the bottom-center:

```
    +-------+
    |       |
    |  NPC  |  Height = 16px
    |       |
    +---*---+
        |
      (x,y)   Width = 16px
```

**Hitbox dimensions:**
- Width: 16 pixels (half-width = 8)
- Height: 16 pixels

### AABB Collision Test

Two AABBs overlap if and only if they overlap on both axes:

$$
overlap = (|x_a - x_b| < hw_a + hw_b) \land (|y_a - y_b| < h_a + h_b)
$$

Where:
- $(x_a, y_a)$, $(x_b, y_b)$ are bottom-center positions
- $hw$ is half-width
- $h$ is height (measured upward from anchor)

### Movement and Collision Resolution

When an entity moves, collision is checked against both the tile grid and other entities. Player
movement uses strict tile-overlap tests, then attempts axis-separated recovery so diagonal movement
can slide along walls instead of stopping outright:

\htmlonly
<pre class="mermaid">
flowchart TD
    A["Input velocity * dt"] --> B["Build target bottom-center"]
    B --> C{"Strict AABB overlaps blocked tiles?"}
    C -->|No| G["Accept target"]
    C -->|Yes| D["Try X-only target"]
    D --> E["Try Y-only target"]
    E --> F{"Any axis target clear?"}
    F -->|Yes| H["Slide along clear axis"]
    F -->|No| I["Keep previous safe position"]
    G --> J["Check player/NPC AABB overlap"]
    H --> J
    I --> J
    J --> K["Final resolved position"]
</pre>
\endhtmlonly

**Axis-Separated Movement:**

Movement is processed one axis at a time to allow sliding along walls:

```cpp
// Try X movement first
position.x += velocity.x * deltaTime;
if (CheckCollision(position)) {
    position.x = previousPosition.x;  // Revert X
}

// Then Y movement
position.y += velocity.y * deltaTime;
if (CheckCollision(position)) {
    position.y = previousPosition.y;  // Revert Y
}
```

This allows diagonal movement to slide along walls rather than stopping completely.

### Corner Handling

The collision system does not expose a single public "corner tolerance" value. It uses strict AABB
overlap against blocked tiles, then applies movement-context helpers for sliding and lane snapping.
The important invariant is that the final accepted position must not overlap any blocked tile:

$$
blocked(p) = \exists t \in overlappedTiles(p): AABB(p) \cap AABB(t) \neq \varnothing
$$

A diagonal move may still make progress when one axis is clear:

$$
accept(p_x, p_y) = \neg blocked(p_x, p_y) \lor \neg blocked(p_x, y_0) \lor \neg blocked(x_0, p_y)
$$

## Elevation System

Tiles can have elevation values that affect rendering (parallax) and potentially movement:

$$
elevation \in [0, 255] \text{ pixels}
$$

**Elevation Effects:**
- Higher elevation tiles render with vertical offset
- Creates illusion of height/depth
- Can restrict movement between elevation levels (optional)

## Navigation System

### Patrol Routes

NPCs follow predefined patrol routes generated from the navigation map:

\htmlonly
<pre class="mermaid">
stateDiagram-v2
    [*] --> Patrolling
    Patrolling --> Moving: Has waypoint
    Moving --> Waiting: Reached waypoint
    Waiting --> Patrolling: Wait timer done
    Patrolling --> Idle: No valid path
    Idle --> Patrolling: Recalculate
</pre>
\endhtmlonly

**Route Generation:**

Patrol routes are computed from the navigation map:

1. Start at the NPC spawn tile.
2. Use BFS to collect reachable walkable tiles, bounded by `maxRouteLength`.
3. If the reachable set forms a closed degree-2 cycle, walk it as a loop.
4. Otherwise, use DFS with backtracking so consecutive waypoints remain adjacent.

$$
route = [waypoint_0, waypoint_1, ..., waypoint_n]
$$

### Patrol Traversal

NPCs do not run dynamic A* pathfinding each frame. They follow the generated waypoint list, wait
briefly at waypoints, and recalculate routes when navigation tiles change or an NPC is placed.
Loop routes wrap from the last waypoint back to the first; non-loop routes ping-pong through the
list.

## NPC Behavior

### Movement State Machine

\htmlonly
<pre class="mermaid">
---
config:
  layout: elk
---
stateDiagram-v2
    [*] --> Idle

    Idle --> Walking: Has target
    Walking --> Idle: Reached target
    Walking --> Stopped: External stop
    Stopped --> Walking: Stop cleared

    state Idle {
        [*] --> Standing
        Standing --> LookingAround: Timer
        LookingAround --> Standing: Timer
    }
</pre>
\endhtmlonly

### Random Behaviors

NPCs exhibit idle behaviors when not patrolling:

| Behavior      | Trigger             | Duration                    |
|---------------|---------------------|-----------------------------|
| Stand Still   | Random (every 3-8s) | 2-5s                        |
| Look Around   | While standing      | Change direction every 1-3s |
| Resume Patrol | After standing      | Immediate                   |

**Standing Still Probability:**

$$
P(standStill) = 0.3
$$

Check interval: 3-8 seconds (random).

### Collision Avoidance

When an NPC collides with the player:

1. NPC is marked as "stopped" (`m_IsStopped = true`)
2. NPC stops moving but continues animation
3. When player moves away, NPC resumes patrolling

**Entity-Entity Resolution:**

```cpp
if (CheckAABBOverlap(player, npc)) {
    npc.SetStopped(true);
}
else {
    npc.SetStopped(false);
}
```

## Debug Visualization

### Collision Overlay (F3)

Red semi-transparent tiles show collision areas:

```
+---+---+---+
|   |RED|   |
+---+---+---+
|RED|RED|RED|  RED = collision tile
+---+---+---+
|   |   |   |
+---+---+---+
```

### Navigation Overlay

Cyan indicators show walkable (navigable) tiles:

```
+---+---+---+
|CYN|   |CYN|
+---+---+---+
|   |   |   |  CYN = navigable tile
+---+---+---+
|CYN|CYN|CYN|
+---+---+---+
```

### NPC Debug Info

When debug mode is active:
- Patrol waypoints shown as markers
- Current target tile highlighted
- Movement direction indicator
- Hitbox visualization

### Collision Response Overlay

Debug drawing focuses on the collision/navigation data and entity hitboxes. Use these overlays to
verify which blocked tiles are causing slide or stop behavior:

```
+---+---+
| X | X |  X = blocked
+---*---+  * = entity bottom-center / hitbox anchor
|   |   |
+---+---+
```

## Performance Considerations

### Spatial Queries

Collision checks use tile grid for O(1) lookups:

```cpp
// Convert world position to tile coordinates
int tileX = static_cast<int>(worldX / tileSize);
int tileY = static_cast<int>(worldY / tileSize);

// Direct array access
bool blocked = collisionMap[tileY * width + tileX];
```

### Entity Collision

With few entities (< 100), brute-force O(n^2) checking is acceptable:

```cpp
for (auto& npc : npcs) {
    if (CheckOverlap(player, npc)) {
        // Handle collision
    }
}
```

For many entities, spatial partitioning (grid, quadtree) would be needed.

### Navigation Caching

Patrol routes are computed once at:
- NPC spawn
- Navigation map changes

Routes are stored per-NPC, not recomputed each frame.

## Editor Integration

### Collision Painting

In editor mode, right-click to toggle collision:

```
Before:           After:
+---+---+        +---+---+
|   |   |   -->  |   |RED|
+---+---+        +---+---+
```

### Navigation Painting

Press M to toggle navigation edit mode:

```
Right-click: Toggle the first tile, then drag to apply that same navigable/non-navigable state
```

### NPC Placement

Press N to toggle NPC placement mode:

```
Left-click:  Place NPC of selected type
Left-click existing NPC: Remove NPC at cursor
```

Patrol routes are auto-generated when NPCs are placed.

## Mathematical Formulas

### AABB Overlap Test

Given two boxes with bottom-center anchors $(x_1, y_1)$ and $(x_2, y_2)$:

$$
overlapX = |x_1 - x_2| < hw_1 + hw_2
$$
$$
overlapY = y_1 - h_1 < y_2 \land y_2 - h_2 < y_1
$$
$$
collision = overlapX \land overlapY
$$

### Tile Coordinate Conversion

World position to tile:

$$
tileX = \lfloor \frac{worldX}{tileSize} \rfloor
$$
$$
tileY = \lfloor \frac{worldY}{tileSize} \rfloor
$$

Tile to world position (center):

$$
worldX = tileX \times tileSize + \frac{tileSize}{2}
$$
$$
worldY = tileY \times tileSize + \frac{tileSize}{2}
$$

### Movement Integration

Position update with velocity:

$$
position_{new} = position_{old} + velocity \times \Delta t
$$

Clamped to valid range:

$$
position = clamp(position, worldMin, worldMax)
$$

## See Also

- [Architecture](ARCHITECTURE.md) - System overview
- [Editor Guide](EDITOR.md) - How to paint collision/navigation
