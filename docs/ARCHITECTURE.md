# Architecture Overview

Rift is built on a small **entity-component-system** core with stateless free-function systems.
`Game` (`src/Game.hpp`) is the composition root: it owns the window, the renderer, the tilemap, the
ECS registry and every subsystem **by value**, and drives Initialize -> Run -> Shutdown.

There are no `PlayerCharacter`, `NonPlayerCharacter` or `CollisionResolver` classes. An actor is an
entity holding plain-struct components; the behavior lives in free functions that take those
components by reference.

## System Overview

\htmlonly
<pre class="mermaid">
flowchart LR
    classDef core fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef system fill:#134e3a,stroke:#10b981,color:#e2e8f0
    classDef render fill:#2e1f5e,stroke:#8b5cf6,color:#e2e8f0
    classDef data fill:#4a3520,stroke:#f59e0b,color:#e2e8f0

    subgraph Entry["Entry Point"]
        direction LR
        main["main.cpp"]:::core
    end

    subgraph Core["Core Loop"]
        direction LR
        Game["Game"]:::core
        Input["ProcessInput()"]:::core
        Update["Update()"]:::core
        Render["Render()"]:::core
    end

    subgraph Ecs["ECS World"]
        direction LR
        Registry["ecs::registry m_World"]:::data
        Services["WorldServices<br/>(registry globals)"]:::data
        Player["Player entity<br/>PlayerTag components"]:::data
        NPC["NPC entities<br/>NpcTag components"]:::data
    end

    subgraph Systems["Stateless Systems"]
        direction LR
        Move["PlayerMovementSystem"]:::system
        NpcAi["NpcAiSystem"]:::system
        Collision["CollisionSystem"]:::system
        Surface["SurfaceSystem"]:::system
        Store["EntityStore"]:::system
    end

    subgraph Owned["Owned Subsystems"]
        direction LR
        Time["TimeManager"]:::system
        Sky["SkyRenderer"]:::system
        Weather["WeatherDirector"]:::system
        Dialogue["DialogueManager"]:::system
        State["GameStateManager"]:::system
        Particles["ParticleSystem"]:::system
        Editor["Editor"]:::system
        Console["Console"]:::system
    end

    subgraph Rendering["Graphics"]
        direction LR
        IRenderer["IRenderer"]:::render
        Factory["RendererFactory"]:::render
        OpenGL["OpenGLRenderer"]:::render
        Vulkan["VulkanRenderer"]:::render
    end

    subgraph World["World Data"]
        direction LR
        Tilemap["Tilemap"]:::data
        CollisionMap["CollisionMap"]:::data
        Navigation["NavigationMap"]:::data
        Elevation["Elevation grid"]:::data
    end

    %% Entry -> Core
    main --> Game

    %% Core Loop chain
    Game --> Input --> Update --> Render

    %% Game fan-out (route through invisible hubs to keep lines clean)
    Game --> GEcs(( ))
    Game --> GSys(( ))
    Game --> GRend(( ))
    Game --> GWorld(( ))

    %% Hub -> targets
    GEcs --> Registry
    Registry --> Services
    Registry --> Player
    Registry --> NPC

    GSys --> Time
    GSys --> Weather
    GSys --> Dialogue
    GSys --> Particles
    GSys --> Editor
    GSys --> Console

    GRend --> IRenderer
    GRend --> Factory

    GWorld --> Tilemap

    %% Subsystem internal links
    Time --> Sky
    Weather --> Time
    Dialogue --> State

    %% Rendering internal links
    Factory --> OpenGL
    Factory --> Vulkan
    IRenderer -.-> OpenGL
    IRenderer -.-> Vulkan

    %% World internal links
    Tilemap --> CollisionMap
    Tilemap --> Navigation
    Tilemap --> Elevation

    %% Systems read components and world data, never each other's state
    Update --> Systems
    Systems --> Registry
    Collision --> CollisionMap
    Surface --> Elevation
    NpcAi --> Navigation

    %% Make hubs invisible (but keep routing)
    style GEcs fill:transparent,stroke:transparent
    style GSys fill:transparent,stroke:transparent
    style GRend fill:transparent,stroke:transparent
    style GWorld fill:transparent,stroke:transparent
</pre>
\endhtmlonly

## Design Principles

| Principle              | Implementation                                                    |
|------------------------|-------------------------------------------------------------------|
| **Abstraction**        | `IRenderer` decouples game logic from the graphics API            |
| **Data over behavior** | Components are plain structs; logic lives in free functions       |
| **Stateless systems**  | A system stores nothing between calls, so tests drive it directly |
| **Explicit ownership** | `unique_ptr` owns; raw pointers and references never do           |
| **Data-Oriented**      | Tile layers and per-cell grids are flat arrays                    |
| **Composition**        | `Game` composes subsystems by value rather than inheriting them   |
| **Factory selection**  | `RendererFactory` creates the backend at runtime                  |

## The Game Loop

Rift uses a **variable timestep** loop. Delta time is sampled before events are polled, so the FPS
limiter's deadline covers event processing, and polling runs **before** input so handlers observe
this frame's key state.

\htmlonly
<pre class="mermaid">
flowchart LR
    subgraph Frame["Each Frame"]
        direction LR
        A["Sample frame start<br/>compute deltaTime"] --> B["glfwPollEvents()"]
        B --> C["Clamp deltaTime<br/>to MAX_DELTA_TIME"]
        C --> D["ProcessInput(dt)"]
        D --> E["Update(dt)"]
        E --> F["Render()"]
        F --> G["FPS limiter<br/>sleep + spin"]
    end

    G --> A
</pre>
\endhtmlonly

### Frame Execution Order

```cpp
while (!glfwWindowShouldClose(m_Window))
{
    double frameStartTime = glfwGetTime();
    float deltaTime = static_cast<float>(frameStartTime) - m_LastFrameTime;
    m_LastFrameTime = static_cast<float>(frameStartTime);

    glfwPollEvents();  // Before input: GLFW only refreshes cached key state on poll.

    static constexpr float MAX_DELTA_TIME = 0.1f;
    deltaTime = std::min(deltaTime, MAX_DELTA_TIME);

    ProcessInput(deltaTime);  // Handle keyboard/mouse
    Update(deltaTime);        // Advance game state
    Render();                 // Draw everything

    // Optional FPS limiter: sleep, then spin-yield to the frame deadline.
}
```

The clamp matters: without it a debugger pause or a window-drag stall delivers one enormous delta
and pushes characters through walls. The three stages run inside a try/catch, so an escaping
exception is logged and the loop is abandoned - `Run()` returning does not imply a clean exit.

### Delta Time and Frame Independence

Movement and animations are scaled by delta time to keep behavior identical at any frame rate:

$$
position_{new} = position_{old} + velocity \times \Delta t
$$

For smooth camera following, Rift uses exponential smoothing:

$$
camera_{new} = camera_{old} + (target - camera_{old}) \times \alpha
$$

Where $\alpha$ is computed for a specific settle time $T$ and epsilon $\epsilon$:

$$
\alpha = 1 - \epsilon^{\Delta t / T}
$$

This covers $1 - \epsilon$ of the distance in time $T$ regardless of frame rate.
`rift::ExpApproachAlpha` (`src/MathUtils.hpp`) is the shared implementation;
`CollisionSystem::CalculateFollowAlpha` is an identical private twin used by lane snapping, so
tuning one does not reach the other.

## Core Systems

### Game (Composition Root)

`Game` owns every subsystem by value, plus the ECS registry itself. Shared services are **not**
components: `Game` publishes non-owning pointers to five of them into `m_World.globals()` as a
`WorldServices` bundle, which is how a stateless system reaches a texture store or a dialogue tree.
Every pointer in the bundle is nullable - tests routinely publish a partial bundle - so readers
null-check rather than assume a wired world.

\htmlonly
<pre class="mermaid">
graph LR
    classDef owner fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef owned fill:#134e3a,stroke:#10b981,color:#e2e8f0
    classDef published fill:#4a3520,stroke:#f59e0b,color:#e2e8f0

    Game["Game"]:::owner

    Game --> |owns| Tilemap["Tilemap"]:::owned
    Game --> |owns| Registry["ecs::registry m_World"]:::owned
    Game --> |owns| Renderer["unique_ptr&lt;IRenderer&gt;"]:::owned
    Game --> |owns| Textures["TextureStore"]:::owned
    Game --> |owns| Dialogues["DialogueStore"]:::owned
    Game --> |owns| Assets["AssetRegistry"]:::owned
    Game --> |owns| Rng["std::mt19937 m_NpcRng"]:::owned
    Game --> |owns| State["GameStateManager"]:::owned
    Game --> |owns| Time["TimeManager"]:::owned
    Game --> |owns| Sky["SkyRenderer"]:::owned
    Game --> |owns| Weather["WeatherDirector"]:::owned
    Game --> |owns| Particles["ParticleSystem"]:::owned
    Game --> |owns| DialogueMgr["DialogueManager"]:::owned
    Game --> |owns| Editor["Editor"]:::owned
    Game --> |owns| Console["Console"]:::owned
    Game --> |owns| Camera["CameraController"]:::owned

    Registry --> |globals| Services["WorldServices"]:::published
    Textures -.-> |pointer| Services
    Dialogues -.-> |pointer| Services
    Assets -.-> |pointer| Services
    Rng -.-> |pointer| Services
    State -.-> |pointer| Services
</pre>
\endhtmlonly

**Responsibilities:**
- Window creation, resize handling and tile-boundary snapping
- Game loop execution and frame timing
- Input routing per `GameMode` (Title, Playing, Paused)
- Camera follow and map clamping
- Render orchestration, including the offscreen scene target and the PostFX composite
- Publishing `WorldServices` and minting the player entity

The implementation is split across partials by concern: `Game.cpp` (lifecycle and frame loop),
`GameInput.cpp` (input routing), `GameMenus.cpp` (title and pause menus plus the title and 3D
render paths), and `GameDialogue.cpp` (dialogue presentation).

### Renderer Abstraction

The rendering system uses the **Strategy Pattern** to support multiple graphics APIs. Both backends
are always compiled in; Vulkan is a hard CMake dependency, so no `#ifdef` guards a backend.

\htmlonly
<pre class="mermaid">
classDiagram
    class IRenderer {
        <<interface>>
        +Init() bool
        +Shutdown()
        +BeginFrame()
        +EndFrame()
        +BeginScene()
        +EndSceneApplyPostFX(params)
        +DrawSprite()
        +DrawSpriteRegion()
        +DrawSpriteAtlas()
        +DrawColoredRect()
        +DrawQuad3D()
        +DrawText()
        +SetProjection()
        +SetViewProjection()
        +SetViewport()
        +UploadTexture()
        +GetBackendInfo() RendererInfo
    }

    class OpenGLRenderer {
        -m_ShaderProgram
        -m_VAO, m_VBO
        -m_BatchVertices
        +Init()
        +DrawSprite()
    }

    class VulkanRenderer {
        -m_Device
        -m_SwapChain
        -m_Pipeline
        +Init()
        +DrawSprite()
    }

    class RendererFactory {
        <<namespace>>
        +CreateRenderer(api, window) unique_ptr~IRenderer~
        +IsRendererAvailable(api) bool
    }

    IRenderer <|.. OpenGLRenderer
    IRenderer <|.. VulkanRenderer
    RendererFactory ..> IRenderer : creates

    style IRenderer fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    style OpenGLRenderer fill:#134e3a,stroke:#10b981,color:#e2e8f0
    style VulkanRenderer fill:#134e3a,stroke:#10b981,color:#e2e8f0
    style RendererFactory fill:#4a2020,stroke:#ef4444,color:#e2e8f0
</pre>
\endhtmlonly

**Legend:** 🟦 Interface - 🟩 Implementations - 🟥 Factory (free functions, not a class)

`RendererMacros.hpp` declares the override set through `RIFT_DECLARE_COMMON_RENDERER_METHODS`.
Adding, removing or re-signing a pure virtual in `IRenderer.hpp` requires the matching edit there,
or the two backends drift apart.

**Runtime Backend Switching:**

Open the developer console with **F12** and run `renderer.set opengl|vulkan` to switch backends
without restarting. No function key other than F12 is bound. `Game::SwitchRenderer` then:

1. Shut down and destroy the current renderer
2. Record the window position, then destroy the GLFW window
3. Create a new window with the target API's hints (`GLFW_OPENGL_CORE_PROFILE` vs `GLFW_NO_API`)
4. Create the renderer through `RendererFactory::CreateRenderer`
5. Re-upload every texture and re-pack characters into the tile atlas
6. Restore the window position

Window position is the only preserved window state. Any cached `GLFWwindow*` or `IRenderer*`
dangles after the switch. A failed switch rolls back to the previous API and still reports `false`;
if the rollback also fails, `Shutdown()` runs and the application cannot continue.

### World System

The world is a `Tilemap`: a dynamic stack of tile layers plus per-cell grids held beside it.

\htmlonly
<pre class="mermaid">
---
config:
  layout: elk
---
graph LR
    classDef primary fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef derived fill:#134e3a,stroke:#10b981,color:#e2e8f0
    classDef data fill:#4a3520,stroke:#f59e0b,color:#e2e8f0

    Tilemap["Tilemap"]:::primary

    subgraph Layers["Tile Layers - default stack of 10, sized by GetLayerCount"]
        L0["Ground"]:::data
        L1["Ground Detail"]:::data
        L2["Objects"]:::data
        L3["Objects2"]:::data
        L4["Objects3"]:::data
        L5["Foreground"]:::data
        L6["Foreground2"]:::data
        L7["Overlay"]:::data
        L8["Overlay2"]:::data
        L9["Overlay3"]:::data
    end

    subgraph PerLayer["Per-Layer, Per-Tile Fields"]
        Tiles["tiles, rotation, flipX/flipY"]:::data
        Stance["stance, elevationRole"]:::data
        YSort["ySortPlus / ySortMinus"]:::data
        Struct["structureId, animationMap"]:::data
    end

    subgraph Derived["Per-Cell Grids"]
        Collision["CollisionMap (BoolGrid)"]:::derived
        Navigation["NavigationMap (BoolGrid)"]:::derived
        Elevation["Per-cell elevation, in pixels"]:::derived
        CornerCut["Corner-cut block mask"]:::derived
    end

    Tilemap --> Layers
    Layers --> PerLayer
    Tilemap --> Derived
</pre>
\endhtmlonly

| Layer | Name            | Render Order | Purpose                    |
|-------|-----------------|--------------|----------------------------|
| 0     | Ground          | 0            | Base terrain               |
| 1     | Ground Detail   | 10           | Grass, paths, decorations  |
| 2     | Objects         | 20           | Buildings, rocks, trees    |
| 3     | Objects2        | 30           | Additional objects         |
| 4     | Objects3        | 40           | Additional objects         |
| 5     | Foreground      | 100          | Elements in front of NPCs  |
| 6     | Foreground2     | 110          | Additional foreground      |
| 7     | Overlay         | 120          | Overlay effects            |
| 8     | Overlay2        | 130          | Additional overlay         |
| 9     | Overlay3        | 140          | Top-most overlay           |

The layer count is data-driven, not an invariant. The constructor and `SetTilemapSize` build the
default 10-layer stack above, but `LoadMapFromJSON` clears the stack and rebuilds it from the map's
`dynamicLayers[]` array. Use `GetLayerCount()`; never hard-code 10. A layer's side of the actors
comes from its `isBackground` flag, not from its index.

Collision, navigation, elevation and the corner-cut mask are **per cell**, held beside the stack.
Collision therefore belongs to no layer: a cell either blocks or does not, whichever layers carry
artwork there. Y-sort, stance, structure and rotation are the opposite - per layer and per tile.

**Tile Storage:**

Tiles are stored in flat arrays for cache-efficient iteration:

```cpp
// Accessing tile at (x, y) in dynamic layer L
int index = y * mapWidth + x;
int tileID = m_Layers[L].tiles[index];
```

### Entity System

An actor is an entity in `m_World` carrying granular components. `EntityStore` is the spawn,
despawn and query seam; the behavior is stateless free functions.

Both actors share `Transform`, `Elevation`, `Facing`, `AnimationState` and `Speed`. Beyond that:

- **Player** adds `Appearance`, `PlayerModes`, `PlayerInputState`, `PlayerMovementState`, `Motor`,
  `PlayerSprite`, `Hitbox`, and the empty `PlayerTag`.
- **NPC** adds `Identity`, `NpcSprite`, `Dialogue`, `NpcIdle`, `Patrol`, `PatrolRoute`, and the
  empty `NpcTag`.

Two asymmetries are deliberate. The player carries no `Identity`: it is reached through
`Game::m_PlayerEntity`, never by a despawn-surviving instance id. NPCs carry no `Hitbox`: their box
dimensions come straight from `CharacterConstants`, and `CharacterCollisionBody` - the per-frame
collision snapshot - stores only a feet anchor and a support state.

| System                 | Responsibility                                                    |
|------------------------|-------------------------------------------------------------------|
| `PlayerSystem`         | Player entry points used by `Game`, the console and the editor    |
| `PlayerMovementSystem` | Per-frame step: input -> motor -> collision -> committed position  |
| `MotionSystem`         | Acceleration, deceleration and the latched grid stop target       |
| `NpcAiSystem`          | Patrol traversal, idle behavior, player-overlap stop              |
| `CollisionSystem`      | Tile and character collision, wall sliding, lane snapping         |
| `SurfaceSystem`        | Ground/elevation support graph and collision ownership            |
| `CharacterKinematics`  | Elevation interpolation, animation cadence, support commit        |
| `EntityStore`          | Spawn, snapshot, despawn, per-frame collision-body assembly       |
| `*Render` helpers      | `PlayerRender`, `NpcRender`, `CharacterRender` draw assembly      |

**Position Convention:**

All entities use **bottom-center anchoring** for their position. Rendering preserves authored
background/Y-sort/foreground roles, then adds underpass/deck constraints only within the connected
elevation region containing an actor's feet. A character is one atomic queue item, so bridge rows
cannot interleave with its body or hair.

@see [Collision & Pathfinding - Entity Hitboxes](COLLISION.md#entity-hitboxes) for hitbox dimensions and AABB collision details.

### Time System

`TimeManager` drives the day/night cycle and provides time-based queries for ambient lighting,
celestial body positions, and atmospheric effects. `WeatherDirector` choreographs weather
transitions on top of it, and a night-only weather can impose night so its visuals are not washed
out by an authored daytime hour.

**Core Responsibilities:**
- Track game time (0.0-24.0 hours) and the elapsed day count
- Compute sun/moon arc positions and the moon phase
- Calculate star visibility, including any imposed-night contribution
- Interpolate ambient, sky and celestial colors
- Drive `SkyRenderer` for celestial rendering

@see [Time System](TIME_SYSTEM.md) for detailed time period definitions, celestial mechanics formulas, and ambient color calculations.

### Dialogue System

The dialogue system supports both a simple one-line fallback and branching conversations. Tree data
is owned by `DialogueStore` and referenced from an NPC's `Dialogue` component by handle;
`DialogueManager` runs the active conversation.

\htmlonly
<pre class="mermaid">
---
config:
  layout: elk
---
graph LR
    classDef manager fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef data fill:#134e3a,stroke:#10b981,color:#e2e8f0
    classDef state fill:#4a3520,stroke:#f59e0b,color:#e2e8f0

    DM["DialogueManager"]:::manager
    UI["DialogueUiState"]:::manager
    Store["DialogueStore"]:::data
    Comp["Dialogue component"]:::data
    Tree["DialogueTree"]:::data
    Node["DialogueNode"]:::data
    Option["DialogueOption"]:::data
    GSM["GameStateManager"]:::state

    Comp --> |handle| Store
    Store --> Tree
    DM --> Tree
    Tree --> Node
    Node --> Option
    Option --> |conditions| GSM
    Option --> |consequences| GSM
    DM --> UI
</pre>
\endhtmlonly

**Branching Dialogue Flow:**

1. The player presses **F** next to an NPC while the editor is off
2. Player and NPC slide into aligned talk positions (`DialogueSnapState`)
3. `DialogueManager` resolves the NPC's `DialogueTree` through `DialogueStore`
4. The current node's text is revealed by the typewriter and paginated to the box
5. Options are filtered by evaluating conditions against `GameStateManager`
6. The player selects an option; its consequences run (set/clear flags)
7. The tree transitions to the next node, or the dialogue ends and the NPC is released

The speaker is referenced by its stable `Identity::instanceId`, not by an entity handle, so a
despawn between frames cannot leave a dangling reference.

## Memory and Performance

### Data Layout

Rift favors **Structure of Arrays (SoA)** for frequently iterated data:

```cpp
// Tilemap stores dynamic layers; each layer owns flat per-tile arrays
std::vector<TileLayer> m_Layers;
int groundTile = m_Layers[0].tiles[index];
int detailTile = m_Layers[1].tiles[index];

// Rather than Array of Structures:
// std::vector<Tile> m_Tiles; // Each tile has all layer data
```

This improves cache utilization when rendering a single layer. The ECS has the same shape:
components live in per-type pools, so a view over `Transform` + `Facing` walks contiguous storage.

### Sprite Batching

OpenGL batches consecutive sprites by texture to minimize draw calls. Vulkan currently submits
each sprite quad directly, while still using persistent per-frame vertex buffers and cached
descriptor sets:

\htmlonly
<pre class="mermaid">
sequenceDiagram
    participant Game
    participant OpenGL
    participant Vulkan
    participant GPU

    Game->>OpenGL: DrawSprite(tex1, ...)
    Game->>OpenGL: DrawSprite(tex1, ...)
    OpenGL->>GPU: Flush batch (tex1, 2 sprites)

    Game->>Vulkan: DrawSprite(tex1, ...)
    Vulkan->>GPU: vkCmdDraw(quad)
    Game->>Vulkan: DrawSprite(tex1, ...)
    Vulkan->>GPU: vkCmdDraw(quad)
</pre>
\endhtmlonly

Packing every character sheet into the tile atlas is what lets the Y-sorted pass collapse into one
OpenGL batch: tiles and characters then share a texture, so no run is broken by a texture change.

### Culling

`Tilemap::ComputeTileRange` converts the camera rectangle into an inclusive tile range and clamps it
to the map:

$$
x_0 = \left\lfloor \frac{cam_x}{tileW} \right\rfloor, \qquad
x_1 = \left\lfloor \frac{cam_x + view_w}{tileW} \right\rfloor
$$

Flooring at both ends is what keeps partially visible edge tiles in range, so the base rectangle
needs no margin. A margin is added only where artwork extends beyond its own cell: the depth-sorted
scan expands the rectangle by 8 tiles on every side, and the 3D path scans 16 extra rows north, so
an upright structure whose base row is off-screen still contributes its visible upper tiles.

## Extension Points

### Adding a New Renderer Backend

1. Create a class implementing `IRenderer`
2. Add an enumerator to `RendererAPI`
3. Declare the override set with `RIFT_DECLARE_COMMON_RENDERER_METHODS`, never by hand
4. Update `CreateRenderer()` and `IsRendererAvailable()` in `RendererFactory.cpp`
5. Add the backend's window hints to `Game::SwitchRenderer`

### Adding a New Entity Type

There are no per-entity classes. An entity is a set of plain-struct components in `m_World`, and
its behavior is a stateless free function.

1. Add the plain-struct components it needs (`Transform`, `Appearance`, ... plus a tag struct),
   with unprefixed `camelCase` fields
2. Spawn it through `EntityStore`, which attaches the components and returns the entity handle
3. Add a stateless system (free functions over the registry) and call it from `Game::Update()`
4. Emit its drawables in the Y-sorted draw-list build (`RenderDrawable`) if it must sort with
   the world
5. Add any new `src/*.cpp` to `TEST_LIB_SOURCES` in `CMakeLists.txt` if a test needs it

### Adding a New Tile Property

1. Add storage in `Tilemap`: a `BoolGrid` for a per-cell flag, or a field on `TileLayer` for a
   per-layer one
2. Add getter/setter methods, and extend `TileLayer::Resize` / `Clear` for a per-layer field
3. Update JSON serialization in `Tilemap::SaveMapToJSON()` / `Tilemap::LoadMapFromJSON()`
4. Add an `EditorCommand` for painting it, so the edit is undoable
5. Add a debug overlay in `EditorRendering.cpp` for visualization

## See Also

- [Rendering Pipeline](RENDERING.md) - Detailed coordinate system and transformation documentation
- [Time System](TIME_SYSTEM.md) - Day/night cycle mathematics
- [Collision & Pathfinding](COLLISION.md) - Physics and navigation details
