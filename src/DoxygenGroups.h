/**
 * @file DoxygenGroups.h
 * @brief Doxygen module definitions and hierarchical documentation structure.
 * @author Alex (https://github.com/lextpf)
 *
 * This file defines the logical groupings (modules) used throughout the codebase
 * documentation. Each module represents a cohesive subsystem of the game engine.
 *
 * @par Architecture Overview
 * The game engine is organized into five primary subsystems:
 *
 * \htmlonly
 * <pre class="mermaid">
 * flowchart LR
 * classDef core fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 * classDef render fill:#2e1f5e,stroke:#8b5cf6,color:#e2e8f0
 * classDef world fill:#134e3a,stroke:#10b981,color:#e2e8f0
 * classDef entity fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 * classDef input fill:#164e54,stroke:#06b6d4,color:#e2e8f0
 *
 * Core["Core Engine"]:::core
 * Rendering["Rendering System"]:::render
 * World["World System"]:::world
 * Entities["Entity System"]:::entity
 * Input["Input System"]:::input
 *
 * Core --> Rendering
 * Core --> World
 * Core --> Entities
 * Core --> Input
 * Entities --> World
 * Rendering --> World
 * </pre>
 * \endhtmlonly
 */

/**
 * @defgroup Core Core Engine
 * @brief Core engine components including the main game loop, state management, and orchestration.
 *
 * The Core module provides the foundational infrastructure for the game:
 *
 * @par Responsibilities
 * - **Game Loop**: Implements a variable-timestep game loop
 * - **State Management**: Handles transitions between game modes
 * - **System Orchestration**: Coordinates updates across all subsystems
 * - **Resource Lifetime**: Manages initialization and shutdown of game resources
 *
 * @par Game Loop Model
 * The game uses a semi-fixed timestep model:
 * @code
 * while (running) {
 *     float deltaTime = currentTime - lastFrameTime;
 *     ProcessInput(deltaTime);
 *     Update(deltaTime);
 *     Render();
 * }
 * @endcode
 *
 * @par Update Order
 * 1. Input processing (keyboard, mouse)
 * 2. Player movement and collision
 * 3. NPC AI and movement
 * 4. Camera following
 * 5. Animation updates
 * 6. Rendering
 *
 * @see Game
 */

/**
 * @defgroup Rendering Rendering System
 * @brief Graphics abstraction layer supporting OpenGL and Vulkan backends.
 *
 * The Rendering module provides a unified interface for 2D sprite rendering,
 * abstracting away the differences between graphics APIs.
 *
 * @par Design Pattern
 * Uses the **Strategy Pattern** via the IRenderer interface, allowing runtime
 * selection of the graphics backend without changing game code.
 *
 * @par Coordinate System
 * - **World Space**: Game coordinates in pixels, origin at top-left
 * - **Screen Space**: Pixel coordinates after camera transformation
 * - **Normalized Device Coordinates (NDC)**: -1 to 1 range used by GPU
 *
 * @par Transformation Pipeline
 * @f[
 * \vec{p}_{screen} = \vec{p}_{world} - \vec{p}_{camera}
 * @f]
 * @f[
 * \vec{p}_{ndc} = M_{projection} \cdot \vec{p}_{screen}
 * @f]
 * Where \f$ M_{projection} \f$ is an orthographic projection matrix:
 * @f[
 * M_{ortho} = \begin{bmatrix}
 *   \frac{2}{w} & 0 & 0 & -1 \\
 *   0 & \frac{-2}{h} & 0 & 1 \\
 *   0 & 0 & -1 & 0 \\
 *   0 & 0 & 0 & 1
 * \end{bmatrix}
 * @f]
 *
 * Applying the projection to a screen-space point:
 * @f[
 * \begin{pmatrix} x_{clip} \\ y_{clip} \\ z_{clip} \\ 1 \end{pmatrix}
 * = M_{ortho} \begin{pmatrix} x \\ y \\ z \\ 1 \end{pmatrix}
 * = \begin{pmatrix} \frac{2x}{w} - 1 \\ 1 - \frac{2y}{h} \\ -z \\ 1 \end{pmatrix}
 * \Bigg|_{z=0}
 * = \begin{pmatrix} \frac{2x}{w} - 1 \\ 1 - \frac{2y}{h} \\ 0 \\ 1 \end{pmatrix}
 * @f]
 *
 * @note In 2D rendering, \f$ z = 0 \f$ for all sprites (set in vertex shader).
 * Depth sorting uses draw order (painter's algorithm), not z-buffer.
 *
 * @par Sprite Batching
 * Both renderers support sprite batching for efficient rendering of tilemaps
 * and multiple entities in a single draw call.
 *
 * @par Coordinate Transformation
 * Mouse input must be transformed through three coordinate spaces to determine
 * which tile the cursor is over. The camera can pan (move) and zoom,
 * so screen position alone doesn't tell us where in the game world the mouse is pointing.
 *
 * **Step 1: Screen -> World**
 * @f[
 * world_x = \frac{screen_x}{screen\_width} \times \frac{base\_width}{zoom} + camera_x
 * @f]
 *
 * Where:
 * - `screen_x / screen_width` = normalized screen position (0.0 to 1.0)
 * - `base_width` = viewport size in world units (tiles_visible * tile_size)
 * - `zoom` = camera zoom factor (1.0 = normal, 2.0 = zoomed in 2x)
 * - `camera_x` = camera offset (what world coordinate is at screen left edge)
 *
 * **Step 2: World -> Tile**
 * @f[
 * tile_x = \lfloor \frac{world_x}{tile\_size} \rfloor
 * @f]
 *
 * Floor division converts continuous world coordinates to discrete tile indices.
 *
 * **Example:**
 * - Screen: 1280x720, Mouse at (640, 360) = center
 * - Viewport: 20x12 tiles x 16px = 320x192 world units
 * - Camera at (100, 50), Zoom = 2.0
 *
 * @code
 * // Visible world area shrinks when zoomed in
 * worldWidth = 320 / 2.0 = 160 world units visible
 *
 * // Screen center (0.5) maps to middle of visible area
 * worldX = 0.5 * 160 + 100 = 180
 *
 * // World position 180 is tile 11 (180 / 16 = 11.25, floor = 11)
 * tileX = floor(180 / 16) = 11
 * @endcode
 *
 * **Implementation:**
 * @code{.cpp}
 * // Get mouse position in screen pixels
 * double mouseX, mouseY;
 * glfwGetCursorPos(window, &mouseX, &mouseY);
 *
 * // Calculate visible world dimensions (affected by zoom)
 * float baseWidth = tilesVisibleX * tileSize;  // e.g., 20 * 16 = 320
 * float worldWidth = baseWidth / cameraZoom;   // Shrinks when zoomed in
 *
 * // Transform screen -> world
 * float worldX = (mouseX / screenWidth) * worldWidth + cameraX;
 * float worldY = (mouseY / screenHeight) * worldHeight + cameraY;
 *
 * // Transform world -> tile (floor for correct negative handling)
 * int tileX = static_cast<int>(std::floor(worldX / tileSize));
 * int tileY = static_cast<int>(std::floor(worldY / tileSize));
 * @endcode
 *
 * @see IRenderer, OpenGLRenderer, VulkanRenderer
 */

/**
 * @defgroup World World System
 * @brief Game world representation including tilemaps, collision detection, and navigation.
 *
 * The World module manages the static game environment and provides spatial queries.
 *
 * @par Tilemap System
 * The tilemap uses a 10-layer architecture for depth sorting:
 *
 * | Layer | Name          | Render Order | Purpose             |
 * |-------|---------------|--------------|---------------------|
 * | 0     | Ground        | 0            | Base terrain        |
 * | 1     | Ground Detail | 10           | Paths, decorations  |
 * | 2     | Objects       | 20           | Buildings, rocks    |
 * | 3     | Objects2      | 30           | Additional objects  |
 * | 4     | Objects3      | 40           | Additional objects  |
 * | -     | NPCs          | (Y-sorted)   |                     |
 * | -     | Player        | (Y-sorted)   |                     |
 * | 5     | Foreground    | 100          | In front of NPCs    |
 * | 6     | Foreground2   | 110          | Additional fg       |
 * | 7     | Overlay       | 120          | Overlay effects     |
 * | 8     | Overlay2      | 130          | Additional overlay  |
 * | 9     | Overlay3      | 140          | Top-most overlay    |
 *
 * @par Tile Indexing
 * Tiles are stored in row-major order:
 * @f[
 * i = y \times w + x
 * @f]
 *
 * @par Collision Detection
 * Uses Axis-Aligned Bounding Box (AABB) collision with a discrete tile grid:
 * @f[
 * c = (A_{min} < B_{max}) \land (A_{max} > B_{min})
 * @f]
 *
 * Applied to both X and Y axes for 2D collision.
 *
 * @par Navigation Map
 * The navigation system provides walkability information for NPC pathfinding.
 * It's independent of collision (a tile can have collision but be walkable,
 * useful for triggers or special tiles).
 *
 * @see Tilemap, CollisionMap, NavigationMap
 */

/**
 * @defgroup Entities Entity System
 * @brief Game entities including the player character and non-player characters (NPCs).
 *
 * The Entities module manages all dynamic objects in the game world.
 *
 * @par Position Convention
 * All entities store their position as **anchor position** (bottom-center of sprite):
 * @f[
 * \vec{p}_{anchor} = (center_x, bottom_y)
 * @f]
 *
 * This convention simplifies depth sorting and tile alignment.
 *
 * @par Hitbox Model
 * Entities use rectangular hitboxes for collision detection:
 * - **Player**: 16x16 pixels, centered on tile
 * - **NPC**: 16x16 pixels, centered on tile
 *
 * The hitbox is positioned relative to the anchor:
 * @f[
 * h_{min} = (anchor_x - \frac{w}{2}, anchor_y - h)
 * @f]
 * @f[
 * h_{max} = (anchor_x + \frac{w}{2}, anchor_y)
 * @f]
 *
 * @par Animation System
 * Sprites use a frame-based animation system with walk cycles:
 * - 4 directions (Down, Up, Left, Right)
 * - 3 frames per direction (Idle, Step Left, Step Right)
 * - Walk sequence: [Step Left, Idle, Step Right, Idle] (4-frame cycle)
 *
 * @par Movement Modes
 * Player supports three movement modes with different speeds:
 * | Mode     | Speed  | Multiplier |
 * |----------|--------|------------|
 * | Walking  | 80     | 1.0x       |
 * | Running  | 152    | 1.9x       |
 * | Bicycle  | 160    | 2.0x       |
 *
 * @see PlayerCharacter, NonPlayerCharacter
 */

/**
 * @defgroup Input Input System
 * @brief Input handling for keyboard and mouse interactions.
 *
 * The Input module processes user input and translates it into game actions.
 * Input handling is centralized in Game::ProcessInput(), which runs once per frame.
 *
 * @par Input Modes
 * The game operates in two mutually exclusive modes:
 * - **Gameplay Mode**: Player movement, NPC interaction, collision detection, camera control
 * - **Editor Mode**: Tile placement, collision editing, NPC spawning, navigation editing
 *
 * Press **E** to toggle between modes.
 *
 * @par Input Priority
 * Input is processed hierarchically. Higher-priority handlers block lower ones:
 * 1. **Global toggles** (E, F3, F4) - always processed
 * 2. **Dialogue** - blocks movement when active (Esc to dismiss)
 * 3. **Editor controls** - only when editor mode is active
 * 4. **Player movement** - only in gameplay mode, outside dialogue
 *
 * @par Gameplay Controls
 * |      Key      |             Action               |
 * |---------------|----------------------------------|
 * |    W/A/S/D    | Move player (8-directional)      |
 * |     Shift     | Run (1.9x speed)                 |
 * |       B       | Toggle bicycle mode (2.0x speed) |
 * |       F       | Talk to NPC (when facing one)    |
 * |       X       | Copy/restore NPC appearance      |
 * |       C       | Cycle character sprite           |
 * |  Ctrl+Scroll  | Zoom camera                      |
 * |  Arrow Keys   | Pan camera (reset when moving)   |
 * |       Z       | Reset zoom to 1.0x               |
 *
 * @par Dialogue Controls
 * |      Key       |           Action            |
 * |----------------|-----------------------------|
 * | W/S or Up/Down | Navigate dialogue options   |
 * |   Enter/Space  | Confirm selection / advance |
 * |     Escape     | End dialogue                |
 *
 * @par Movement Modes
 * |  Mode   |  Speed   |       Collision        |
 * |---------|----------|------------------------|
 * | Walking |  80 px/s | Strict (full hitbox)   |
 * | Running | 152 px/s | Relaxed (center point) |
 * | Bicycle | 160 px/s | Relaxed (center point) |
 *
 * Diagonal movement is normalized to prevent faster speed:
 * @f[
 * \vec{v} = \hat{d} \times speed \times \Delta t
 * @f]
 *
 * @par Editor Controls
 * |     Key      |                Action                 |
 * |--------------|---------------------------------------|
 * |      E       | Toggle editor mode                    |
 * |     1-0      | Select tilemap layer (1-5 bg, 6-0 fg) |
 * |      T       | Toggle tile picker                    |
 * |      R       | Rotate selection 90 deg               |
 * |    Delete    | Remove tile at cursor                 |
 * |      S       | Save map to JSON                      |
 * |      L       | Load map from JSON                    |
 * |      M       | Toggle navigation editing             |
 * |      N       | Toggle NPC placement                  |
 * |      H       | Toggle elevation editing              |
 * |      B       | Toggle no-projection editing          |
 * |      Y       | Toggle Y-sort-plus editing            |
 * |      O       | Toggle Y-sort-minus editing           |
 * |      J       | Toggle particle zone editing          |
 * |      K       | Toggle animated tile editing          |
 * |    , / .     | Cycle types (NPC/particle/anim)       |
 * |  Left Click  | Place tile/NPC/zone                   |
 * | Right Click  | Toggle collision/navigation           |
 * |    Arrows    | Pan tile picker                       |
 * | Shift+Arrows | Pan tile picker (fast)                |
 * |    Scroll    | Pan tile picker                       |
 * | Ctrl+Scroll  | Zoom                                  |
 *
 * @par Debug and Visual Controls
 * |   Key   |                         Action                          |
 * |---------|---------------------------------------------------------|
 * |   F1    | Switch renderer (OpenGL/Vulkan)                         |
 * |   F2    | Toggle debug overlays (collision, navigation, anchors)  |
 * |   F3    | Toggle FPS/position display                             |
 * |   F4    | Toggle 3D globe effect                                  |
 * |   F5    | Cycle time of day (day/evening/night/morning)           |
 * | Up/Down | Adjust 3D globe intensity                               |
 * |    X    | Toggle corner cut blocking on tile (debug mode only)    |
 *
 * @par Key Debouncing
 * Toggle keys use a flag pattern to prevent repeated triggers:
 * @code{.cpp}
 * static bool keyPressed = false;
 * if (glfwGetKey(window, KEY) == GLFW_PRESS && !keyPressed) {
 *     // Handle single press
 *     keyPressed = true;
 * }
 * if (glfwGetKey(window, KEY) == GLFW_RELEASE)
 *     keyPressed = false;
 * @endcode
 *
 * @see Game::ProcessInput, PlayerCharacter::Move
 */

/**
 * @defgroup Dialogue Dialogue System
 * @brief Interactive dialogue and conversation management for NPC interactions.
 *
 * The Dialogue module provides a flexible conversation system for player-NPC
 * interactions with branching dialogue trees and conditional responses.
 *
 * @par Architecture
 * The dialogue system consists of three main components:
 * - **DialogueManager**: Orchestrates dialogue flow and rendering
 * - **DialogueTypes**: Manages dialogue data and conversation state
 * - **DialogueNode/DialogueOption**: Data structures for dialogue trees
 *
 * @par Dialogue Flow
 * @code
 * Player presses F near NPC
 *         v
 * DialogueManager::StartDialogue()
 *         v
 * Display current DialogueNode text
 *         v
 * Show DialogueOptions (if any)
 *         v
 * Player selects option -> Jump to next node
 *         v
 * Repeat until end node or player exits
 * @endcode
 *
 * @par Dialogue Node Types
 * | Type      | Description                              |
 * |-----------|------------------------------------------|
 * | Text      | Simple text display, advances on input   |
 * | Choice    | Multiple selectable options              |
 * | Condition | Branch based on game state               |
 * | End       | Terminates the conversation              |
 *
 * @par Input Handling
 * During active dialogue, normal game input is blocked:
 * - **Up/Down/W/S**: Navigate options
 * - **Enter/Space**: Confirm selection or advance
 * - **Escape**: Exit dialogue early
 *
 * @see DialogueManager, DialogueNode, DialogueOption
 */

/**
 * @defgroup Effects Visual Effects System
 * @brief Particle systems, atmospheric effects, and visual enhancements.
 *
 * The Effects module provides dynamic visual elements that enhance the game's
 * atmosphere without affecting gameplay mechanics.
 *
 * @par Particle System
 * The particle system renders collections of small sprites with physics-based
 * motion. Particles are spawned in zones defined in the tilemap editor.
 *
 * @par Particle Types
 * | Type       | Behavior                                    |
 * |------------|---------------------------------------------|
 * | Leaves     | Float downward with wind sway               |
 * | Rain       | Fall rapidly with slight angle              |
 * | Snow       | Gentle descent with horizontal drift        |
 * | Fireflies  | Random wandering with glow pulsing          |
 * | Dust       | Slow drift with fade in/out                 |
 * | Sparkles   | Brief bright flashes at random positions    |
 *
 * @par Particle Lifecycle
 * @f[
 * \alpha(t) = \begin{cases}
 *   \frac{t}{t_{fade}} & t < t_{fade} \\
 *   1.0 & t_{fade} \leq t \leq t_{life} - t_{fade} \\
 *   \frac{t_{life} - t}{t_{fade}} & t > t_{life} - t_{fade}
 * \end{cases}
 * @f]
 *
 * @par Sky Effects
 * The SkyRenderer provides time-of-day atmospheric effects:
 * - **Stars**: Twinkling night sky with color variation
 * - **Shooting Stars**: Random meteor streaks
 * - **Sun/Moon Rays**: God ray effects from light sources
 * - **Dawn Gradient**: Color transitions during sunrise
 * - **Dew Sparkles**: Morning ground-level glints
 *
 * @par Performance
 * Particles use texture atlasing and instanced rendering for efficiency.
 * Each particle zone has configurable density and spawn rate limits.
 *
 * @see ParticleSystem, SkyRenderer, ParticleZone
 */
