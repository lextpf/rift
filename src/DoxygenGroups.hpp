// Documentation-only header

/**
 * @brief Doxygen module definitions and hierarchical documentation structure.
 * @author Alex (https://github.com/lextpf)
 *
 * This file defines the logical groupings (modules) used throughout the codebase
 * documentation. Each module represents a cohesive subsystem of the game engine.
 *
 * @par Architecture overview
 * The engine is organized into the eight modules declared below. An arrow means
 * "drives or reads from"; it is a documentation grouping, not a link-time
 * dependency graph.
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 * classDef core fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 * classDef render fill:#2e1f5e,stroke:#8b5cf6,color:#e2e8f0
 * classDef world fill:#134e3a,stroke:#10b981,color:#e2e8f0
 * classDef entity fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 * classDef input fill:#164e54,stroke:#06b6d4,color:#e2e8f0
 * classDef dialogue fill:#4c1d3d,stroke:#ec4899,color:#e2e8f0
 * classDef effects fill:#3b2a55,stroke:#a78bfa,color:#e2e8f0
 * classDef editor fill:#3f3a1c,stroke:#eab308,color:#e2e8f0
 *
 * Core["Core Engine"]:::core
 * Rendering["Rendering System"]:::render
 * World["World System"]:::world
 * Entities["Entity System"]:::entity
 * Input["Input System"]:::input
 * Dialogue["Dialogue System"]:::dialogue
 * Effects["Visual Effects"]:::effects
 * Editor["Editor and Tools"]:::editor
 *
 * Core --> Rendering
 * Core --> World
 * Core --> Entities
 * Core --> Input
 * Entities --> World
 * Rendering --> World
 * Entities --> Dialogue
 * Input --> Dialogue
 * Input --> Editor
 * Editor --> World
 * Editor --> Entities
 * Effects --> World
 * Effects --> Rendering
 * </pre>
 * @endhtmlonly
 */

/**
 * @addtogroup Core Core Engine
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
 * @par Game loop model
 * The loop uses a variable timestep clamped to a maximum frame delta. The delta is sampled
 * before polling, and polling runs before input so handlers see this frame's key state.
 * @code{.cpp}
 * while (running)
 * {
 *     float deltaTime = currentTime - lastFrameTime;
 *     glfwPollEvents();
 *     deltaTime = std::min(deltaTime, MAX_DELTA_TIME);
 *     ProcessInput(deltaTime);
 *     Update(deltaTime);
 *     Render();
 * }
 * @endcode
 *
 * @par Update order
 * 1. GLFW event polling.
 * 2. Input processing: keyboard, mouse, console and menus.
 * 3. World update: player, NPCs, particles, sky and camera, as the current mode permits.
 * 4. Rendering.
 *
 * @see Game
 */

/**
 * @addtogroup Rendering Rendering System
 * @brief Graphics abstraction layer supporting OpenGL and Vulkan backends.
 *
 * The Rendering module provides a unified interface for 2D sprite rendering,
 * abstracting away the differences between graphics APIs.
 *
 * @par Design pattern
 * Uses the **Strategy Pattern** via the IRenderer interface, allowing runtime
 * selection of the graphics backend without changing game code.
 *
 * @par Coordinate system
 * - **World Space**: Game coordinates in pixels, origin at top-left
 * - **Screen Space**: Pixel coordinates after camera transformation
 * - **Normalized Device Coordinates (NDC)**: -1 to 1 range used by GPU
 *
 * @par Transformation pipeline
 * @f[
 * \vec{p}_{screen} = \vec{p}_{world} - \vec{p}_{camera}
 * @f]
 * @f[
 * \vec{p}_{ndc} = M_{projection} \cdot \vec{p}_{screen}
 * @f]
 * Where @f$ M_{projection} @f$ is an orthographic projection matrix:
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
 * @note In 2D rendering @f$ z = 0 @f$ for every sprite, set in the vertex shader.
 * Depth sorting uses draw order (painter's algorithm), not z-buffer.
 *
 * @par Sprite batching
 * Both renderers support sprite batching for efficient rendering of tilemaps
 * and multiple entities in a single draw call.
 *
 * @par Coordinate transformation
 * Mouse input must be transformed through three coordinate spaces to determine
 * which tile the cursor is over. The camera can pan (move) and zoom,
 * so screen position alone does not identify the world position under the mouse.
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
 * @addtogroup World World System
 * @brief Game world representation including tilemaps, collision detection, and navigation.
 *
 * The World module manages the static game environment and provides spatial queries.
 *
 * @par Tilemap system
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
 * @par Tile indexing
 * Tiles are stored in row-major order:
 * @f[
 * i = y \times w + x
 * @f]
 *
 * @par Collision detection
 * Uses Axis-Aligned Bounding Box (AABB) collision with a discrete tile grid:
 * @f[
 * c = (A_{min} < B_{max}) \land (A_{max} > B_{min})
 * @f]
 *
 * Applied to both X and Y axes for 2D collision.
 *
 * @par Navigation map
 * The navigation system provides walkability information for NPC pathfinding.
 * It's independent of collision (a tile can have collision but be walkable,
 * useful for triggers or special tiles).
 *
 * @see Tilemap, CollisionMap, NavigationMap
 */

/**
 * @addtogroup Entities Entity System
 * @brief Game entities including the player character and non-player characters (NPCs).
 *
 * The Entities module manages all dynamic objects in the game world.
 *
 * @par Position convention
 * All entities store their position as **anchor position** (bottom-center of sprite):
 * @f[
 * \vec{p}_{anchor} = (center_x, bottom_y)
 * @f]
 *
 * This convention simplifies depth sorting and tile alignment.
 *
 * @par Hitbox model
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
 * @par Animation system
 * Sprites use a frame-based animation system with walk cycles:
 * - 4 directions (Down, Up, Left, Right)
 * - 3 frames per direction (Idle, Step Left, Step Right)
 * - Walk sequence: [Step Left, Idle, Step Right, Idle] (4-frame cycle)
 *
 * @par Movement modes
 * Player supports three movement modes with different speeds:
 * | Mode     | Speed      | Multiplier |
 * |----------|------------|------------|
 * | Walking  | 50.0 px/s  | 1.0x       |
 * | Running  | 87.5 px/s  | 1.75x      |
 * | Bicycle  | 112.5 px/s | 2.25x      |
 *
 * @see PlayerMovementSystem, PlayerModes
 */

/**
 * @addtogroup Input Input System
 * @brief Input handling for keyboard and mouse interactions.
 *
 * The Input module processes user input and translates it into game actions.
 * Input handling is centralized in Game::ProcessInput(), which runs once per frame.
 *
 * @par Input modes
 * The game operates in two mutually exclusive modes:
 * - **Gameplay Mode**: Player movement, NPC interaction, collision detection, camera control
 * - **Editor Mode**: Tile placement, collision editing, NPC spawning, navigation editing
 *
 * Open the developer console with **F12** and run
 * `editor on` to enter editor mode.
 *
 * @par Input priority
 * Input is processed hierarchically. Higher-priority handlers block lower ones:
 * 1. **Console toggle** (F12) - always processed; when open consumes all input
 * 2. **Dialogue** - blocks movement when active (Esc to dismiss)
 * 3. **Editor controls** - only when editor mode is active
 * 4. **Player movement** - only in gameplay mode, outside dialogue
 *
 * @par Gameplay controls
 * |      Key      |             Action                |
 * |---------------|-----------------------------------|
 * |    W/A/S/D    | Move player (8-directional)       |
 * |     Shift     | Run (1.75x speed)                 |
 * |       B       | Toggle bicycle mode (2.25x speed) |
 * |       F       | Talk to NPC (when facing one)     |
 * |  Ctrl+Scroll  | Zoom camera                       |
 * |  Arrow Keys   | Pan camera (reset when moving)    |
 * |       Z       | Reset zoom to 1.0x                |
 *
 * @par Dialogue controls
 * |      Key       |           Action            |
 * |----------------|-----------------------------|
 * | W/S or Up/Down | Navigate dialogue options   |
 * |   Enter/Space  | Confirm selection / advance |
 * |     Escape     | End dialogue                |
 *
 * @par Movement modes
 * |  Mode   |   Speed    |       Collision         |
 * |---------|------------|-------------------------|
 * | Walking |  50.0 px/s | Strict full-AABB hitbox |
 * | Running |  87.5 px/s | Strict full-AABB hitbox |
 * | Bicycle | 112.5 px/s | Strict full-AABB hitbox |
 *
 * Diagonal movement is normalized
 * to prevent faster speed:
 * @f[
 * \vec{v} = \hat{d} \times speed \times \Delta t
 * @f]
 *
 * @par Editor controls
 * The mode keys below only choose which sub-mode is active; what left/right
 * click then does in each mode is tabulated on @ref Editor and is not repeated
 * here.
 *
 * |     Key      |                       Action                        |
 * |--------------|-----------------------------------------------------|
 * |     1-0      | Select tilemap layer (1-5 bg, 6-0 fg)               |
 * |      T       | Toggle tile picker                                  |
 * |      R       | Rotate selection 90 deg                             |
 * |      F       | Flip brush / mirror selection (Shift+F = Y axis)    |
 * |    Delete    | Remove tile at cursor                               |
 * |      S       | Save map to JSON                                    |
 * |      L       | Load map from JSON                                  |
 * |      M       | Toggle navigation editing                           |
 * |      N       | Toggle NPC placement                                |
 * |      G       | Toggle structure editing (anchors, flood assign)    |
 * |      H       | Toggle elevation editing                            |
 * |      B       | Toggle stance editing (Flat/Prop/Wall/Structure)    |
 * |      Y       | Toggle Y-sort-plus editing                          |
 * |      O       | Toggle Y-sort-minus editing                         |
 * |      J       | Toggle particle zone editing (F = noProjection)     |
 * |      K       | Toggle animated tile editing                        |
 * |    Enter     | Commit collected frames (animation mode)            |
 * |    Escape    | Clear region selection / cancel structure anchor    |
 * |    , / .     | Cycle type or step value (mode-dependent)           |
 * |    Ctrl+Z    | Undo the last editor command                        |
 * |    Ctrl+Y    | Redo                                                |
 * |  Ctrl+drag   | Select a rectangular map region                     |
 * |    Ctrl+C    | Copy the selected region to the editor clipboard    |
 * |    Ctrl+V    | Paste the clipboard with its top-left at the cursor |
 * |  Left Click  | Place tile/NPC/zone                                 |
 * | Right Click  | Toggle collision/navigation                         |
 * |    Arrows    | Pan camera, or the tile picker while it is open     |
 * | Shift+Arrows | Same, at 2.5x speed                                 |
 * |    Scroll    | Pan tile picker                                     |
 * | Ctrl+Scroll  | Zoom                                                |
 *
 * Undo/redo covers every EditorCommand, including the composite command a
 * drag-paint stroke commits on mouse-up. The clipboard and the region
 * selection are editor session state and are not saved with the map.
 *
 * @par Debug and visual controls
 * Engine controls are exposed as developer-console commands (open with **F12**).
 * The remaining keybindings are gameplay-only.
 *
 * |        Key         |                  Action                     |
 * |--------------------|---------------------------------------------|
 * |       Space        | Toggle free camera (gameplay only)          |
 * |         X          | Toggle corner cut blocking (debug mode only)|
 *
 * The authoritative command list is the runtime registry assembled in
 * Console::RegisterDefaultCommands(). Use `help` in the developer console for
 * the complete current catalog and aliases. Frequently used entry points:
 *
 * | Canonical                             | Aliases           | Action                           |
 * |---------------------------------------|-------------------|----------------------------------|
 * | help                                  | ---               | List every registered command    |
 * | editor [on\|off\|toggle]              | ed                | Toggle level editor mode         |
 * | renderer.set &lt;opengl\|vulkan&gt;   | rndr.set, gfx     | Switch renderer at runtime       |
 * | debug.info [on\|off\|toggle]          | dbg.info, fps     | Toggle FPS/coords HUD            |
 * | debug.overlays [on\|off\|toggle]      | dbg.overlays, dbg | Toggle collision/nav overlays    |
 * | time.set &lt;hours&gt;                | ts                | Set in-game time (0.0-24.0)      |
 * | weather.next                          | ---               | Cycle weather state              |
 * | player.pos                            | pos, ppos         | Print player tile/world/facing   |
 * | npc.list                              | npcs              | List NPCs                        |
 * | map.save [path]                       | ---               | Save map JSON                    |
 * | renderer.trace [on\|off\|dump\|clear] | draws.trace       | Capture/dump draw-call trace     |
 *
 * `help` takes no arguments - it always prints the whole catalog, and Cmd_Help
 * ignores whatever follows the verb. Narrow the listing by tab-completing a
 * partial verb instead.
 *
 * @par Key debouncing
 * A toggle key must fire once per press rather than once per frame. @ref KeyToggle holds the
 * pressed flag and edge detection for that, so handlers test it instead of calling glfwGetKey
 * directly.
 *
 * @see Game::ProcessInput, PlayerSystem::Move, KeyToggle
 */

/**
 * @addtogroup Dialogue Dialogue System
 * @brief Interactive dialogue and conversation management for NPC interactions.
 *
 * The Dialogue module provides a flexible conversation system for player-NPC
 * interactions with branching dialogue trees and conditional responses.
 *
 * @par Architecture
 * - **DialogueTypes**: passive aggregates only - DialogueTree, DialogueNode,
 *   DialogueOption, DialogueCondition, DialogueConsequence. No runtime state.
 * - **DialogueStore**: owns every DialogueTree; the single source of tree data.
 * - **DialogueHandle**: trivially-copyable store key held by the ECS Dialogue
 *   component, so an NPC references a tree without owning or copying it.
 * - **DialogueManager**: the only conversation *state* - it copies the active
 *   tree into m_ActiveTree, tracks m_CurrentNode, filters the visible options,
 *   and drives the typewriter/selection UI.
 *
 * @par Dialogue flow
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
 * @par The node model
 * There is no node "type" tag. Every DialogueNode is the same shape - an `id`,
 * a `speaker`, a `text` body, and a list of `options` - and its role follows
 * from what that list contains:
 * - No options, or every option with an empty `nextNodeId`: terminal.
 *   DialogueNode::IsTerminal() derives this; nothing stores it.
 * - One or more options with a `nextNodeId`: a branch, and the player picks.
 * - Branching on game state is a property of the *option*, not the node:
 *   DialogueManager hides any option whose DialogueConditions fail, so the same
 *   node presents different choices as flags change. DialogueConsequences on
 *   the chosen option then write back through GameStateManager.
 *
 * @par Input handling
 * During active dialogue, normal game input is blocked:
 * - **Up/Down/W/S**: Navigate options
 * - **Enter/Space**: Confirm selection or advance
 * - **Escape**: Exit dialogue early
 *
 * @see DialogueManager, DialogueStore, DialogueHandle, DialogueNode, DialogueOption
 */

/**
 * @addtogroup Effects Visual Effects System
 * @brief Particle systems, atmospheric effects, and visual enhancements.
 *
 * The Effects module provides dynamic visual elements that enhance the game's
 * atmosphere without affecting gameplay mechanics.
 *
 * @par Particle system
 * The particle system renders collections of small sprites with physics-based
 * motion. There are two spawn sources: zones authored in the tilemap editor,
 * and the global weather stream (ParticleSystem::UpdateWeatherSpawning), which
 * emits across the visible view rather than inside a zone rect.
 *
 * @par Particle types
 * ParticleType is a large and still-growing enum, so this page deliberately
 * does not tabulate it - `EnumTraits&lt;ParticleType&gt;::Names` in
 * ParticleSystem.hpp is the only list that cannot go stale. The values fall
 * into loose families:
 * - **Ambient**: fireflies, drifting leaves, dust motes, pollen, sparkles.
 * - **Weather**: rain, snow, fog, cherry blossom, ash, ember, sand, wind.
 * - **Magic**: wisps, arcane/enchant/rune glyphs, hex, curse, void, vortex,
 *   souls, pixie dust, sparks, zaps.
 * - **Creatures and props**: fairies, butterflies, bats, bubbles, coins, gems,
 *   confetti, ink, smoke, steam, lanterns.
 * - **Emote**: hearts and Zzz, spawned above a character.
 * - **Celestial**: aurora motes, constellations, planets, moons, sunshine rays.
 * - **One-shot splashes**: RainSplash / SnowSplash, short strip bursts emitted
 *   at an impact point rather than from a zone.
 *
 * Ordinal values are serialized into map JSON, so the enum is append-only:
 * never reorder or remove an existing value.
 *
 * @par Particle lifecycle
 * @f[
 * \alpha(t) = \begin{cases}
 *   \frac{t}{t_{fade}} & t < t_{fade} \\
 *   1.0 & t_{fade} \leq t \leq t_{life} - t_{fade} \\
 *   \frac{t_{life} - t}{t_{fade}} & t > t_{life} - t_{fade}
 * \end{cases}
 * @f]
 *
 * @par Sky effects
 * The SkyRenderer provides time-of-day atmospheric effects:
 * - **Stars**: Twinkling night sky with color variation
 * - **Shooting Stars**: Random meteor streaks
 * - **Sun/Moon Rays**: God ray effects from light sources
 * - **Dawn Gradient**: Color transitions during sunrise
 * - **Dew Sparkles**: Morning ground-level glints
 *
 * @par Performance
 * Every particle sprite is packed into one 512px-wide atlas at load
 * (ParticleSystem::BuildAtlas), so the whole pass needs a single texture bind.
 * There is no instanced draw path: each particle is an ordinary batched quad.
 * The per-frame batches are instead partitioned by blend mode - non-additive
 * first, then additive - so the renderer flips blend state once rather than
 * per particle, and the noProjection batch is drawn separately with
 * perspective suspended. Each particle zone has configurable density and
 * spawn rate limits.
 *
 * @see ParticleSystem, SkyRenderer, ParticleZone
 */

/**
 * @addtogroup Editor Editor & Tools
 * @brief In-game level editor: tile placement, undo/redo, NPC and particle-zone
 *        editing, navigation/elevation strokes, and the EditorCommand pattern.
 *
 * The Editor module is decoupled from Game via the EditorContext struct, which
 * is built fresh each frame by Game::MakeEditorContext(). The editor never
 * stores the context across frames - its reference members would dangle.
 *
 * @par Architecture
 * - **Editor**: top-level mode driver, mouse/keyboard handlers, rendering of
 *   overlays and the tile picker.
 * - **EditorContext**: per-frame bundle of references to Game state the editor
 *   may mutate (tilemap, player, NPCs, time, project manifest).
 * - **EditorCommand**: abstract base for undo/redo, declared in
 *   EditorCommand.hpp. Concrete commands live in EditorCommands.hpp /
 *   EditorCommands.cpp; CompositeCmd batches multiple commands as one undo
 *   step (used by stroke accumulators).
 * - **UndoRedoStack**: bounded stack of EditorCommand instances.
 * - **Stroke accumulators**: TilePlaceStrokeAccum, CollisionStrokeAccum,
 *   NavigationStrokeAccum, ElevationStrokeAccum - coalesce drag-paint input
 *   into a single composite command at mouse release.
 *
 * @see Editor, EditorCommand, EditorCommands, UndoRedoStack
 */
