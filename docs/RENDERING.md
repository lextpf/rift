# Rendering Pipeline

This document describes the coordinate systems, transformations, and rendering techniques used by Rift.

## Coordinate System

Rift uses a **top-left origin, Y-down** coordinate system measured in pixels.

This matches typical 2D game and UI conventions where:
- Origin $ (0, 0) $ is at the **top-left** corner
- $ \hat{x} = (1, 0) $ points **right**
- $ \hat{y} = (0, 1) $ points **down**

## Coordinate Spaces

The flat 2.5D pipeline transforms vertices through several coordinate spaces. Everything up to
clip space happens on the CPU: the renderer builds each quad's four corners in view space and
writes them into a batch buffer, so the only matrix the 2D vertex shader applies is the
projection.

\htmlonly
<pre class="mermaid">
flowchart LR
  classDef space fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
  classDef transform fill:#134e3a,stroke:#10b981,color:#e2e8f0
  classDef auto fill:#4a2020,stroke:#ef4444,color:#e2e8f0

  subgraph CPU ["Game code + renderer CPU"]
    World["World Space (world px)"]:::space
    Camera["-camera"]:::transform
    View["View Space (world px)"]:::space
    Local["Quad corners (0,0)..(sx,sy)"]:::space
    Model["Corner transform S, R about c, T(p)"]:::transform
    Batch["Batch vertices (view space)"]:::space
  end

  subgraph Shader ["Vertex Shader"]
    Proj["projection (scene or UI ortho)"]:::transform
    Clip["Clip Space [-w,w] (x, y, z, w)"]:::space
  end

  subgraph GPU ["Automatic GPU"]
    WDiv["/w"]:::auto
    NDC["NDC [-1,1] (x, y, z)"]:::space
    Viewport["viewport transform"]:::auto
    Screen["Screen Space [0,W]x[0,H] px"]:::space
  end

  World --> Camera --> View
  Local --> Model --> Batch
  View -.->|"position"| Model
  Batch --> Proj --> Clip
  Clip --> WDiv --> NDC --> Viewport --> Screen
</pre>
\endhtmlonly

**Legend:** 🟦 Coordinate spaces - 🟩 Transforms we implement - 🟥 GPU fixed-function

The 2D shader still declares a `model` uniform, but every batched path leaves it at identity;
only the Vulkan glyph path pushes a non-identity model matrix. The world-space 3D path
(`DrawQuad3D`) skips this chain entirely - see [World-Space 3D Path](#world-space-3d-path).

### World Space

Absolute pixel coordinates in the game world. Tile width and height are per-map data
(`Tilemap::GetTileWidth()` / `GetTileHeight()`) and are independent of each other, so a tile at
grid position $(tx, ty)$ has world coordinates:

$$
\vec{p}_{world} = (tx \times tileWidth, ty \times tileHeight)
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

**Pixel scale and zoom:**

Screen pixels are not world pixels. `Game::PIXEL_SCALE` (5) is the integer upscale factor for the
pixel art, and camera zoom divides on top of it, so the visible world extent is:

$$
\vec{s}_{view} = \frac{\vec{s}_{screen}}{PIXEL\_SCALE \times zoom}
$$

That is `viewScaling::VisibleWorldSizeZoomed()`, the single source of truth shared by the ortho
projection, `IRenderer::SetViewSize()` and every culling test.

|                   | Zoom=1            | Zoom=2            |
|-------------------|-------------------|-------------------|
| **Screen size**   | 1920x1080 px      | 1920x1080 px      |
| **Visible world** | 384x216 world px  | 192x108 world px  |

**How zoom works - we change the projection matrix:**

```cpp
// Zoom is applied by changing ortho() parameters
// (CameraController::GetOrthoProjection, called from Game::Render)
glm::mat4 P = glm::ortho(0.0f, visibleWidth, visibleHeight, 0.0f, -1.0f, 1.0f);
```

| Zoom | ortho() right edge | Effect                                           |
|------|--------------------|--------------------------------------------------|
| 1.0  | 384                | View coord $384 \rightarrow NDC +1$ (right edge) |
| 2.0  | 192                | View coord $192 \rightarrow NDC +1$ (right edge) |

At zoom=2, the projection maps a **smaller** view range to the **same** NDC range $[-1,+1]$. This makes everything appear larger - a 16px sprite that was $\frac{16}{384} \approx 4.2\%$ of screen width is now $\frac{16}{192} \approx 8.3\%$.

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

The vertex buffer holds no shared unit quad. Each draw builds four local corners sized to the
sprite, with the origin at the sprite's top-left:

$$
\vec{p}_{local} \in \{(0,0),\ (s_x,0),\ (s_x,s_y),\ (0,s_y)\}
$$

The renderer rotates those corners about the sprite center, adds the view-space position, and
pushes the result as six vertices (see [Batch Structure](#batch-structure)). Local space
therefore never reaches the GPU.

### Clip Space

The 2D vertex shader applies only the projection, because the batch vertices are already in view
space:

$$
\vec{p}_{clip} = P \cdot \vec{p}_{view}^h
$$

Where $\vec{p}_{view}^h = (x, y, 0, 1)^T$ is the homogeneous view-space position. $z$ is 0 for
every 2D sprite: the flat path binds no depth buffer and relies on submission order for layering.

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

### Sprite Corner Transform

The corner transform maps the unit quad to view-space pixels with position, scale, and rotation.
It runs on the CPU - `DrawSpriteRegion` builds the corners and `IRenderer::RotateCorners` rotates
them - but the matrix below is the exact composition it performs:

$$
M = T(\vec{p}) \cdot T(\vec{c}) \cdot R_z(\theta) \cdot T(-\vec{c}) \cdot S(\vec{s})
$$

Where:
- $\vec{p} = (p_x, p_y)$ - sprite position in view pixels
- $\vec{s} = (s_x, s_y)$ - sprite size in pixels
- $\vec{c} = \frac{1}{2}\vec{s}$ - sprite center (rotation pivot)
- $\theta$ - rotation angle in radians; the `IRenderer` methods take **degrees** and convert.
  Positive $\theta$ turns clockwise on screen, because Y points down.

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

Maps view pixels $[0, w] \times [0, h]$ to NDC $[-1, 1] \times [-1, 1]$ while preserving Y-down.
A frame installs two of these through `SetProjection`: the **scene ortho**, whose extent is the
zoomed world view, and - after the post-FX composite - the **UI ortho**, measured in screen
pixels. Each `SetProjection` call drains every pending batch, so it is also a hard painter-order
barrier.

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

The full transformation is split: the CPU applies $M$ when it builds the batch vertices, the
vertex shader applies $P$.

$$
\vec{p}_{clip} = P \cdot \underbrace{M \cdot \vec{p}_{local}^h}_{\text{CPU, per corner}}
$$

```glsl
// shaders/Geometry.vert - model is identity for every batched path
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

**Backend convention:**

| API    | Raw API convention | Rift sampling convention |
|--------|--------------------|--------------------------|
| OpenGL | Texture origin is bottom-left | `flipY=true` maps top-left image pixels to top-left sprites |
| Vulkan | UV origin is top-left, but tilesets are pre-flipped at load | Keeps the same `flipY=true` convention so one call site feeds both backends |

Both backends return `true` from `IRenderer::RequiresYFlip()`, so `flipY` is effectively always
true in engine code. It is distinct from `tileFlipX` / `tileFlipY`, which mirror the sampled
source region per tile and are applied before rotation.

### Vulkan Texture Uploads

Vulkan texture uploads must happen outside an active render pass. A draw call that references a
texture without a Vulkan image view uses the renderer's white fallback texture rather than stalling
to upload mid-frame:

\htmlonly
<pre class="mermaid">
sequenceDiagram
    participant Game
    participant Renderer as "VulkanRenderer"
    participant Texture
    participant GPU

    Game->>Renderer: UploadTexture(texture)
    Renderer->>Texture: CreateVulkanTexture(...)
    Texture->>GPU: Staging copy + image layout transition
    Game->>Renderer: DrawSpriteRegion(texture, ...)
    Renderer->>Texture: GetVulkanImageView()
    alt Texture uploaded
        Renderer->>GPU: Bind descriptor and draw textured quad
    else Texture missing image view
        Renderer->>GPU: Bind white fallback and draw quad
    end
</pre>
\endhtmlonly

## Sprite Batching

OpenGL batches consecutive sprites that share the same texture into a single draw call. When the
texture changes, the current OpenGL batch is flushed and a new batch begins. Vulkan currently
submits each quad directly with cached descriptor sets and persistent per-frame vertex buffers.

\htmlonly
<pre class="mermaid">
sequenceDiagram
    participant Game
    participant OpenGL
    participant GLBatch as "OpenGL Batch"
    participant Vulkan
    participant GPU

    Game->>OpenGL: DrawSprite(tex1, pos1)
    OpenGL->>GLBatch: Add vertices

    Game->>OpenGL: DrawSprite(tex1, pos2)
    OpenGL->>GLBatch: Add vertices

    Game->>OpenGL: DrawSprite(tex2, pos3)
    Note over OpenGL: Texture changed
    OpenGL->>GPU: Flush batch (tex1)
    OpenGL->>GLBatch: Add vertices (tex2)

    Game->>Vulkan: DrawSprite(tex1, pos1)
    Vulkan->>GPU: vkCmdDraw(quad)
</pre>
\endhtmlonly

### Batch Structure

Each sprite adds **6 vertices** (2 triangles) to the batch - corners 0 (TL) and 2 (BR) are
duplicated rather than using an index buffer:

```cpp
struct BatchVertex {
    float x, y;    // View-space position (camera already subtracted)
    float u, v;    // UV coordinates
};

// Two triangles per quad, counter-clockwise (corners 0 and 2 duplicated)
//  0 -------- 1
//  | \        |
//  |   \      |  Triangle 1: 0-2-3
//  |     \    |  Triangle 2: 0-1-2
//  |       \  |
//  3 -------- 2
```

The OpenGL backend keeps four independent 2D batches plus the world-space 3D batch. They differ
in vertex format and in what ends them early:

| Batch     | Vertex format          | Own flush trigger                |
|-----------|------------------------|----------------------------------|
| Sprites   | position + UV          | Texture change                   |
| Rects     | position + UV + RGBA   | Blend-mode change                |
| Particles | position + UV + RGBA   | Texture or blend-mode change     |
| Text      | position + UV + RGBA   | Atlas change, quad budget        |
| 3D quads  | scene position + UV + RGBA | Texture, blend or depth mode |

`MAX_BATCH_SPRITES` (10000 quads) caps the sprite, rect, particle and 3D buffers; text has its
own `MAX_TEXT_QUADS` budget and does not flush per `DrawText` call.

Beyond its own trigger, a batch is also drained by:
1. A full buffer, or a switch to a different batch type.
2. `SetProjection` and `EndFrame` - these drain every batch, 2D and 3D.
3. `SetAmbientColor` (sprite batch), `SetViewProjection` (3D batch), `DrawQuad3D` (sprite, rect
   and particle batches first).
4. `BeginScene` / `EndSceneApplyPostFX` - every batch except text, so text queued inside the
   scene pass reaches the swapchain after the composite and is never graded.

Cross-type drains are asymmetric. The drain matrix in `OpenGLRenderer.hpp` is the authority on
which call pairs preserve painter order.

## Upright Tiles

Some tiles (buildings, signs) represent objects that stand up rather than lying flat on
the ground. These carry `TileStance::Structure` (see `src/TileStance.hpp`). In the flat
2D pipeline they are drawn exactly like any other tile - the stance changes only their
draw ORDER, never their geometry. The `world3d` orbit-camera path is where a stance
becomes real geometry, turning the tile into an upright billboard.

`stance` is a per-map-cell, per-layer field replacing the old `noProjection` boolean. The flat
pipeline only distinguishes `Structure` from everything else; the remaining stances (`Prop`,
`Wall`) are 3D concepts and read as ordinary flat artwork here. Maps written before the field
existed are migrated once, at load - see `Tilemap::LoadMapFromJSON`.

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

Enabled via the `additive` flag on `DrawSpriteAlpha`, `DrawSpriteAtlas` and `DrawColoredRect`.
OpenGL honours it; the Vulkan 2D path ignores it until it has a second additive-blend pipeline.
`DrawQuad3D` is unaffected: it takes an explicit `renderModes::BlendMode`, and Vulkan pre-builds
one pipeline per blend/depth combination, so additive world geometry works on both backends.

## Render Order

`Game::Render()` selects one of three self-contained paths and never mixes them:

| Path            | Entry point         | When                                   |
|-----------------|---------------------|----------------------------------------|
| Title screen    | `RenderTitleFrame`  | `GameMode::Title`                      |
| World-space 3D  | `RenderFrame3D`     | `world3d` console toggle is on         |
| Flat 2.5D       | rest of `Render()`  | default gameplay path                  |

The flat path renders in this order for correct depth. `BeginFrame()` then `BeginScene()` run
first, so steps 1-10 accumulate in the offscreen scene target; `EndSceneApplyPostFX()` ends that
target, and every pass after it draws straight to the swapchain.

\htmlonly
<pre class="mermaid">
flowchart LR
    subgraph Background
        A1["1. Clear (sky color)"]
        A2["2. Background layers"]
        A3["3. Upright background tiles"]
    end

    subgraph YSorted["World Depth Pass"]
        B1["4. Authored baseline + structure-local support constraints<br/>(atomic characters)"]
    end

    subgraph Foreground
        C1["5. Upright foreground tiles"]
        C2["6. Upright-tile particles"]
        C3["7. Foreground layers"]
        C4["8. World particles"]
    end

    subgraph Sky["Lights and Sky"]
        D1["9. World light pools (additive)"]
        D2["10. Sky / ambient overlay"]
    end

    subgraph Post["Composite"]
        E1["11. EndSceneApplyPostFX"]
    end

    subgraph UI["Swapchain, ungraded"]
        F1["12. Editor / UI overlays"]
        F2["13. Dialogue, debug HUD, console"]
    end

    Background --> YSorted --> Foreground --> Sky --> Post --> UI
</pre>
\endhtmlonly

Steps 3 and 5 are the upright (`TileStance::Structure`) tiles, drawn by
`Tilemap::RenderBackgroundLayersNoProjection` / `RenderForegroundLayersNoProjection`. Step 9 only
contributes once `TimeManager::GetStarVisibility()` exceeds 0.01, and each light is further
scaled by `ComputeLightIntensity(schedule, hour)`.

### World Depth Algorithm

Explicit Y-sort tiles, elevated object/foreground tiles, and characters are collected into one
list. The first pass preserves the map's authored roles:

```text
Background → Y-sorted actors/tiles → Foreground
```

Inside a phase, authored painter depth remains:

$$
depth = anchorY
$$

Elevation is intentionally **not** a global depth offset. Plane relationships
are added by the local structure constraints below; folding elevation into
every Y-sort comparison would make ramp and railing art cover unrelated actors
standing beside the structure.

```cpp
struct Drawable {
    DrawablePhase phase;   // authored background / Y-sort / foreground role
    int surfaceRegionId;   // connected elevation footprint, or -1
    SupportSurface supportSurface; // actor topology; ignored for tiles
    float sortY;           // authored depth key; smaller sorts further back
    float supportHeight;   // metadata for the local support relationship
    bool isYSortMinus;     // tile occlusion flag; false for entities
    std::uint8_t tieBias;  // equal-depth order: tile (4), NPC (3), player (1)
    DrawableClass cls;
};
```

The elevation map is flood-filled into connected runtime region IDs. Explicit Y-sort artwork
inherits a region through its connected Y-sort component or authored structure ID, allowing
railings and visual overhangs outside the walkable cells to stay associated with the bridge.

After baseline sorting, a stable topological pass adds only these local constraints:

```text
ground actor inside region → every tile of that region          (walking underneath)
background surface tile → elevated actor in the same region     (walking on the deck)
authored Y-sort tile ↔ elevated actor in the same region         (railings)
```

An actor beside the ramp has no elevation-region ID, so none of these constraints applies.
`ySortPlus` and `ySortMinus` always retain their authored Y-sorted phase—even when the artwork is
stored on a foreground layer—and therefore keep the same tie/anchor behavior the map author set.
The same railings are explicitly evaluated against deck actors and are forced above a ground actor
only while that actor is actually under the bridge footprint.

Each character contributes one atomic queue item. The current player/NPC renderer still submits
two sprite regions, but both submissions execute consecutively from that item. No tile can be
inserted between feet, body, hair, hats, equipment, or a future taller sprite; sprite dimensions
are not part of the sorting contract.

The ground and ground-detail layers stay in the fixed background pass. On a non-zero elevation
cell, object and foreground artwork is automatically promoted out of its fixed pass and into the
world depth queue, preventing double rendering while retaining the layer's original phase.

## Post-Processing

`BeginScene()` redirects the scene into an offscreen target; `EndSceneApplyPostFX(params)`
composites it into the swapchain. Everything drawn afterwards - editor, dialogue, debug HUD,
console - bypasses the chain and stays sharp and ungrained.

On OpenGL the composite runs an HSV-saturation bright pass over the scene texture, builds and
additively upsamples a bloom mip chain, then draws a full-screen triangle that applies, in order
(`shaders/PostFXComposite.frag`):

1. Scene sample with radial chromatic aberration (3 fetches).
2. Chroma-only bloom add - luma-orthogonal, so it tints without brightening.
3. Lift/gamma/gain grading, split per time of day.
4. Saturation pump.
5. Vignette + edge desaturation (elliptical smoothstep from screen center).
6. Film grain (luminance-modulated, 2x2 pixel tiles).
7. Soft-shoulder tonemap.

`PostFXParams::postFXEnabled` is the master gate; when it is 0 the shader returns the raw scene
texel. The console command is `postfx [on|off|toggle]`.

`PostFXComposite.frag` is OpenGL-only: it is not in CMake's `SHADER_SOURCES`, is never compiled
to SPIR-V, and its default-block uniforms are illegal in Vulkan GLSL. On Vulkan both
`BeginScene()` and `EndSceneApplyPostFX()` are no-ops - the scene has already rendered to the
swapchain, so every field of `params` is ignored and the frame ships without bloom or grading.

## World-Space 3D Path

The `world3d` console toggle switches `Game::Render()` to `RenderFrame3D()`, an in-progress
world-space orbit camera. It is not the default path and does not replace the flat pipeline; both
are compiled in and the flat path is untouched while the toggle is off.

The 3D path does not use the 2D primitives at all. `CameraRig` builds one `projection * view`
matrix, published through `SetViewProjection()`, and every piece of scene geometry - ground
tiles, upright billboards, characters, particles, light pools - is submitted through
`DrawQuad3D()` as four scene-space corners. Unlike every other draw method, `DrawQuad3D` does
**not** take camera-pre-subtracted coordinates: the matrix does that work. Depth is a real depth
buffer, selected per quad by `renderModes::DepthMode`, instead of submission order, and
`Frustum` planes extracted from the same matrix cull off-screen geometry.

`shaders/Geometry3D.vert` / `.frag` back this path; `Geometry.vert` / `.frag` stay in use for the
flat world and for all screen-space UI, which must never be projected.

Tile stance becomes real geometry here: `TileStance::Prop` turns to face the camera,
`TileStance::Wall` and `TileStance::Structure` stay locked to the grid as upright surfaces (see
[Upright Tiles](#upright-tiles)).

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

$t_{in}$ is an absolute number of seconds per type (0.15 s for rain, 0.5 s for fireflies, 4 s for
fog), while $t_{out}$ is usually a fraction of $t_{max}$ (0.3 for fireflies, 0.4 for fog).

Combined fade envelope:
$$
\alpha_{fade} = \alpha_{in} \cdot \alpha_{out}
$$

Each behavior multiplies this envelope by its own animation term and a per-type base alpha, so
$\alpha_{fade}$ is a ceiling, not the final alpha.

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
- $\omega_x, \omega_y$ = angular frequency (radians/second)
- $k$ = phase multiplier for Y (creates varied paths)

Fireflies use $A_x = 10$, $A_y = 8$, $\omega_x = 2$, $\omega_y = 1.5$, $k = 1.3$. The drift is
added on top of the Euler step above, not instead of it.

### Alpha Animation

**Pulsing Glow (Fireflies):**

A unit sine pulse on global time, scaled by the fade envelope and the type's base alpha (0.7):
$$
\alpha_{pulse} = \tfrac{1}{2} + \tfrac{1}{2}\sin(\omega t + \phi), \quad \omega = 4
$$
$$
\alpha = \alpha_{pulse} \cdot \alpha_{fade} \cdot 0.7
$$

Per-particle $\phi$ keeps a swarm out of lockstep.

**Sparkle Twinkle:**

Sparkles use a fast attack and a quadratic decay over life progress $\ell$, which reads as a
twinkle instead of a strobe:
$$
\alpha = \min\left(1, \frac{\ell}{0.12}\right) \cdot (1 - \ell)^2 \cdot 0.85
$$

### Rotation

Angular velocity $\overset{\cdot}{\theta}$ is in **degrees per second** and varies by particle
phase:
$$
\overset{\cdot}{\theta} = \omega_{base} + \frac{\phi}{2\pi} \cdot \omega_{range}
$$

Fireflies use $\omega_{base} = 20$, $\omega_{range} = 40$ (20-60 deg/s); drifting leaves use
$30$ and $60$ (30-90 deg/s).

Direction alternates on the same phase value, so roughly half of each population spins the other
way:
$$
\overset{\cdot}{\theta}' = \begin{cases}
-\overset{\cdot}{\theta} & \text{if } \phi \bmod 2 < 1 \\\\
+\overset{\cdot}{\theta} & \text{otherwise}
\end{cases}
$$

### Particle Rendering Order

`ParticleSystem::Render` is called twice per frame: once for the particles that ride upright
tiles (step 6 of the render order) and once for world particles (step 8), both before the light
pools and the sky.

Within each call, the visible particles are partitioned - not sorted - by blend mode, so relative
order inside a group is the spawn order:

1. Non-additive particles (fog, rain) - standard alpha blending
2. Additive particles (fireflies, sparkles, wisps) - glow blending

Upright-tile particles are never viewport-culled, because they are projected onto the structure
mesh underneath them. World particles are culled against the view rect with a size-based pad.

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
        +BeginScene()
        +EndSceneApplyPostFX(params)
        +DrawSprite()
        +DrawSpriteRegion()
        +DrawSpriteAlpha()
        +DrawSpriteAtlas()
        +DrawColoredRect()
        +DrawQuad3D()
        +DrawText()
        +SetProjection()
        +SetViewProjection()
        +SetViewport()
        +Clear()
        +UploadTexture()
        +SetAmbientColor()
        +RequiresYFlip() bool
        +GetBackendInfo() RendererInfo
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

    class RendererFactory_h {
        <<free functions>>
        +CreateRenderer(api, window)$ unique_ptr
        +IsRendererAvailable(api)$ bool
    }

    IRenderer <|.. OpenGLRenderer : implements
    IRenderer <|.. VulkanRenderer : implements
    RendererFactory_h ..> IRenderer : creates

    style IRenderer fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    style OpenGLRenderer fill:#134e3a,stroke:#10b981,color:#e2e8f0
    style VulkanRenderer fill:#134e3a,stroke:#10b981,color:#e2e8f0
    style RendererFactory_h fill:#4a2020,stroke:#ef4444,color:#e2e8f0
</pre>
\endhtmlonly

**Legend:** 🟦 Interface - 🟩 Implementations - 🟥 Factory

The `IRenderer` interface provides all drawing operations. Game code calls these methods without knowing which backend is active:

```cpp
// Game code doesn't know if this is OpenGL or Vulkan
renderer->DrawSprite(texture, position, size, rotation);
renderer->DrawColoredRect(position, size, color);
```

`RendererFactory.hpp` provides free helper functions that create the appropriate backend based on
configuration or availability. `api` is taken by reference so the factory can report the backend
it actually created after a fallback:

```cpp
RendererAPI api = RendererAPI::OpenGL;
std::unique_ptr<IRenderer> renderer = CreateRenderer(api, window);
```

Both backends are always compiled in - Vulkan is a hard `find_package(Vulkan REQUIRED)` - so
there is no build configuration in which one of them is absent.

Two constraints follow from that design:

- **Override sets must stay identical.** Adding, removing or re-signing a pure virtual in
  `IRenderer.hpp` requires the matching edit in `RendererMacros.hpp`.
  `RIFT_DECLARE_COMMON_RENDERER_METHODS` is what keeps both backends byte-identical, so the two
  files are always edited together.
- **Nothing may cache an `IRenderer*`.** The `renderer.set opengl|vulkan` console command
  destroys and recreates the GLFW window **and** the renderer, after which `TextureStore`
  re-uploads every texture. Any pointer, reference or texture handle captured before the switch
  dangles; take the renderer by reference per call instead.

## See Also

- [Architecture](ARCHITECTURE.md) - System design overview
- [Time System](TIME_SYSTEM.md) - Ambient lighting that affects rendering
