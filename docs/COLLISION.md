# Collision & Pathfinding

This document covers Rift's collision detection, physics, and NPC navigation systems.

Collision is stateless. There is no `CollisionResolver`: `CollisionSystem` is a namespace of free
functions that take an entity's `Hitbox`, its feet position and its committed support state, and
return a decision. Nothing is stored between calls.

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
        Elevation["Elevation grid"]:::map
    end

    subgraph Entities["Entities (ECS)"]
        Player["Player entity<br/>Transform + Hitbox + Elevation"]:::entity
        NPC["NPC entities<br/>Transform + Patrol + NpcIdle"]:::entity
    end

    subgraph Queries["Stateless Systems"]
        Tile["CollisionSystem<br/>tile overlap + tolerances"]:::query
        Entity["CollisionSystem<br/>character vs character"]:::query
        Surface["SurfaceSystem<br/>support graph"]:::query
        Route["PatrolRoute<br/>waypoint generation"]:::query
    end

    Tilemap --> Collision
    Tilemap --> Navigation
    Tilemap --> Elevation
    Player --> Tile
    NPC --> Tile
    Player --> Entity
    NPC --> Entity
    NPC --> Route
    Tile --> Collision
    Tile --> Surface
    Entity --> Surface
    Surface --> Elevation
    Route --> Navigation
</pre>
\endhtmlonly

## Tile-Based Collision

### Collision Map

Each cell in the world has one collision flag (true = blocked, false = passable):

```cpp
bool isBlocked = tilemap.GetTileCollision(tileX, tileY);
```

The flag lives in a `CollisionMap` (a `BoolGrid`) held beside the layer stack, not inside any
layer, and is indexed in row-major order:

$$
collisionIndex = tileY \times mapWidth + tileX
$$

A blocked cell does not block everyone. `SurfaceSystem::CollisionBelongsTo` matches the cell's
authored elevation against the tested character's support, so the same grid serves the ground under
a bridge and the deck above it. See **Elevation System** below.

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
- Height: 16 pixels, extending *upward* from the anchor - and world Y grows downward, so the box
  spans `[feet.y - height, feet.y]`

Only the player carries a `Hitbox` component, whose defaults are the constants above. NPCs carry
none: `NpcAiSystem` and `CollisionSystem` read `CharacterConstants::HALF_HITBOX_WIDTH` and
`HITBOX_HEIGHT` directly, and `CharacterCollisionBody` - the per-frame NPC snapshot - stores a feet
anchor and a support state only. The pixel values are calibrated for 16px tiles and do not scale
with a different tile size.

### AABB Collision Test

Two character boxes overlap only if they overlap on both axes. Both boxes are first shrunk inward by
$\epsilon$ (`CharacterConstants::COLLISION_EPS`, 0.05 px) so bodies resting edge-to-edge do not
register a collision. For two equal-size boxes:

$$
overlapX = |x_a - x_b| < hw_a + hw_b - 2\epsilon
$$
$$
overlapY = |y_a - y_b| < h - 2\epsilon
$$
$$
collision = overlapX \land overlapY
$$

Where:
- $(x_a, y_a)$, $(x_b, y_b)$ are bottom-center positions
- $hw$ is half-width, $h$ the shared height measured upward from the anchor

The vertical term uses $h$, not $h_a + h_b$: both boxes extend the same direction from their
anchors, so a separation of one full box height already clears them.
`CollisionGeometry::MakeFeetAabb` and `FeetBoxesOverlap` are the single implementation; no call
site rebuilds the box by hand.

### Movement and Collision Resolution

When an entity moves, collision is checked against both the tile grid and the other characters.
Player movement uses strict tile-overlap tests, then attempts corner sliding and axis-separated
recovery so diagonal movement can slide along walls instead of stopping outright:

\htmlonly
<pre class="mermaid">
flowchart TD
    A["Input direction * speed * dt"] --> B["Build target feet position"]
    B --> C{"ProbeMovement: support,<br/>tiles, NPCs"}
    C -->|clear| L["Lane snapping"]
    C -->|NPC blocked| Z["Displacement = 0"]
    C -->|tile blocked| D["TrySlideMovement<br/>corner slide + binary search"]
    D -->|no slide, diagonal| E["Axis split: X-only and Y-only<br/>from the SAME origin"]
    D --> L
    Z --> L
    E --> L
    L --> M{"Re-probe"}
    M -->|blocked| N["Axis fallback<br/>0.15 s axis preference"]
    M -->|clear| O["Zero the blocked axis on the motor"]
    N --> O
    O --> P["Commit position + support together"]
</pre>
\endhtmlonly

**Axis-Separated Movement:**

Movement is not integrated axis by axis. The full diagonal step is probed first; only when that
probe is blocked are two single-axis candidates built from the same starting position and probed
independently. `PlayerMovementSystem` then keeps whichever candidate is clear:

```cpp
// Full diagonal candidate first.
if (probeMovement(position + desiredMovement).IsBlocked())
{
    // Both single-axis candidates start from the SAME position, not from a
    // partially applied X step.
    const bool okX = !probeMovement(position + vec2(desiredMovement.x, 0.0f)).IsBlocked();
    const bool okY = !probeMovement(position + vec2(0.0f, desiredMovement.y)).IsBlocked();

    // When both are clear, a 0.15 s axis-preference hysteresis picks one, so a
    // corner does not flip the player between axes frame to frame.
}
```

This allows diagonal movement to slide along walls rather than stopping completely. Before this
fallback runs, `CollisionSystem::TrySlideMovement()` has already tried to project the motion onto
the nearest open corridor and binary-searched the largest safe step.

Position and support are committed **together**, and only from the probe that accepted the move, so
a rejected move leaves both untouched rather than half-applied.

### Corner Handling

There is no single public "corner tolerance" value. An overlapping tile is not a collision by
itself: every overlapping tile runs through a cascade of permissive checks inside
`CollidesWithTilesStrict()`, and each phase reasons about a different geometric situation with its
own budget. The movement-direction arguments feed that cascade, so probing with zero direction is a
harsher test than probing with the real one.

```
                  +------------------------------------------+
  moveDx,moveDy   | CollidesWithTilesStrict                  |
  diagonalInput   |   for each overlapping blocked tile       |
  feetPos         |   that belongs to this support:          |
                  |     1) diagonal grazing: < 4 px into a   |--+
                  |        diagonally adjacent tile during   |  |
                  |        cardinal motion                   |  |
                  |     2) wall-face penetration: <= 5 px    |  |
                  |        while not moving into the face,   |  +--> "no
                  |        with >= 4 px of face contact      |  |     collision"
                  |     3) corner cut: exposed convex corner |  |     (tile skipped)
                  |        with an escape route, <= 4 px     |  |
                  |        perpendicular (cardinal) or       |  |
                  |        <= 20% area (diagonal); or        |  |
                  |        <= 15% area against a side wall   |  |
                  |     4) overlap <= 1% of hitbox area      |--+
                  |                                          |
                  |     3a) closed convex corner under       |--+
                  |         diagonal input: forceCollision   |  +--> "blocked"
                  |     5) overlap > 1% of hitbox area       |--+
                  +------------------------------------------+
```

Phase 3a is the one hard block: it bypasses the 1% area floor, so a sliver overlap is tolerated
everywhere except when both movement axes push into solid faces of the same tile. A corner may also
be authored as cut-blocked per corner (`Tilemap::IsCornerCutBlocked`), which removes it from
phase 3 entirely.

Whatever the cascade allows, the invariant on the accepted position is unchanged:

$$
blocked(p) = \exists t \in overlappedTiles(p): AABB(p) \cap AABB(t) \neq \varnothing
$$

A diagonal move may still make progress when one axis is clear:

$$
accept(p_x, p_y) = \neg blocked(p_x, p_y) \lor \neg blocked(p_x, y_0) \lor \neg blocked(x_0, p_y)
$$

Two more helpers shape movement without being collision tests. `ApplyLaneSnapping` nudges the player
toward tile-center lanes during cardinal movement, exponentially smoothed over a 0.3 s settle and
clamped to 1.2 px per frame, so it ratchets into tight gaps instead of snapping.
`HandleStuckRecovery` searches a 5x5 tile window for the nearest safe tile center when the feet end
up embedded in a solid tile; it only *reports* the target, and the caller applies it.

## Elevation System

Each X/Y cell has an authored elevation height, while a character has an explicit committed
support state:

$$
support = (surface, height), \quad surface \in \{Ground, Elevation\}
$$

Elevated footprints do not replace the ground. They add a second traversable surface above the
implicit ground, which is what lets a bridge and an underpass occupy the same X/Y cells.

- A ground character crossing an elevated region perpendicular to its run remains on `Ground`.
- A character enters `Elevation` only through the reachable low end of a ramp, along the ramp
  axis. A diagonal step never enters or leaves an elevation.
- Once elevated, adjacent height changes must be within `MAX_STEP_HEIGHT` (8 px).
- Leaving an elevated surface is only legal through a reachable ramp edge; walking off the side
  of a deck is rejected.

`MAX_STEP_HEIGHT` is compared against a different quantity per edge kind: the absolute destination
height for ground -> elevation, the delta for elevation -> elevation (so a ramp may climb
arbitrarily far in small increments), and the absolute source height for elevation -> ground. A
rejected step is not clamped - the whole movement transaction is blocked.

Movement uses a read-only `ProbeMovement` transaction. It first resolves the destination support,
then evaluates tile and NPC collision against **that candidate** surface. Position and support are
committed together only if both checks succeed. Thus a railing cannot admit a ground-plane move and
promote the character into the railing on the next frame. Long probes are subdivided into steps of
at most 45% of a tile, so a fast move cannot tunnel past a ramp connector.

Collision tiles belong to the support authored at their cell: zero-elevation collision blocks
`Ground`; non-zero-elevation collision blocks `Elevation` at that exact height. The height match is
exact, with no tolerance band. This prevents a 6px ramp character from snagging on a nearby 10px
railing before the movement transaction reaches the deck. Character-vs-character overlap uses the
same exact support identity: mismatched supports are skipped outright rather than tolerated.

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
2. Use BFS to collect reachable walkable tiles, bounded by `maxRouteLength` (100 by default).
3. If the reachable set forms a closed degree-2 cycle, walk it as a loop.
4. Otherwise, use DFS with backtracking so consecutive waypoints remain adjacent.

$$
route = [waypoint_0, waypoint_1, ..., waypoint_n]
$$

Initialization fails, leaving the NPC standing and looking around, when the start tile is not
walkable or the region yields fewer than two waypoints.

**Walkable is stricter here than for pathfinding.** `PatrolRoute::IsValidTile` requires the
navigation flag **and** the absence of a collision flag, while `Pathfinding::FindPath` accepts any
navigable tile. A rock dropped on an authored patrol lane is therefore still routable by the
console's `nav.path` but unusable as a waypoint; a reported path is not evidence that a patrol can
follow it.

### Patrol Traversal

NPCs do not run dynamic A* pathfinding each frame. They follow the generated waypoint list, wait
briefly at waypoints, and recalculate routes when navigation tiles change or an NPC is placed.
Loop routes wrap from the last waypoint back to the first; non-loop routes ping-pong through the
list. A waypoint whose tile has since become blocked stops the NPC and invalidates the route, which
forces a rebuild on the next update.

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

NPCs pause and look around between patrol legs:

| Behavior      | Trigger                                    | Duration                      |
|---------------|--------------------------------------------|-------------------------------|
| Stand Still   | Rolled on arriving at a waypoint, once the cooldown expires | 2-4.99s random |
| Look Around   | While standing still                       | New facing every 2s           |
| Resume Patrol | After the stand-still timer expires        | Immediate                     |

The roll happens at waypoint arrival, not on a wall-clock schedule, and only when the cooldown has
run out:

$$
P(standStill) = 0.3
$$

The cooldown is re-armed to a random 5-9.99 seconds after every roll, so NPCs do not fall into
synchronized pauses. An NPC that has no valid route stands still with the timer at zero and looks
around indefinitely until a route can be built.

### Collision Avoidance

When an NPC collides with the player:

1. NPC is marked as stopped (`NpcIdle::isStopped = true`)
2. NPC stops moving but continues animation
3. When player moves away, NPC resumes patrolling

`NpcAiSystem::Update` applies a second, independent brake: a move whose destination overlaps the
player, or whose support transition is disconnected, is refused and the NPC waits 0.5 s instead.

**Entity-Entity Resolution:**

`NpcAiSystem::ApplyPlayerOverlapStop` rewrites the flag for every NPC each frame - it assigns
rather than OR-combines, so a caller that wants an NPC frozen for another reason must set
`isStopped` after this pass runs:

```cpp
world.each<NpcIdle, NpcTag>([&](ecs::entity npc, NpcIdle& idle)
{
    idle.isStopped = OverlapsPlayerFeetBox(world, npc, playerFeet);
});
```

This pass runs with epsilon zero, i.e. an exact box test, because its job is to remove visible
sprite overlap rather than to decide movement.

## Debug Visualization

### Collision Overlay (`debug.overlays`)

No function key except F12 is bound. Open the developer console with F12 and run
`debug.overlays` (aliases `dbg.overlays`, `dbg`) to turn every debug overlay on or off.

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

The collision and navigation overlays are shared: they also appear while the editor is active, with
no debug mode required.

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

Debug mode adds, per NPC:
- Magenta hitbox rectangle at the NPC's feet anchor
- Green dot on the NPC's current target tile

The whole patrol route is not drawn; use the console's `npc.path` command to list a route's
waypoints. The player's own hitbox is drawn in yellow by the corner-cutting overlay.

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

Collision checks index the tile grid directly, so a lookup is O(1). The conversion is not a bare
floor: `TileMath` exposes three deliberately different Y conversions, and picking the wrong one is
an off-by-one-row bug.

```cpp
int tileX = TileMath::TileIndex(worldX, tileWidth);                    // bare floor
int collisionRow = TileMath::AnchorTileRow(worldY, tileHeight, eps);   // collision pipeline
int standingRow = TileMath::StandingTileRow(worldY, tileHeight);       // NPC patrol / interact

bool blocked = tilemap.GetTileCollision(tileX, collisionRow);
```

`GetTileCollision` returns false out of bounds, and the strict tile test skips out-of-bounds cells
rather than treating them as walls. Corner detection is the exception: it treats out of bounds as
blocked, so the map edge does not read as an open escape route.

### Entity Collision

Character-vs-character collision is brute force over a per-frame snapshot, which is acceptable at
Rift's entity counts. `EntityStore::BuildNpcCollisionBodies` refills a reused vector each frame, so
the scan allocates nothing:

```cpp
for (const CharacterCollisionBody& npc : npcBodies)
{
    if (npc.support != support)
        continue;  // Different surface or height: they share X/Y but not a walkable plane.

    if (CollisionGeometry::FeetBoxesOverlap(feet, npc.feet, halfWidth, height, COLLISION_EPS))
        return true;
}
```

For many more entities, spatial partitioning (grid, quadtree) would be needed.

### Navigation Caching

Patrol routes are stored per NPC in a `PatrolRoute` component and are not recomputed each frame.
They are built:

- lazily, the first time an NPC reaches a waypoint with no valid route
- by `RebuildPatrolRoutes` whenever an editor command changes navigation flags or places an NPC

`RebuildPatrolRoutes` is unconditional and idempotent: it discards and regenerates every route, and
an NPC whose route cannot be rebuilt is left standing and looking around with a warning logged,
rather than skipped.

## Editor Integration

### Collision Painting

In editor mode, right-click a tile to toggle its collision flag, then drag to apply that same
state to further tiles. The whole drag commits as one undoable command:

```
Before:           After:
+---+---+        +---+---+
|   |   |   -->  |   |RED|
+---+---+        +---+---+
```

### Navigation Painting

While the editor is active, press M to toggle navigation edit mode:

```
Right-click: Toggle the first tile, then drag to apply that same navigable/non-navigable state
```

Clearing a tile's navigation flag destroys any NPC standing on it, because that NPC no longer has a
valid patrol home. The command snapshots those NPCs so undo restores them with their original
instance ids.

### NPC Placement

While the editor is active, press N to toggle NPC placement mode:

```
Left-click:  Place NPC of selected type (navigable tiles only)
Left-click existing NPC: Remove NPC at cursor
```

Removal takes priority: a click on an occupied tile removes rather than stacks. Use `,` and `.` to
cycle the selected NPC type. Patrol routes are regenerated when NPCs are placed or removed.

## Mathematical Formulas

### AABB Overlap Test

Given two boxes with bottom-center anchors $(x_1, y_1)$ and $(x_2, y_2)$, ignoring the epsilon
shrink:

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

World X to tile column is a bare floor:

$$
tileX = \left\lfloor \frac{worldX}{tileWidth} \right\rfloor
$$

World Y to tile row has three answers, because a feet anchor sits on a tile boundary. Pick by the
caller's intent:

$$
TileIndex = \left\lfloor \frac{worldY}{tileHeight} \right\rfloor
$$
$$
StandingTileRow = \left\lfloor \frac{worldY - 0.1}{tileHeight} \right\rfloor
$$
$$
AnchorTileRow = \left\lfloor \frac{worldY - tileHeight / 2 - \epsilon}{tileHeight} \right\rfloor
$$

Tile to feet position - the bottom-center of the tile, so Y lands on the tile's **bottom** edge, not
its center:

$$
worldX = tileX \times tileWidth + \frac{tileWidth}{2}
$$
$$
worldY = tileY \times tileHeight + tileHeight
$$

A caller that re-derives a row from such a Y must use $AnchorTileRow$; a bare floor selects the row
below and places the feet one whole tile past the intended tile.

### Movement Integration

Position update with velocity:

$$
position_{new} = position_{old} + velocity \times \Delta t
$$

Character positions are **not** clamped to the map rectangle - the map edge is enforced only where
tiles are authored as blocking. The camera is what gets clamped to the map bounds, so near an edge
the viewport stops and the player drifts off-center.

## See Also

- [Architecture](ARCHITECTURE.md) - System overview
- [Editor Guide](EDITOR.md) - How to paint collision/navigation
