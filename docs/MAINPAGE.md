@mainpage Rift Game Engine

@tableofcontents

## Overview

Rift is a 2D game engine written in C++ featuring dual graphics backends (OpenGL 4.6 and Vulkan 1.4), a complete day/night cycle with atmospheric effects, tile-based collision, NPC pathfinding, and a built-in level editor.

\htmlonly
<pre class="mermaid">
flowchart LR
    classDef core fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef system fill:#134e3a,stroke:#10b981,color:#e2e8f0
    classDef backend fill:#4a3520,stroke:#f59e0b,color:#e2e8f0

    subgraph Core["Core"]
        Game((Game)):::core
    end

    subgraph Systems["Engine Systems"]
        Input[Input]:::system
        Renderer[IRenderer]:::system
        World[World]:::system
        Entities[Entities]:::system
        Time[TimeManager]:::system
    end

    subgraph GPU["Graphics Backends"]
        OpenGL[OpenGL 4.6]:::backend
        Vulkan[Vulkan 1.4]:::backend
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

The engine uses a **top-left origin, Y-down** coordinate system. Press **F1** to switch between OpenGL and Vulkan at runtime.

@see [Rendering Pipeline](RENDERING.md) for coordinate transforms, sprite batching, and shader architecture.

---

## World & Collision

Tile-based world with 8 configurable layers. Entities use AABB collision with corner-cutting tolerance for smooth movement.

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
.\build\Debug\rift.exe  # Run
```

@see [Setup Guide](SETUP.md) for dependency installation.
@see [Building Guide](BUILDING.md) for platform-specific instructions.

---

## API Reference {#api}

### Core Classes

| Class                   | Responsibility                                            |
|-------------------------|-----------------------------------------------------------|
| @ref Game               | Main loop orchestration, input handling, state management |
| @ref IRenderer          | Abstract rendering interface (OpenGL/Vulkan)              |
| @ref OpenGLRenderer     | OpenGL 4.6 backend implementation                         |
| @ref VulkanRenderer     | Vulkan 1.4 backend implementation                         |
| @ref Tilemap            | Tile storage, collision map, navigation map               |
| @ref TimeManager        | Day/night cycle, ambient lighting                         |
| @ref SkyRenderer        | Stars, sun, moon, atmospheric effects                     |
| @ref PlayerCharacter    | Player entity, movement, animation                        |
| @ref NonPlayerCharacter | NPC behavior, patrol routes, dialogue                     |
| @ref PatrolRoute        | NPC patrol path generation and traversal                  |
| @ref DialogueManager    | Branching conversation system                             |
| @ref GameStateManager   | Game flags, quest state, persistence                      |
| @ref ParticleSystem     | Visual effects (fireflies, dust, etc.)                    |

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
    bool GetCollision(int x, int y) const;
    bool GetNavigation(int x, int y) const;
    int GetTileID(int x, int y, int layer) const;
};

// Pathfinding
class PatrolRoute {
    bool Initialize(startX, startY, tilemap, maxLength);
    bool GetNextWaypoint(int& x, int& y);
};
```
