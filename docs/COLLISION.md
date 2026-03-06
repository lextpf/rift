# Collision & Pathfinding

This document covers the collision detection, physics, and NPC navigation systems in the Wild Game Engine.

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

When an entity moves, collision is checked against both tile grid and other entities:

\htmlonly
<pre class="mermaid">
sequenceDiagram
    participant Entity
    participant Physics
    participant TileMap
    participant Entities

    Entity->>Physics: Move(velocity * dt)
    Physics->>Physics: Save previous position
    Physics->>TileMap: Check tile collisions
    TileMap-->>Physics: Blocked tiles
    Physics->>Physics: Adjust position (slide)
    Physics->>Entities: Check entity collisions
    Entities-->>Physics: Overlapping entities
    Physics->>Physics: Resolve overlaps
    Physics-->>Entity: Final position
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

### Corner Cutting

The collision system supports **corner cutting** for smoother navigation around obstacles:

```
Without corner cutting:     With corner cutting:
    +---+                       +---+
    |   |                       |   |
    | X |  Player stuck         | -->  Player slides past
    |   |                       |   |
+---+---+                   +---+---+
|       |                   |       |
```

**Corner Tolerance:**

When moving diagonally near a corner, the system checks if the entity could pass with a small tolerance:

$$
canCutCorner = (overlap < cornerTolerance)
$$

Default `cornerTolerance = 4` pixels.

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

Patrol routes are computed using a spanning tree traversal of walkable tiles:

1. Start at NPC's spawn position
2. Build a spanning tree of reachable tiles
3. Select waypoints along the tree edges
4. Create a cyclic route visiting all waypoints

$$
route = [waypoint_0, waypoint_1, ..., waypoint_n, waypoint_0]
$$

### Pathfinding Algorithm

NPCs use a simple **direct line** pathfinding for short distances:

```cpp
glm::vec2 direction = normalize(target - current);
velocity = direction * speed;
```

For longer paths or when obstacles block the direct line:

1. Check direct path
2. If blocked, try adjacent tiles
3. Follow pre-computed patrol route

**Future Enhancement:** A* pathfinding for dynamic obstacle avoidance.

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

### Corner Cutting Overlay

Shows which tile corners allow diagonal passage:

```
+---+---+
| X | X |  X = blocked
+---O---+  O = corner cut allowed
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

Press N to toggle navigation edit mode:

```
Left-click:  Set tile as navigable
Right-click: Set tile as non-navigable
```

### NPC Placement

Press P to toggle NPC placement mode:

```
Left-click:  Place NPC of selected type
Right-click: Remove NPC at cursor
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
