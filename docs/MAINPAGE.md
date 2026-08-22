@mainpage Rift

@tableofcontents

## Overview

Rift is a 2.5D RPG game written in C++ featuring dual graphics backends (OpenGL 4.6 and Vulkan), a complete day/night cycle with atmospheric effects, tile-based collision, NPC patrol routing, and a built-in level editor.

Two Vulkan version numbers appear in this documentation and they mean different things. The build needs the Vulkan 1.4 SDK (`find_package(Vulkan REQUIRED)`), but the backend requests instance `apiVersion` 1.0 and enables only `VK_KHR_SWAPCHAIN`, so any Vulkan 1.0+ device can run it. See @ref VulkanRenderer for the exact feature set.

\htmlonly
<pre class="mermaid">
flowchart LR
    classDef core fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef system fill:#134e3a,stroke:#10b981,color:#e2e8f0
    classDef backend fill:#4a3520,stroke:#f59e0b,color:#e2e8f0

    subgraph Core["Core"]
        Game((Game)):::core
    end

    subgraph Systems["Game Systems"]
        Input[Input]:::system
        Renderer[IRenderer]:::system
        World[World]:::system
        Entities[Entities]:::system
        Time[TimeManager]:::system
    end

    subgraph GPU["Graphics Backends"]
        OpenGL[OpenGL 4.6]:::backend
        Vulkan[Vulkan 1.0]:::backend
    end

    Game -->|process| Input
    Game -->|render| Renderer
    Game -->|query| World
    Game -->|update| Entities
    Game -->|time| Time
    Renderer -.-> OpenGL
    Renderer -.-> Vulkan
</pre>
\endhtmlonly

---

## Core Systems

@see [Architecture](ARCHITECTURE.md) for detailed system design and game loop.

---

## Rendering

Rift uses a **top-left origin, Y-down** coordinate system. Run `renderer.set opengl|vulkan` in the developer console (`F12`) to switch backends at runtime.

@see [Rendering Pipeline](RENDERING.md) for coordinate transforms, sprite batching, and shader architecture.

---

## World & Collision

Tile-based world with 10 configurable layers. Entities use strict AABB collision with slide recovery, lane snapping, and NPC patrol routes generated from the navigation map.

@see [Collision & Pathfinding](COLLISION.md) for collision detection, navigation meshes, and NPC AI.

---

## Time System

Complete day/night cycle with 8 time periods, sun/moon arcs, star visibility, and ambient color transitions.

@see [Time System](TIME_SYSTEM.md) for celestial mechanics and lighting calculations.

---

## Building

```powershell
.\setup.ps1             # Download dependencies
.\build.bat             # Build the project
.\run.bat               # Run (Release, repo-root CWD, manifest/assets/shaders preflight)
```

`run.bat` starts the Release build with the repository root as the working directory. Launching the
executable directly also works, because the build copies `rift.project.json`, `assets/` and
`shaders/` next to it - but the game resolves all three against the working directory, so the
directory you start it from decides which configuration it picks up.

@see [Setup Guide](SETUP.md) for dependency installation.
@see [Building Guide](BUILDING.md) for platform-specific instructions.

---

## API Reference {#api}

### Core Types and Systems

| Symbol                    | Responsibility                                            |
|---------------------------|-----------------------------------------------------------|
| @ref Game                 | Main loop orchestration, input handling, state management |
| @ref IRenderer            | Abstract rendering interface (OpenGL/Vulkan)              |
| @ref OpenGLRenderer       | OpenGL 4.6 backend implementation                         |
| @ref VulkanRenderer       | Vulkan 1.0 backend implementation                         |
| @ref Tilemap              | Tile storage, collision map, navigation map               |
| @ref TimeManager          | Day/night cycle, ambient lighting                         |
| @ref SkyRenderer          | Stars, sun, moon, atmospheric effects                     |
| @ref PlayerSystem         | Player appearance, character switching, per-frame update  |
| @ref PlayerMovementSystem | Stateless player movement, facing, and stop logic         |
| @ref NpcAiSystem          | NPC patrol and idle AI over route components              |
| @ref EntityStore          | Entity spawn, snapshot, reposition, despawn, query        |
| @ref WorldServices        | Non-owning shared services published into `globals()`     |
| @ref PatrolRoute          | NPC patrol path generation and traversal                  |
| @ref DialogueManager      | Branching conversation system                             |
| @ref GameStateManager     | Game flags, quest state, persistence                      |
| @ref ParticleSystem       | Visual effects (fireflies, dust, etc.)                    |

### Key Interfaces

```cpp
// Rendering
class IRenderer {
    virtual void DrawSprite(texture, position, size, rotation, color);
    virtual void DrawSpriteRegion(texture, position, size, texCoord, texSize, ...);
    virtual void DrawText(text, position, scale, color);
    virtual void DrawColoredRect(position, size, color);
};

// World Queries
class Tilemap {
    bool GetTileCollision(int x, int y) const;
    bool GetNavigation(int x, int y) const;
    int GetLayerTile(int x, int y, size_t layer) const;
    size_t GetLayerCount() const;  // the layer stack is dynamic; never hard-code it
};

// Pathfinding
class PatrolRoute {
    bool Initialize(startX, startY, tilemap, maxLength);
    bool GetNextWaypoint(int& x, int& y);
};
```
