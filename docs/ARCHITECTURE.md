# Architecture Overview

The Wild Game Engine follows a **layered architecture** with clear separation of concerns. At its core is the `Game` class which orchestrates all subsystems through a traditional game loop pattern.

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

    subgraph Systems["Engine Systems"]
        direction LR
        Time["TimeManager"]:::system
        Sky["SkyRenderer"]:::system
        Dialogue["DialogueManager"]:::system
        State["GameStateManager"]:::system
        Particles["ParticleSystem"]:::system
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
        Collision["CollisionMap"]:::data
        Navigation["NavigationMap"]:::data
        Texture["Texture"]:::data
    end

    subgraph Entities["Entities"]
        direction LR
        Player["PlayerCharacter"]:::data
        NPC["NonPlayerCharacter"]:::data
    end

    %% Entry -> Core
    main --> Game

    %% Core Loop chain
    Game --> Input --> Update --> Render

    %% Game fan-out (route through invisible hubs to keep lines clean)
    Game --> GSys(( ))
    Game --> GRend(( ))
    Game --> GWorld(( ))
    Game --> GEnt(( ))

    %% Hub -> targets
    GSys --> Time
    GSys --> Dialogue
    GSys --> Particles

    GRend --> IRenderer
    GRend --> Factory

    GWorld --> Tilemap

    GEnt --> Player
    GEnt --> NPC

    %% Systems internal links
    Time --> Sky
    Dialogue --> State

    %% Rendering internal links
    Factory --> OpenGL
    Factory --> Vulkan
    IRenderer -.-> OpenGL
    IRenderer -.-> Vulkan

    %% World internal links
    Tilemap --> Collision
    Tilemap --> Navigation
    Tilemap --> Texture

    %% Entity interactions
    NPC --> Navigation
    Player --> Collision
    NPC --> Collision

    %% Make hubs invisible (but keep routing)
    style GSys fill:transparent,stroke:transparent
    style GRend fill:transparent,stroke:transparent
    style GWorld fill:transparent,stroke:transparent
    style GEnt fill:transparent,stroke:transparent
</pre>
\endhtmlonly

## Design Principles

| Principle                 | Implementation                                                |
|---------------------------|---------------------------------------------------------------|
| **Abstraction**           | `IRenderer` interface decouples game logic from graphics API  |
| **Single Responsibility** | Each class handles one concern (Tilemap, CollisionMap,...)    |
| **Data-Oriented**         | Tilemaps use flat arrays for cache-friendly iteration         |
| **Composition**           | Game composes systems rather than inheriting from them        |
| **Factory Pattern**       | `RendererFactory` creates appropriate backend at runtime      |

## The Game Loop

The engine uses a **variable timestep** game loop where delta time is computed each frame:

\htmlonly
<pre class="mermaid">
flowchart LR
    subgraph Frame["Each Frame"]
        direction LR
        A["Compute deltaTime"] --> B["ProcessInput(dt)"]
        B --> C["Update(dt)"]
        C --> D["Render()"]
        D --> E["Poll Events"]
    end

    E --> A
</pre>
\endhtmlonly

### Frame Execution Order

```cpp
void Game::Run()
{
    while (!glfwWindowShouldClose(m_Window))
    {
        float currentTime = glfwGetTime();
        float deltaTime = currentTime - m_LastFrameTime;
        m_LastFrameTime = currentTime;

        ProcessInput(deltaTime);  // Handle keyboard/mouse
        Update(deltaTime);        // Advance game state
        Render();                 // Draw everything

        glfwPollEvents();         // Process window events
    }
}
```

### Delta Time and Frame Independence

Movement and animations are scaled by delta time to ensure consistent behavior regardless of frame rate:

$$
position_{new} = position_{old} + velocity \times \Delta t
$$

For smooth camera following, the engine uses exponential smoothing:

$$
camera_{new} = camera_{old} + (target - camera_{old}) \times \alpha
$$

Where $\alpha$ is computed for a specific settle time $T$ and epsilon $\epsilon$:

$$
\alpha = 1 - \epsilon^{\Delta t / T}
$$

This ensures the camera reaches 99% of its target in time $T$ regardless of frame rate.

## Core Systems

### Game (Orchestrator)

The `Game` class is the **composition root** that owns and coordinates all subsystems:

\htmlonly
<pre class="mermaid">
graph LR
    classDef owner fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef owned fill:#134e3a,stroke:#10b981,color:#e2e8f0

    Game["Game"]:::owner

    Game --> |owns| Tilemap:::owned
    Game --> |owns| Player["PlayerCharacter"]:::owned
    Game --> |owns| NPCs["vector<NPC>"]:::owned
    Game --> |owns| Renderer["unique_ptr<IRenderer>"]:::owned
    Game --> |owns| Time["TimeManager"]:::owned
    Game --> |owns| Sky["SkyRenderer"]:::owned
    Game --> |owns| Dialogue["DialogueManager"]:::owned
    Game --> |owns| State["GameStateManager"]:::owned
    Game --> |owns| Particles["ParticleSystem"]:::owned
</pre>
\endhtmlonly

**Responsibilities:**
- Window creation and management
- Game loop execution
- Input processing and dispatch
- Camera management
- Editor mode handling
- Render orchestration

### Renderer Abstraction

The rendering system uses the **Strategy Pattern** to support multiple graphics APIs:

\htmlonly
<pre class="mermaid">
classDiagram
    class IRenderer {
        <<interface>>
        +Init()
        +Shutdown()
        +BeginFrame()
        +EndFrame()
        +DrawSprite()
        +DrawSpriteRegion()
        +DrawText()
        +SetProjection()
        +UploadTexture()
    }

    class OpenGLRenderer {
        -m_ShaderProgram
        -m_VAO, m_VBO
        -m_TextureCache
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
        +Create(api) IRenderer*
        +IsAvailable(api) bool
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

**Legend:** 🟦 Interface - 🟩 Implementations - 🟥 Factory

**Runtime Backend Switching:**

Press **F1** to switch between OpenGL and Vulkan without restarting. The process:

1. Save current game state
2. Destroy current renderer and window
3. Create new window with appropriate context hints
4. Create new renderer via `RendererFactory`
5. Re-upload all textures to GPU
6. Restore game state

### World System

The world is represented by `Tilemap` which manages tile data and derived maps:

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

    subgraph Layers["8 Tile Layers"]
        L0["Ground"]:::data
        L1["Ground Detail"]:::data
        L2["Objects"]:::data
        L3["Objects2"]:::data
        L4["Foreground"]:::data
        L5["Foreground2"]:::data
        L6["Overlay"]:::data
        L7["Overlay2"]:::data
    end

    subgraph Derived["Derived Data"]
        Collision["CollisionMap"]:::derived
        Navigation["NavigationMap"]:::derived
        Elevation["ElevationMap"]:::derived
        YSort["Y-Sort Tiles"]:::derived
        NoProj["No-Projection Tiles"]:::derived
    end

    Tilemap --> Layers
    Tilemap --> Derived
</pre>
\endhtmlonly

**Tile Storage:**

Tiles are stored in flat arrays for cache-efficient iteration:

```cpp
// Accessing tile at (x, y) in layer L
int index = y * mapWidth + x;
int tileID = m_Layers[L][index];
```

### Entity System

Entities (Player and NPCs) share common concepts but have distinct behaviors:

\htmlonly
<pre class="mermaid">
---
config:
  layout: elk
---
stateDiagram-v2
    [*] --> Idle
    Idle --> Walking: Input/AI
    Walking --> Idle: Reached Target
    Walking --> Walking: New Target

    state Walking {
        [*] --> Frame0
        Frame0 --> Frame1: timer
        Frame1 --> Frame2: timer
        Frame2 --> Frame1: timer
    }
</pre>
\endhtmlonly

**Position Convention:**

All entities use **bottom-center anchoring** for their position. This simplifies Y-sorting: entities with higher Y values (lower on screen) render in front.

@see [Collision & Pathfinding - Entity Hitboxes](COLLISION.md#entity-hitboxes) for hitbox dimensions and AABB collision details.

### Time System

`TimeManager` drives the day/night cycle and provides time-based queries for ambient lighting, celestial body positions, and atmospheric effects.

**Core Responsibilities:**
- Track game time (0.0-24.0 hours)
- Compute sun/moon arc positions
- Calculate star visibility
- Interpolate ambient light colors
- Drive `SkyRenderer` for celestial rendering

@see [Time System](TIME_SYSTEM.md) for detailed time period definitions, celestial mechanics formulas, and ambient color calculations.

### Dialogue System

The dialogue system supports both simple text and branching conversations:

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
    Tree["DialogueTree"]:::data
    Node["DialogueNode"]:::data
    Option["DialogueOption"]:::data
    GSM["GameStateManager"]:::state

    DM --> Tree
    Tree --> Node
    Node --> Option
    Option --> |conditions| GSM
    Option --> |consequences| GSM
</pre>
\endhtmlonly

**Branching Dialogue Flow:**

1. Player interacts with NPC
2. `DialogueManager` loads NPC's `DialogueTree`
3. Current node's text is displayed
4. Options are filtered by evaluating conditions against `GameStateManager`
5. Player selects an option
6. Consequences are executed (set/clear flags)
7. Transition to next node or end dialogue

## Memory and Performance

### Data Layout

The engine favors **Structure of Arrays (SoA)** for frequently iterated data:

```cpp
// Tilemap stores each layer as a flat array
std::vector<int> m_Layer0;  // Ground tiles
std::vector<int> m_Layer1;  // Ground detail
// ... etc

// Rather than Array of Structures:
// std::vector<Tile> m_Tiles; // Each tile has all layer data
```

This improves cache utilization when rendering a single layer.

### Sprite Batching

Both renderers batch sprites by texture to minimize draw calls:

\htmlonly
<pre class="mermaid">
sequenceDiagram
    participant Game
    participant Renderer
    participant GPU

    Game->>Renderer: DrawSprite(tex1, ...)
    Game->>Renderer: DrawSprite(tex1, ...)
    Game->>Renderer: DrawSprite(tex2, ...)
    Game->>Renderer: DrawSprite(tex1, ...)

    Note over Renderer: Batch by texture

    Renderer->>GPU: Draw batch (tex1, 3 sprites)
    Renderer->>GPU: Draw batch (tex2, 1 sprite)
</pre>
\endhtmlonly

### Culling

Before rendering, tiles outside the camera viewport are culled:

$$
visible = (tileX \geq camX - margin) \land (tileX < camX + viewWidth + margin) \land ...
$$

The margin accounts for perspective distortion when 3D effects are enabled.

## Extension Points

### Adding a New Renderer Backend

1. Create a class implementing `IRenderer`
2. Add enum value to `RendererAPI`
3. Update `RendererFactory::Create()` and `IsAvailable()`
4. Implement all virtual methods

### Adding a New Entity Type

1. Create a class with position, sprite, and update logic
2. Add a container in `Game` (e.g., `std::vector<MyEntity>`)
3. Update `Game::Update()` to advance entity state
4. Update `Game::Render()` to draw entities (with Y-sorting if needed)

### Adding a New Tile Property

1. Add storage in `Tilemap` (e.g., `std::vector<bool> m_NewProperty`)
2. Add getter/setter methods
3. Update JSON serialization in `SaveMap()`/`LoadMap()`
4. Add editor UI for painting the property
5. Add debug overlay for visualization

## See Also

- [Rendering Pipeline](RENDERING.md) - Detailed coordinate system and transformation documentation
- [Time System](TIME_SYSTEM.md) - Day/night cycle mathematics
- [Collision & Pathfinding](COLLISION.md) - Physics and navigation details
