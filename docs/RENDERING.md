# Rendering Pipeline

This document describes the coordinate systems, transformations, and rendering techniques used in the Rift Game Engine.

## Coordinate System

The engine uses a **top-left origin, Y-down** coordinate system measured in pixels

This matches typical 2D game and UI conventions where:
- Origin $ (0, 0) $ is at the **top-left** corner
- $ \hat{x} = (1, 0) $ points **right**
- $ \hat{y} = (0, 1) $ points **down**

## Coordinate Spaces

The rendering pipeline transforms vertices through several coordinate spaces:

\htmlonly
<pre class="mermaid">
flowchart LR
  classDef space fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
  classDef transform fill:#134e3a,stroke:#10b981,color:#e2e8f0
  classDef auto fill:#4a2020,stroke:#ef4444,color:#e2e8f0

  subgraph CPU ["Game Code CPU"]
    World["World Space (pixels)"]:::space
    Camera["-camera"]:::transform
    View["View Space (pixels)"]:::space
  end

  subgraph Shader ["Vertex Shader"]
    Local["Local Space [0,1] (x, y)"]:::space
    Model["Model Matrix ST(-c)RT(c)T(p)"]:::transform
    Proj["Projection ortho()"]:::transform
    Clip["Clip Space [-w,w] (x, y, z, w)"]:::space
  end

  subgraph GPU ["Automatic GPU"]
    WDiv["/w"]:::auto
    NDC["NDC [-1,1] (x, y, z)"]:::space
    Viewport["glViewport"]:::auto
    Screen["Screen Space [0,W]x[0,H] px"]:::space
  end

  World --> Camera --> View
  Local --> Model
  View -.->|"position"| Model
  Model --> Proj --> Clip
  Clip --> WDiv --> NDC --> Viewport --> Screen
</pre>
\endhtmlonly

**Legend:** 🟦 Coordinate spaces - 🟩 Transforms we implement - 🟥 GPU fixed-function

### World Space

Absolute pixel coordinates in the game world. A tile at grid position $(tx, ty)$ has world coordinates:

$$
\vec{p}_{world} = (tx \times tileSize, ty \times tileSize)
$$

### View/Camera Space

Coordinates relative to the camera's top-left corner. This answers: "where does this object appear on screen?"

$$
\vec{p}_{view} = \vec{p}_{world} - \vec{p}_{camera}
$$

**What the camera position means:**

$\vec{p}_{camera}$ is the world coordinate of the **top-left corner** of the visible area. If the camera is at $(100, 50)$, then world point $(100, 50)$ appears at view position $(0, 0)$ - the top-left of the screen.

**View space coordinates:**
- $(0, 0)$ = top-left corner of screen
- $(viewWidth, viewHeight)$ = bottom-right corner
- $Values < 0$ = off-screen to the left/top
- $Values > viewSize$ = off-screen to the right/bottom

**With zoom:**

Zoom changes how much of the world fits on screen:

|                   | Zoom=1           | Zoom=2          |
|-------------------|------------------|-----------------|
| **Screen size**   | 1920x1080 px     | 1920x1080 px    |
| **Visible world** | 320x180 world px | 160x90 world px |

**How zoom works - we change the projection matrix:**

$$
visibleWidth = \frac{baseWidth}{zoom}
$$

```cpp
// Zoom is applied by changing ortho() parameters
glm::mat4 P = glm::ortho(0.0f, visibleWidth, visibleHeight, 0.0f, -1, 1);
```

| Zoom | ortho()            | Effect                                           |
|------|--------------------|--------------------------------------------------|
| 1.0  | 320                | View coord $320 \rightarrow NDC +1$ (right edge) |
| 2.0  | 160                | View coord $160 \rightarrow NDC +1$ (right edge) |

At zoom=2, the projection maps a **smaller** view range to the **same** NDC range $[-1,+1]$. This makes everything appear larger - a 16px sprite that was $\frac{16}{320} = 5\%$ of screen width is now $\frac{16}{160} = 10\%$.

**Why CPU-side:**

The camera transform is done on CPU before rendering because:
1. We need view positions for **culling** (skip off-screen tiles)
2. The model matrix needs view-space position to place sprites
3. Avoids passing camera uniform to every draw call

```cpp
// CPU: compute view position
glm::vec2 viewPos = worldPos - cameraPos;

// Pass to renderer (becomes part of model matrix)
renderer.DrawSprite(texture, viewPos, size, rotation);
```

### Local/Object Space

For sprites, the vertex buffer contains a **unit quad**:

$$
\vec{p}_{local} = (u, v), \quad u, v \in [0, 1]
$$

With vertices at $(0,0)$, $(1,0)$, $(0,1)$, $(1,1)$.

### Clip Space

After the vertex shader applies projection and model matrices:

$$
\vec{p}_{clip} = P \cdot M \cdot \vec{p}_{local}^h
$$

Where $\vec{p}_{local}^h = (u, v, 0, 1)^T$ is the homogeneous local position.

### Normalized Device Coordinates (NDC)

After the perspective divide (trivial for orthographic projection since $w = 1$):

$$
\vec{p}_{NDC} = \frac{\vec{p}_{clip}}{w_{clip}} = \vec{p}_{clip}
$$

NDC ranges from $[-1, 1]$ in both X and Y.

### Screen Space

The viewport transform maps NDC to framebuffer pixels. For viewport $(x_0, y_0, w, h)$:

$$
x_{screen} = x_0 + \frac{w}{2}(x_{NDC} + 1)
$$
$$
y_{screen} = y_0 + \frac{h}{2}(y_{NDC} + 1)
$$

## Transformation Matrices

### Model Matrix

The model matrix transforms the unit quad to view-space pixels with position, scale, and rotation:

$$
M = T(\vec{p}) \cdot T(\vec{c}) \cdot R_z(\theta) \cdot T(-\vec{c}) \cdot S(\vec{s})
$$

Where:
- $\vec{p} = (p_x, p_y)$ - sprite position in view pixels
- $\vec{s} = (s_x, s_y)$ - sprite size in pixels
- $\vec{c} = \frac{1}{2}\vec{s}$ - sprite center (rotation pivot)
- $\theta$ - rotation angle in radians

This sequence:
1. **S** - Scale the unit quad to sprite size
2. **T(-c)** - Translate so the center is at the origin
3. **R** - Rotate around the origin
4. **T(c)** - Translate back
5. **T(p)** - Translate to final position

**Primitive matrices:**

$$
S(\vec{s}) = \begin{pmatrix}
s_x & 0 & 0 & 0 \\\\
0 & s_y & 0 & 0 \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix} \\\\
$$
$$
T(\vec{t}) = \begin{pmatrix}
1 & 0 & 0 & t_x \\\\
0 & 1 & 0 & t_y \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix} \\\\
$$
$$
R_z(\theta) = \begin{pmatrix}
\cos\theta & -\sin\theta & 0 & 0 \\\\
\sin\theta & \cos\theta & 0 & 0 \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
$$

**Step-by-step multiplication**:

**Step 1:** $T(-\vec{c}) \cdot S(\vec{s})$ - Scale then shift center to origin

$$
\begin{pmatrix}
1 & 0 & 0 & -c_x \\\\
0 & 1 & 0 & -c_y \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
\times
\begin{pmatrix}
s_x & 0 & 0 & 0 \\\\
0 & s_y & 0 & 0 \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
=
\begin{pmatrix}
s_x & 0 & 0 & -c_x \\\\
0 & s_y & 0 & -c_y \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
$$

**Step 2:** $R_z(\theta) \cdot [T(-\vec{c}) \cdot S(\vec{s})]$ - Rotate around origin

$$
\begin{pmatrix}
\cos\theta & -\sin\theta & 0 & 0 \\\\
\sin\theta & \cos\theta & 0 & 0 \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
\times
\begin{pmatrix}
s_x & 0 & 0 & -c_x \\\\
0 & s_y & 0 & -c_y \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
=
\begin{pmatrix}
s_x\cos\theta & -s_y\sin\theta & 0 & -c_x\cos\theta + c_y\sin\theta \\\\
s_x\sin\theta & s_y\cos\theta & 0 & -c_x\sin\theta - c_y\cos\theta \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
$$

**Step 3:** $T(\vec{c}) \cdot [R_z(\theta) \cdot T(-\vec{c}) \cdot S(\vec{s})]$ - Shift back from origin

$$
\begin{pmatrix}
1 & 0 & 0 & c_x \\\\
0 & 1 & 0 & c_y \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
\times
\begin{pmatrix}
s_x\cos\theta & -s_y\sin\theta & 0 & -c_x\cos\theta + c_y\sin\theta \\\\
s_x\sin\theta & s_y\cos\theta & 0 & -c_x\sin\theta - c_y\cos\theta \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
=
\begin{pmatrix}
s_x\cos\theta & -s_y\sin\theta & 0 & c_x(1-\cos\theta) + c_y\sin\theta \\\\
s_x\sin\theta & s_y\cos\theta & 0 & c_y(1-\cos\theta) - c_x\sin\theta \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
$$

**Step 4:** $T(\vec{p}) \cdot [T(\vec{c}) \cdot R_z(\theta) \cdot T(-\vec{c}) \cdot S(\vec{s})]$ - Move to final position

$$
M = \begin{pmatrix}
s_x\cos\theta & -s_y\sin\theta & 0 & p_x + c_x(1-\cos\theta) + c_y\sin\theta \\\\
s_x\sin\theta & s_y\cos\theta & 0 & p_y + c_y(1-\cos\theta) - c_x\sin\theta \\\\
0 & 0 & 1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
$$

**Simplification insight:** The $c_xy(1-\cos\theta) + c_yx\sin\theta$ terms come from:
$$
c_x + (-c_x\cos\theta + c_y\sin\theta) = c_x - c_x\cos\theta + c_y\sin\theta = c_x(1-\cos\theta) + c_y\sin\theta
$$

### Orthographic Projection Matrix

Maps view pixels $[0, w] \times [0, h]$ to NDC $[-1, 1] \times [-1, 1]$ while preserving Y-down:

$$
P = \begin{pmatrix}
\frac{2}{w} & 0 & 0 & -1 \\\\
0 & -\frac{2}{h} & 0 & 1 \\\\
0 & 0 & -1 & 0 \\\\
0 & 0 & 0 & 1
\end{pmatrix}
$$

This produces:
- $x = 0 \rightarrow x_{NDC} = -1$
- $x = w \rightarrow x_{NDC} = +1$
- $y = 0 \rightarrow y_{NDC} = +1$ (top)
- $y = h \rightarrow y_{NDC} = -1$ (bottom)

**Resulting NDC mapping:**

$$
x_{NDC} = \frac{2x}{w} - 1
$$
$$
y_{NDC} = 1 - \frac{2y}{h}
$$

### Complete Vertex Transform

The full transformation in the vertex shader:

$$
\vec{p}_{clip} = P \cdot M \cdot \vec{p}_{local}^h
$$

```glsl
// Vertex shader
gl_Position = projection * model * vec4(aPos, 0.0, 1.0);
```

## Texture Coordinates

### Sprite Sheets

`DrawSpriteRegion` extracts a rectangular portion of a texture atlas using pixel coordinates:

$$
u_0 = \frac{texCoord.x}{textureWidth}, \quad u_1 = \frac{texCoord.x + texSize.x}{textureWidth}
$$
$$
v_0 = \frac{texCoord.y}{textureHeight}, \quad v_1 = \frac{texCoord.y + texSize.y}{textureHeight}
$$

### Y-Flip Handling

Image files typically store pixels top-to-bottom (row 0 = top), but OpenGL's texture coordinate origin is at the **bottom-left**.

With `flipY = true`:

$$
v' = 1 - v
$$

This is applied during UV calculation so sprite sheets work correctly regardless of how images are loaded.

**OpenGL vs Vulkan:**

| API    | Texture Origin | Framebuffer Origin |
|--------|----------------|--------------------|
| OpenGL | Bottom-left    | Bottom-left        |
| Vulkan | Top-left       | Top-left           |

The engine abstracts this via `IRenderer::RequiresYFlip()`.

## Sprite Batching

Both renderers batch consecutive sprites that share the same texture into a single draw call. When the texture changes, the current batch is flushed and a new batch begins:

\htmlonly
<pre class="mermaid">
sequenceDiagram
    participant Game
    participant Renderer
    participant Batch as "Batch Buffer"
    participant GPU

    Game->>Renderer: DrawSprite(tex1, pos1)
    Renderer->>Batch: Add vertices

    Game->>Renderer: DrawSprite(tex1, pos2)
    Renderer->>Batch: Add vertices

    Game->>Renderer: DrawSprite(tex2, pos3)
    Note over Renderer: Texture changed!
    Renderer->>GPU: Flush batch (tex1)
    Renderer->>Batch: Add vertices (tex2)

    Game->>Renderer: EndFrame()
    Renderer->>GPU: Flush remaining
</pre>
\endhtmlonly

### Batch Structure

Each sprite adds **6 vertices** (2 triangles) to the batch - vertices are duplicated rather than using indices:

```cpp
struct BatchVertex {
    float x, y;    // View-space position
    float u, v;    // UV coordinates
};

// Two triangles per quad (6 vertices, corners 0 and 2 duplicated)
//  0 -------- 1
//  | \\\\       |
//  |   \\\\     |  Triangle 1: 0-2-3
//  |     \\\\   |  Triangle 2: 0-1-2
//  |       \\\\ |
//  3 -------- 2
```

The batch is flushed when:
1. The batch buffer is full (vertices)
2. Texture changes
3. Blend mode changes
4. Frame ends

## Perspective Effects

The engine supports pseudo-3D projection modes that transform the flat orthographic view into curved, depth-aware scenes.

\htmlonly
<pre class="mermaid">
graph LR
    subgraph Modes["Projection Modes"]
        M1["Orthographic<br/>(flat, default)"]
        M2["Globe<br/>(barrel distortion)"]
        M3["Vanishing Point<br/>(horizon scaling)"]
        M4["Fisheye<br/>(combined)"]
    end

    M1 --> |"SetGlobePerspective"| M2
    M1 --> |"SetVanishingPointPerspective"| M3
    M1 --> |"SetFisheyePerspective"| M4
    M2 --> M4
    M3 --> M4
</pre>
\endhtmlonly

### Globe Projection

Maps screen coordinates onto a sphere surface, creating a curved-world effect where edges bend inward.

**Sphere Mapping:**

For each vertex at position $(x, y)$ relative to screen center $(c_x, c_y)$:

$$
\begin{aligned}
\Delta x &= x - c_x \\\\
\Delta y &= y - c_y
\end{aligned}
$$

The globe transformation maps linear distance to arc length on a sphere of radius $R$:

$$
\begin{aligned}
x' &= c_x + R \sin\left(\frac{\Delta x}{R}\right) \\\\
y' &= c_y + R \sin\left(\frac{\Delta y}{R}\right)
\end{aligned}
$$

### Vanishing Point Projection

Simulates depth by scaling objects based on their Y position, making distant objects (near horizon) smaller.

**Depth Normalization:**

Given horizon line at $y_h$ and screen height $H$, compute normalized depth:

$$
d = \text{clamp}\left(\frac{y - y_h}{H - y_h}, 0, 1\right)
$$

Where:
- $d = 0$ at the horizon ($y = y_h$)
- $d = 1$ at screen bottom ($y = H$)

**Scale Factor:**

The scale interpolates from $s_h$ (horizon scale) at the horizon to $1.0$ at screen bottom:

$$
s(y) = s_h + (1 - s_h) \cdot d
$$

Typical value: $s_h = 0.6$ means objects at horizon are 60% normal size.

**Coordinate Transform:**

Each vertex is scaled toward the vanishing point $(v_x, y_h)$:

$$
\begin{aligned}
x' &= v_x + (x - v_x) \cdot s(y) \\
y' &= y_h + (y - y_h) \cdot s(y)
\end{aligned}
$$

The vanishing point $v_x$ is typically at screen center: $v_x = c_x$.

**Visual Effect:**

$$
\begin{array}{|c|c|c|}
\text{Screen Position} & d & \text{Scale} \\\\
\text{Horizon } (y = y_h) & 0 & 0.6 \\\\
\text{Middle } (y = \frac{y_h + H}{2}) & 0.5 & 0.8 \\\\
\text{Bottom } (y = H) & 1.0 & 1.0 \\\\
\end{array}
$$

### Fisheye Mode

Applies both globe and vanishing point transformations in sequence:

$$
\vec{p}' = T_{vanishing}(T_{globe}(\vec{p}))
$$

### Projection Parameters

| Parameter     | Symbol | Typical Value | Effect                      |
|---------------|--------|---------------|-----------------------------|
| Sphere Radius | $R$    | 800-1200      | Larger = less curvature     |
| Horizon Y     | $y_h$  | 0-100         | Distance from top of screen |
| Horizon Scale | $s_h$  | 0.5-0.8       | Smaller = more depth        |
| View Width    | $w$    | Screen width  | For center calculation      |
| View Height   | $H$    | Screen height | For depth calculation       |

### No-Projection Tiles

Some tiles (buildings, signs) should remain upright rather than following the perspective distortion. These are marked as "no-projection" and rendered with:

```cpp
renderer->SuspendPerspective(true);
// Draw upright tiles
renderer->SuspendPerspective(false);
```

## Alpha Blending

All drawing uses standard alpha blending:

$$
C_{out} = C_{src} \times \alpha_{src} + C_{dst} \times (1 - \alpha_{src})
$$

**OpenGL:**
```cpp
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
```

**Vulkan:**
```cpp
colorBlendAttachment.blendEnable = VK_TRUE;
colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
```

### Additive Blending

For glowing effects (particles, light rays):

$$
C_{out} = C_{src} \times \alpha_{src} + C_{dst}
$$

Enabled via `DrawSpriteAlpha(..., additive=true)`.

## Render Order

The engine renders in a specific order for correct depth:

\htmlonly
<pre class="mermaid">
flowchart LR
    subgraph Background
        A1["1. Clear (sky color)"]
        A2["2. Background layers"]
        A3["3. No-projection background"]
    end

    subgraph YSorted["Y-Sorted Pass"]
        B1["4. Tiles + NPCs + Player<br/>(interleaved by Y)"]
    end

    subgraph Foreground
        C1["5. No-projection foreground"]
        C2["6. No-projection particles"]
        C3["7. Foreground layers"]
        C4["8. Regular particles"]
    end

    subgraph Sky
        D1["9. Sky/ambient overlay"]
    end

    subgraph UI
        E1["10. Editor/UI overlays"]
        E2["11. Debug overlays & anchors"]
    end

    Background --> YSorted --> Foreground --> Sky --> UI
</pre>
\endhtmlonly

### Y-Sorting Algorithm

Entities and Y-sorted tiles are collected into a single list and sorted by Y coordinate:

```cpp
struct RenderItem {
    enum Type { TILE, NPC, PLAYER } type;
    float sortY;
    // ... data
};

std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
    const float epsilon = 1.0f;
    if (std::abs(a.sortY - b.sortY) > epsilon)
        return a.sortY < b.sortY;  // Lower Y renders first (behind)
    return a.type > b.type;        // Tiebreaker: entities behind tiles
});
```

## Particle System Mathematics

The particle system provides ambient visual effects through physics-based motion and procedural animation.

### Particle Lifecycle

Each particle has lifetime $t_{max}$ and current remaining time $t$. The normalized life progress:

$$
\ell = \frac{t_{max} - t}{t_{max}} \in [0, 1]
$$

Where $\ell = 0$ at spawn, $\ell = 1$ at death.

**Fade Curves:**

Fade-in over duration $t_{in}$:
$$
\alpha_{in} = \min\left(1, \frac{t_{max} - t}{t_{in}}\right)
$$

Fade-out over duration $t_{out}$:
$$
\alpha_{out} = \min\left(1, \frac{t}{t_{out}}\right)
$$

Combined fade envelope:
$$
\alpha_{fade} = \alpha_{in} \cdot \alpha_{out}
$$

### Motion Equations

**Basic Euler Integration:**

Position update each frame with timestep $\Delta t$:
$$
\vec{p}_{n+1} = \vec{p}_n + \vec{v} \cdot \Delta t
$$

**Sinusoidal Drift (Fireflies, Wisps):**

Adds oscillating displacement using particle phase $\phi$:
$$
\begin{aligned}
\Delta x_{drift} &= A_x \sin(\omega_x t + \phi) \cdot \Delta t \\\\
\Delta y_{drift} &= A_y \cos(\omega_y t + k\phi) \cdot \Delta t
\end{aligned}
$$

Where:
- $A_x, A_y$ = drift amplitude (pixels/second)
- $\omega_x, \omega_y$ = angular frequency
- $k$ = phase multiplier for Y (creates varied paths)

### Alpha Animation

**Pulsing Glow (Fireflies):**

Sinusoidal pulse between $\alpha_{min}$ and $\alpha_{max}$:
$$
\alpha_{pulse} = \frac{\alpha_{max} + \alpha_{min}}{2} + \frac{\alpha_{max} - \alpha_{min}}{2} \cdot \sin(\omega t + \phi)
$$

**Sparkle Flash:**

Binary flash in first 15% of lifetime:
$$
\alpha = \begin{cases}
1 & \text{if } \ell < 0.15 \\\\
0 & \text{otherwise}
\end{cases}
$$

### Rotation

Angular velocity $\overset{\cdot}{\theta}$ varies by particle phase:
$$
\overset{\cdot}{\theta} = \omega_{base} + \frac{\phi}{2\pi} \cdot \omega_{range}
$$

Direction alternates based on phase:
$$
\overset{\cdot}{\theta}' = \begin{cases}
+\overset{\cdot}{\theta} & \text{if } \phi \mod 2 < 1 \\\\
-\overset{\cdot}{\theta} & \text{otherwise}
\end{cases}
$$

### Particle Rendering Order

Particles are rendered after foreground tiles but before sky effects:

1. Non-additive particles (fog, rain) - standard alpha blending
2. Additive particles (fireflies, sparkles, wisps) - glow blending

## Renderer Architecture

The rendering system uses a backend-agnostic interface to support multiple graphics APIs:

\htmlonly
<pre class="mermaid">
classDiagram
    class IRenderer {
        <<interface>>
        +Init() bool
        +Shutdown()
        +BeginFrame()
        +EndFrame()
        +DrawSprite()
        +DrawSpriteRegion()
        +DrawSpriteAlpha()
        +DrawFilledRect()
        +SetProjectionMode()
        +RequiresYFlip() bool
    }

    class OpenGLRenderer {
        -m_ShaderProgram
        -m_BatchVAO
        -m_BatchVBO
        -m_TextureCache
        +FlushBatch()
    }

    class VulkanRenderer {
        -m_Instance
        -m_Device
        -m_SwapChain
        -m_CommandPool
        +CreatePipeline()
    }

    class RendererFactory {
        +Create(backend)$ IRenderer*
        +GetAvailableBackends()$ vector
    }

    IRenderer <|.. OpenGLRenderer : implements
    IRenderer <|.. VulkanRenderer : implements
    RendererFactory ..> IRenderer : creates

    style IRenderer fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    style OpenGLRenderer fill:#134e3a,stroke:#10b981,color:#e2e8f0
    style VulkanRenderer fill:#134e3a,stroke:#10b981,color:#e2e8f0
    style RendererFactory fill:#4a2020,stroke:#ef4444,color:#e2e8f0
</pre>
\endhtmlonly

**Legend:** 🟦 Interface - 🟩 Implementations - 🟥 Factory

The `IRenderer` interface provides all drawing operations. Game code calls these methods without knowing which backend is active:

```cpp
// Game code doesn't know if this is OpenGL or Vulkan
renderer->DrawSprite(texture, position, size, rotation);
renderer->DrawFilledRect(rect, color);
```

The `RendererFactory` creates the appropriate backend based on configuration or availability:

```cpp
IRenderer* renderer = RendererFactory::Create(RenderBackend::OpenGL);
```

## See Also

- [Architecture](ARCHITECTURE.md) - System design overview
- [Time System](TIME_SYSTEM.md) - Ambient lighting that affects rendering
