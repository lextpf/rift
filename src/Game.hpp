#pragma once

#include "AssetRegistry.hpp"
#include "CameraController.hpp"
#include "CharacterDirection.hpp"
#include "Console.hpp"
#include "DialogueManager.hpp"
#include "DialogueStore.hpp"
#include "Editor.hpp"
#include "EntityStore.hpp"
#include "GameMode.hpp"
#include "GameStateManager.hpp"
#include "IRenderer.hpp"
#include "KeyToggle.hpp"
#include "MenuLogic.hpp"
#include "NpcIdle.hpp"
#include "ParticleSystem.hpp"
#include "RenderDrawable.hpp"
#include "RendererAPI.hpp"
#include "RendererFactory.hpp"
#include "SkyRenderer.hpp"
#include "TextureStore.hpp"
#include "Tilemap.hpp"
#include "TimeManager.hpp"
#include "ViewScaling.hpp"
#include "WeatherDirector.hpp"
#include "WorldServices.hpp"

#include <ecs.hpp>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <random>
#include <string>
#include <vector>

/**
 * @struct FPSCounter
 * @brief Frame rate measurement and display state.
 * @author Alex (https://github.com/lextpf)
 */
struct FPSCounter
{
    float updateTimer = 0.0f;     ///< Accumulator for FPS update interval
    float consoleTimer = 0.0f;    ///< Timer for console FPS output
    int frameCount = 0;           ///< Frames since last FPS update
    float currentFps = 0.0f;      ///< Calculated FPS for display
    float targetFps = 0.0f;       ///< Target FPS limit (<=0 = unlimited)
    int drawCallAccumulator = 0;  ///< Accumulated draw calls since last update
    int currentDrawCalls = 0;     ///< Average draw calls per frame for display
};

/**
 * @struct DialogueSnapState
 * @brief Smooth pre-dialogue alignment animation state.
 * @author Alex (https://github.com/lextpf)
 *
 * Player + NPC slide into final talk positions before dialogue begins.
 */
struct DialogueSnapState
{
    bool active = false;                       ///< Whether snap animation is in progress
    float timer = 0.0f;                        ///< Elapsed time during snap animation
    float duration = 0.4f;                     ///< Total snap animation duration in seconds
    glm::vec2 playerStart{0.0f};               ///< Player position at snap start
    glm::vec2 playerTarget{0.0f};              ///< Player target position (facing NPC)
    glm::vec2 npcStart{0.0f};                  ///< NPC position at snap start
    glm::vec2 npcTarget{0.0f};                 ///< NPC target position (facing player)
    bool hasPlayerTile = true;                 ///< Whether player has a valid target tile
    int playerTileX = 0;                       ///< Player target tile column
    int playerTileY = 0;                       ///< Player target tile row
    int npcTileX = 0;                          ///< NPC target tile column
    int npcTileY = 0;                          ///< NPC target tile row
    Direction playerFacing = Direction::DOWN;  ///< Player facing after snap
    Direction npcFacing = Direction::DOWN;     ///< NPC facing after snap
    bool prefersTree = false;                  ///< Use branching tree dialogue after snap
    std::string fallbackText;                  ///< Simple text if no tree available
};

/**
 * @struct DialogueUiState
 * @brief Presentation state of the single active conversation.
 * @author Alex (https://github.com/lextpf)
 *
 * Groups the formerly-loose Game members describing the one active dialogue. The
 * branching-tree runtime lives in @ref DialogueManager and the per-NPC tree data
 * in the @ref Dialogue component; this is just the simple-text + pagination +
 * fade/typewriter + snap presentation. The speaker is referenced by @ref npcId
 * (a stable instanceId that survives despawn, unlike an entity handle).
 */
struct DialogueUiState
{
    bool inDialogue = false;    ///< Simple-dialogue mode active.
    std::uint64_t npcId = 0;    ///< Instance id of the NPC being talked to (0 = none).
    std::string text;           ///< Current simple-dialogue text.
    int page = 0;               ///< Current page (pagination).
    int totalPages = 1;         ///< Total pages (cached during render).
    float boxFadeTimer = 0.0f;  ///< Dialogue-box fade-in timer (seconds).
    float charReveal = -1.0f;   ///< Typewriter char count (<0 = fully revealed).
    DialogueSnapState snap;     ///< Pre-dialogue snap-alignment animation state.
};

/**
 * @class Game
 * @brief Central game manager handling the main loop and all subsystems.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * The Game class is the application's entry point and primary coordinator.
 * It owns all major game systems and manages their lifecycle.
 *
 * @par Game Loop
 * Uses a simple variable-timestep loop:
 * @code
 * while (!shouldClose) {
 *
 * float deltaTime = currentTime - lastTime;
 *     glfwPollEvents();
 * ProcessInput(deltaTime);
 *
 * Update(deltaTime);
 *     Render();
 * }
 * @endcode
 *
 * @par Frame Timing
 * Delta time is clamped to 0.1s (MAX_DELTA_TIME) to prevent physics
 * explosions after debugger pauses or window drag stalls. See Run().
 *
 * @par Game Modes
 * The game supports multiple modes:
 *
 * | Mode     | Input          | Features                          |
 * |----------|----------------|-----------------------------------|
 * | Gameplay | WASD movement  | Player control, NPC interaction   |
 * | Dialogue | W/S or Up/Down | Conversation with NPCs            |
 * | Editor   | Mouse + keys   | Tile placement, collision editing |
 *
 * Toggle editor mode from the developer console with
 * `editor [on|off|toggle]` (alias: `ed`).
 * Dialogue activates on NPC interaction.
 *
 * @par Camera System
 * The camera follows the player with smooth interpolation:
 * @f[
 * camera_{new} = camera_{old} + (target - camera_{old}) \times \alpha
 * @f]
 *
 * Where @f$ \alpha @f$ is calculated for a specific settle time.
 * The camera is also clamped to keep the player centered in the viewport.
 *
 * @par Render Order
 * The playing render path draws world content into an offscreen scene target,
 * composites PostFX,
 * then draws sharp UI directly to the swapchain. Title mode
 * returns early through
 * RenderTitleFrame(), which renders a cosmetic title world
 * plus menu UI without player/NPC
 * gameplay passes.
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart TD
 * Begin[BeginFrame]
 * --> Scene[BeginScene offscreen]
 *     Scene --> World[Background and no-projection layers]
 *
 * World --> Sort[Y-sorted tiles, NPCs, player]
 *     Sort --> Foreground[Foreground layers and
 * particles]
 *     Foreground --> Lighting[Cloud shadows, world lights, sky overlay]
 * Lighting
 * --> PostFX[EndSceneApplyPostFX]
 *     PostFX --> UI[Editor, debug, dialogue, menu, console UI]

 * *     UI --> End[EndFrame]
 * </pre>
 * @endhtmlonly
 *
 * @par Viewport Configuration
 * The game uses a tile-based virtual resolution:
 * - Visible tile counts are derived from current window size.
 * - Default startup target is 19x10 tiles (at 16px per tile = 304x160 virtual pixels).
 * - Scaled to fit window while maintaining aspect ratio
 *
 * @htmlonly
 * <pre class="mermaid">
 * graph LR
 * classDef core fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 * classDef render fill:#2e1f5e,stroke:#8b5cf6,color:#e2e8f0
 * classDef world fill:#134e3a,stroke:#10b981,color:#e2e8f0
 * classDef entity fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 * classDef input fill:#164e54,stroke:#06b6d4,color:#e2e8f0
 *
 * Game((Game)):::core
 * Game --> Tilemap:::world
 * Game --> World[ECS registry]:::entity
 * World --> Player[player entity]:::entity
 * World --> NPCs[NPC entities]:::entity
 * Game --> Renderer[IRenderer]:::render
 * Game --> Window[GLFW]:::input
 * Tilemap --> CollisionMap:::world
 * Tilemap --> NavigationMap:::world
 * </pre>
 * @endhtmlonly
 *
 * @par Lifecycle
 * @code
 * Game g;
 * g.Initialize();  // Create window, load assets
 * g.Run();         // Main loop (blocks until window closes)
 * g.Shutdown();    // Release resources
 * @endcode
 *
 * @see PlayerSystem, Tilemap, IRenderer
 */
class Game
{
public:
    /**
     * @brief Construct a new Game object.
     *
     * Does not initialize resources; call Initialize() separately.
     */
    Game();

    /**
     * @brief Destructor calls Shutdown() if not already called.
     */
    ~Game();

    /// Game owns the GLFW window and renderer resources; copying is unsupported.
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    /// Moving would invalidate GLFW callbacks and subsystem references.
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    /**
     * @brief Initialize all game systems.
     *
     * Performs the startup sequence:
     * 1. Initialize GLFW and load/validate
     * `rift.project.json` (or built-in defaults).
     * 2. Select the startup renderer from the
     * manifest and create the window.
     * 3. Create and initialize the renderer, then configure
     * the initial viewport.
     * 4. Load tilesets, NPC sprites, fonts, and player character
     * sprite assets.
     * 5. Initialize particles, time, sky, dialogue, and editor subsystems.

     * * 6. Set the game day duration to 1200 real seconds and load the cosmetic
     * title-screen
     * world.
     *
     * NPC patrol routes are initialized lazily during NPC update when needed.

     * * The player's save/default gameplay world is not loaded until the user
     * chooses
     * Continue or New Game from the title menu.
     *
     * @par Error Handling
     * Returns false if any critical initialization fails.
     * Errors are logged via Logger.
     *
     * @return `true` if initialization succeeded.
     */
    bool Initialize();

    /**
     * @brief Starts and maintains the engine's main game loop (variable timestep).
     *
     * @details
     * This function is **blocking** and returns only when the application is asked to exit
     *
     * The loop uses a **variable timestep**: each iteration computes a frame-to-frame
     * delta time based on the current GLFW time and forwards it to the simulation and rendering
     * stages.
     *
     * @par Per-frame execution order
     * Each frame performs the following steps in order:
 * -
     * Compute @p deltaTime since the previous frame
     * - Poll GLFW events via @c
     * glfwPollEvents()
     * - @ref ProcessInput(float) "ProcessInput(deltaTime)"
     * - @ref
     * Update(float) "Update(deltaTime)"
     * - @ref Render() "Render()"
     *
     * @htmlonly

     * * <pre class="mermaid">
     * sequenceDiagram
     *     participant Loop as Game::Run
 *
     * participant GLFW as GLFW
     *     participant Input as ProcessInput
     *     participant
     * Update as Update
     *     participant Render as Render
     *     Loop->>Loop: compute and
     * clamp deltaTime
     *     Loop->>GLFW: glfwPollEvents()
     *     Loop->>Input:
     * ProcessInput(deltaTime)
     *     Loop->>Update: Update(deltaTime)
     *     Loop->>Render:
     * Render()
     * </pre>
     * @endhtmlonly
     *
     * @see ProcessInput(float)
     * @see Update(float)
     * @see Render()
     */
    void Run();

    /**
     * @brief Shutdown and release all resources.
     *
     * Performs cleanup in reverse initialization order:
     * 1. Destroy renderer
     * 2. Destroy GLFW window
     * 3. Terminate GLFW
     *
     * Safe to call multiple times.
     */
    void Shutdown();

    /**
     * @brief Set the target FPS limit.
     * @param fps Target FPS (<=0 = unlimited, default).
     */
    void SetTargetFps(float fps) { m_Fps.targetFps = fps; }

    /**
     * @brief Switch to a different renderer API at runtime.
     * @param api The renderer API to switch to (OpenGL or Vulkan).
     * @return true if switch was successful, false otherwise.
     *
     * This destroys the current renderer and creates a new one.
     * Textures and other GPU resources will need to be re-uploaded.
     */
    bool SwitchRenderer(RendererAPI api);

    /**
     * @brief Get the currently active renderer API.
     * @return The active renderer API.
     */
    RendererAPI GetRendererAPI() const { return m_RendererAPI; }

    /**
     * @brief Force-close any active dialogue (simple or tree).
     *
     * Closes whichever dialogue path is currently active and clears the
     * snap-alignment state. Safe to call when no dialogue is active.
     * Used by the developer console's `dialogue.end` command.
     *
     * @note Defined inline so the symbol exists in every TU that includes
     * Game.h, including the test target (which does not link GameInput.cpp).
     * The body intentionally inlines the parts of ForceCloseTreeDialogue /
     * CloseSimpleDialogue / ReleaseDialogueNPC that it needs rather than
     * calling them, since those are defined in GameInput.cpp.
     */
    void EndAnyDialogue()
    {
        const bool treeActive = m_DialogueManager.IsActive();
        if (treeActive)
        {
            m_DialogueManager.EndDialogue();
            m_DialogueUi.page = 0;
        }
        if (m_DialogueUi.inDialogue)
        {
            m_DialogueUi.inDialogue = false;
            m_DialogueUi.text.clear();
        }
        if (const ecs::entity dialogueNpc = FindNPCById(m_DialogueUi.npcId))
        {
            m_World.get<NpcIdle>(dialogueNpc).isStopped = false;
        }
        m_DialogueUi.npcId = 0;
        m_DialogueUi.snap.active = false;
    }

    /**
     * @brief Path the project's manifest configured for save/load JSON.
     * @return Const reference to @c m_SaveMapPath.
     */
    const std::string& GetSaveMapPath() const { return m_SaveMapPath; }

    /**
     * @brief Snapshot of the engine's frame-rate / draw-call counters.
     * @return Const reference to the live FPSCounter struct.
     */
    const FPSCounter& GetFPSCounter() const { return m_Fps; }

    /**
     * @brief Raw GLFW window handle for code paths that must call GLFW
     * directly (e.g. clipboard get/set). Returns nullptr before the window
     * is created or after shutdown.
     */
    GLFWwindow* GetWindow() const { return m_Window; }

    /**
     * Visible world extent (world pixels) at the current window size; matches
     * the render projection. Single source of truth for camera/particle sizing.
     */
    glm::vec2 VisibleWorldSize() const
    {
        return viewScaling::VisibleWorldSize(m_ScreenWidth, m_ScreenHeight, PIXEL_SCALE);
    }
    /// As VisibleWorldSize() but divided by the current camera zoom.
    glm::vec2 VisibleWorldSizeZoomed() const
    {
        return viewScaling::VisibleWorldSizeZoomed(
            m_ScreenWidth, m_ScreenHeight, PIXEL_SCALE, m_Camera.GetState().zoom);
    }

    /**
     * @brief Whether a simple (non-tree) dialogue is currently active.
     *
     * Read by the developer console's `dialogue.active` command and used
     * by `npc.despawn` to refuse despawning the speaker mid-conversation.
     */
    bool IsInSimpleDialogue() const { return m_DialogueUi.inDialogue; }

    /// @brief Current simple-dialogue text (empty when no simple dialogue active).
    const std::string& GetSimpleDialogueText() const { return m_DialogueUi.text; }

    /**
     * @brief Instance id of the NPC currently in dialogue.
     * @return NPC instance id, or 0 if no NPC is currently the speaker.
     */
    std::uint64_t GetDialogueNPCId() const { return m_DialogueUi.npcId; }

    /**
     * @brief GLFW scroll callback for tile picker navigation.
     *
     * Static callback registered with GLFW to handle mouse wheel events.
     * Behavior depends on current mode:
     * - **Elevation edit mode** (no Ctrl): Adjusts elevation paint value (0-32)
     * - **Ctrl+scroll** (tile picker closed): Camera zoom (0.1x-4.0x)
     * - **Ctrl+scroll** (tile picker open): Tile picker zoom
     * - **Scroll** (tile picker open, no Ctrl): Tile picker navigation
     *
     * @param window GLFW window handle.
     * @param xoffset Horizontal scroll offset (unused).
     * @param yoffset Vertical scroll offset (positive = up/zoom in, negative = down/zoom out).
     */
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    /**
     * @brief GLFW character callback - feeds typed text into the developer console.
     *
     * Forwards the codepoint to m_Console.OnChar; the console itself decides
     * whether to consume it (only when open). Mirrors ScrollCallback's static
     * forwarding pattern.
     */
    static void CharCallback(GLFWwindow* window, unsigned int codepoint);

private:
    /**
     * Console is an authorised mutator of game state (it's the developer's
     * REPL). Granting friendship lets the default command bindings reach
     * m_Player / m_GameState / m_TimeManager / m_Tilemap / m_World without
     * adding accessors that exist solely for console use.
     */
    friend class Console;

    /**
     * @brief Process keyboard and mouse input.
     *
     * Handles both gameplay and editor input based on current mode.
     * See @ref Input for complete input documentation.
     *
     * @param deltaTime Frame time in seconds (for movement scaling).
     */
    void ProcessInput(float deltaTime);

    /// @brief Handle branching and simple dialogue key input.
    void ProcessDialogueInput();

    /// @brief Handle player movement, collision, and NPC interaction.
    void ProcessPlayerMovement(glm::vec2 moveDirection, float deltaTime);

    /**
     * @brief Update game state.
     *
     * Updates all dynamic elements:
     * 1. Player animation
     * 2. NPC AI and animation
     * 3. Camera following
     * 4. Dialogue state
     *
     * @param deltaTime Frame time in seconds.
     */
    void Update(float deltaTime);

    /**
     * @brief Render all game elements.
     *
     * Performs the full render pass:
     * 1. Begin frame (clear, set projection)
     * 2. Render tilemap layers (with depth ordering)
     * 3. Render entities (NPCs, player)
     * 4. Render editor UI (if active)
     * 5. Render debug overlays (if enabled)
     * 6. End frame
     */
    void Render();

    /**
     * @brief Build an EditorContext from current Game state.
     *
     * @warning The returned context holds references into Game-owned state and
     * is only valid for the current frame. It must be passed straight to the
     * Editor and discarded; storing it across frames will leave dangling
     * references when Game state is rebuilt (e.g. on `renderer.set` or
     * map reload). See `Editor.h` for the full lifetime contract.
     *
     * @return EditorContext with references to Game-owned state.
     */
    EditorContext MakeEditorContext();

    /**
     * @brief Render simple dialogue text above NPC's head.
     *
     * Fallback for NPCs without dialogue trees.
     */
    void RenderNPCHeadText();

    /**
     * @brief Render text inside the dialogue box.
     *
     * @param boxPos  Center position of dialogue box.
     * @param boxSize Dimensions of dialogue box.
     */
    void RenderDialogueText(glm::vec2 boxPos, glm::vec2 boxSize);

    /**
     * @brief Render branching dialogue tree UI.
     *
     * Shows dialogue text and response options for tree-based dialogue.
     */
    void RenderDialogueTreeBox();

    /**
     * @brief Check if dialogue is on the last page.
     * @return True if on last page or no dialogue active.
     */
    bool IsDialogueOnLastPage();

    /**
     * @brief Release the NPC currently held in dialogue and reset dialogue NPC index.
     */
    void ReleaseDialogueNPC();

    /// @brief Whether m_DialogueUi.npcId currently resolves to a live NPC.
    bool HasDialogueNPC() const;

    /**
     * @brief Resolve an NPC instance id to its entity handle.
     * @return The NPC entity in @ref m_World, or @c ecs::entity{} if no live NPC
     *         has that id (including id 0). Delegates to the registry seam
     *         (@ref EntityStore::FindById); the scan is O(NPC count).
     * @note Defined inline so header-only callers (e.g. EndAnyDialogue) need no
     *       out-of-line translation unit.
     */
    ecs::entity FindNPCById(std::uint64_t id) { return EntityStore::FindById(m_World, id); }
    ecs::entity FindNPCById(std::uint64_t id) const { return EntityStore::FindById(m_World, id); }

    /**
     * @brief Close simple (non-tree) dialogue and release the NPC.
     */
    void CloseSimpleDialogue();

    /**
     * @brief Advance tree dialogue page, or confirm selection on last page.
     */
    void ConfirmOrAdvanceTreeDialogue();

    /**
     * @brief Force-close tree dialogue via Escape.
     */
    void ForceCloseTreeDialogue();

    /**
     * @name Title Screen / Pause Overlay
     * @brief Top-level menu state and dispatch (see @c GameMenus.cpp).
     * @{
     */

    /**
     * True when @c rift.save.json (or whatever the manifest configures)
     * exists as a regular file. Used to grey out @em Continue and to gate
     * the overwrite-confirmation prompt on @em New @em Game.
     */
    [[nodiscard]] bool CheckSaveExists() const;

    /**
     * Load the game world: tilemap, NPCs, player position, camera target.
     * @param loadSave  True to load from @c m_SaveMapPath; false to
     *                  regenerate the default tilemap and place the player
     *                  at the default spawn.
     * Called from @em Continue / @em New @em Game in the title menu.
     * Boot uses @c LoadTitleScreenWorld instead so the user's save isn't
     * touched until they pick @em Continue.
     */
    void LoadGameWorld(bool loadSave);

    /**
     * Pack every loaded NPC sprite sheet and the active player's three
     * sheets into the tile atlas, then bind each character to its packed
     * region. Called from LoadGameWorld so character draws share the atlas
     * texture with tiles, collapsing the Y-sorted pass into one batch.
     */
    void PackCharactersIntoAtlas();

    /**
     * Build the title world at the given tile dimensions: resize the map, paint
     * grass on layer 0, rebuild whole-map particle zones, and refresh the
     * particle system. Title screen only.
     */
    void PaintTitleWorld(int tilesWide, int tilesTall);
    /**
     * Size the title world to cover the current viewport (never below the base
     * size) and re-center the camera. forceRepaint repaints even if the size is
     * unchanged (use on initial load; pass false on resize to skip no-op rebuilds).
     */
    void RefreshTitleWorldForViewport(bool forceRepaint);

    /**
     * Load the cosmetic title-screen world: a plain grass map populated
     * with firefly particle zones and frozen at night. No player, no NPCs.
     * Used at boot and on @em Quit @em to @em Title to keep the title's
     * scenic background separate from the player's actual save.
     */
    void LoadTitleScreenWorld();

    /**
     * Reset world + per-session state back to a fresh start.
     * Wraps @c LoadGameWorld(false) plus @c TimeManager::Initialize and
     * @c GameStateManager::Clear. Does @b not touch the on-disk save.
     */
    void ResetWorldToDefaults();

    /// Process input while @c m_GameMode == Title (menu nav, confirm prompt).
    void ProcessTitleInput();

    /// Process input while @c m_GameMode == Paused (Resume / Quit-to-Title).
    void ProcessPauseInput();

    /**
     * Refresh @c m_TitleMenu.enabled flags based on save existence and
     * snap selection to the first enabled item. Call on entering Title.
     */
    void RebuildTitleMenu();

    /**
     * Render an entire Title-mode frame: BeginFrame to EndFrame, with its
     * own scene clear + PostFX + UI. Called as an early-return from
     * @c Render() so Title runs a minimal pipeline.
     */
    void RenderTitleFrame();

    /**
     * Render the title screen content (logo + menu + version) into the
     * current frame after @c EndSceneApplyPostFX. Used by
     * @c RenderTitleFrame.
     */
    void RenderTitleContent();

    /**
     * Draw the "rift <version>" label in the bottom-right corner (the same
     * footer shown on the title screen). Self-contained: it suspends
     * perspective and switches to a screen-space UI projection, so a caller
     * that draws in world space afterward must restore its own projection.
     * Shared by the title screen and the in-game HUD.
     */
    void RenderVersionFooter();

    /**
     * Render the dim overlay + pause menu on top of the existing world
     * frame. Called from inside @c Render() before the console pass.
     */
    void RenderPauseOverlay();

    /**
     * Render the New-Game confirm-overwrite modal on top of the title
     * screen. Called from @c RenderTitleContent when @c m_ConfirmOverwriteShown.
     */
    void RenderConfirmOverwritePrompt();
    /// @}

    /**
     * @brief Find a valid tile for the player to stand on during dialogue snap.
     *
     * Searches cardinal directions from the NPC tile, preferring the direction
     * the player approached from. Falls back to current player tile or (-1,-1).
     *
     * @param npcTileX NPC tile column.
     * @param npcTileY NPC tile row.
     * @param playerTileX Current player tile column (rounded).
     * @param playerTileY Current player tile row (rounded).
     * @param preferredDx Preferred X direction from NPC to player (-1, 0, or 1).
     * @param preferredDy Preferred Y direction from NPC to player (-1, 0, or 1).
     * @return Valid tile coordinates, or (-1, -1) if no safe tile found.
     */
    glm::ivec2 FindDialogueSnapTile(int npcTileX,
                                    int npcTileY,
                                    int playerTileX,
                                    int playerTileY,
                                    int preferredDx,
                                    int preferredDy) const;

    /**
     * @name Window Management
     * @{
     */
    GLFWwindow* m_Window = nullptr;  ///< GLFW window handle
    int m_ScreenWidth = 1520;        ///< Window width in pixels (19 tiles * 80 px)
    int m_ScreenHeight = 800;        ///< Window height in pixels (10 tiles * 80 px)
    bool m_GlfwInitialized = false;  ///< Whether glfwInit() succeeded (for safe Shutdown)
    /// @}

    /**
     * @name Viewport Settings
     * @brief Define the virtual game resolution based on window size.
     *
     * The number of visible tiles is calculated from window size, with the
     * window size snapped to tile boundaries (16 pixel increments) for clean rendering.
     * @{
     */
    int m_TilesVisibleWidth = 19;   ///< Tiles visible horizontally (based on window width)
    int m_TilesVisibleHeight = 10;  ///< Tiles visible vertically (based on window height)
    static constexpr int TILE_PIXEL_SIZE = 16;  ///< Size of a tile in pixels
    static constexpr int PIXEL_SCALE = 5;       ///< Scale factor for rendering (5x)
    float m_ResizeSnapTimer = 0.0f;             ///< Timer for deferred window snap after resize
    bool m_PendingWindowSnap = false;           ///< Whether a window snap is pending
    /** @} */

    /**
     * @brief Handle window resize - updates viewport immediately, defers snap.
     * @param width  New framebuffer width
     * @param height New framebuffer height
     */
    void OnFramebufferResized(int width, int height);

    /**
     * @brief Snap window to tile boundaries (called after resize settles).
     */
    void SnapWindowToTileBoundaries();

    /**
     * @brief GLFW framebuffer size callback.
     */
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

    /**
     * @brief GLFW window refresh callback - redraws during resize drag.
     */
    static void WindowRefreshCallback(GLFWwindow* window);

    /**
     * @name Game Entities
     * @brief Core game objects.
     * @{
     */
    TextureStore m_TextureStore;    ///< Owns sprite textures (player/NPC) keyed by handle.
    DialogueStore m_DialogueStore;  ///< Owns NPC dialogue trees keyed by handle.
    AssetRegistry m_Assets;         ///< Character/NPC sprite asset paths (demoted statics).
    /**
     * @brief The world's NPC-AI random source, owned here and published into
     * globals() via WorldServices::npcRng so NpcAiSystem draws from one
     * world-scoped stream (replacing its former file-local static engine).
     */
    std::mt19937 m_NpcRng{std::random_device{}()};
    /**
     * @brief The ECS world. Shared services live in its globals() (WorldServices);
     * the entity migration (player/NPCs -> registry) is in progress.
     */
    ecs::registry m_World;
    Tilemap m_Tilemap;                      ///< The game world
    ecs::entity m_PlayerEntity{};           ///< Player entity in m_World (PlayerTag + components)
    ParticleSystem m_Particles;             ///< Ambient particle effects (fireflies, etc.)
    TimeManager m_TimeManager;              ///< Day/night cycle time management
    WeatherDirector m_WeatherDirector;      ///< Weather transition choreography
    SkyRenderer m_SkyRenderer;              ///< Sky rendering (sun, moon, stars)
    std::unique_ptr<IRenderer> m_Renderer;  ///< Graphics renderer
    RendererAPI m_RendererAPI = RendererAPI::OpenGL;  ///< Active renderer type
    std::vector<std::string> m_FontCandidates;     ///< Project-configured renderer font candidates.
    std::string m_SaveMapPath = "rift.save.json";  ///< Project-configured save/load JSON path.
    /** @} */

    CameraController m_Camera;   ///< Camera controller (position, zoom, perspective)
    bool m_IsRendering = false;  ///< Reentrancy guard for Render()
    bool m_IsUpdating = false;   ///< True while Update() runs; prevents
                                 ///< WindowRefreshCallback from firing Render()
                                 ///< on mid-Update state via synchronous WM_SIZE
                                 ///< from SnapWindowToTileBoundaries().

    /**
     * @name Frame Timing
     * @{
     */
    float m_LastFrameTime = 0.0f;  ///< Timestamp of last frame (for delta calculation)

    /**
     * Time accumulator threaded into PostFXParams. Drives the grain noise
     * seed and any subtle time-based motion in the post-process pass.
     * Wraps periodically inside Game::Update to avoid float precision drift.
     */
    float m_PostFXTime = 0.0f;

    /**
     * Master toggle for post-processing. When false, the PostFX call sites
     * in Game::Render() and the title-screen path skip building grading /
     * vignette / grain / bloom intensities, so the offscreen scene is
     * composited into the swapchain unmodified. Toggleable from the
     * developer console via the `postfx` command.
     */
    bool m_PostFXEnabled = true;
    /// @}

    FPSCounter m_Fps;  ///< Frame rate measurement

    /**
     * @name Editor
     * @{
     */
    Editor m_Editor;  ///< Level editor (extracted from Game)
    /// @}

    /**
     * @name Collision Resolution
     * @brief Per-frame NPC-collision scratch (the player plane is derived inline in
     *        ProcessPlayerMovement; NPCs in NpcAiSystem::UpdateAll).
     * @{
     */
    std::vector<glm::vec2> m_NpcPositions;  ///< Pre-allocated for per-frame NPC collision checks
    /** @} */

    /**
     * @name Render Sorting
     * @brief Y-sorted render list reused each frame to avoid allocation.
     * @{
     */
    std::vector<Drawable> m_RenderList;
    /// @}

    /**
     * @name Dialogue System
     * @brief NPC dialogue UI state.
     * @{
     */
    DialogueUiState m_DialogueUi;       ///< Active-conversation presentation state (grouped)
    DialogueManager m_DialogueManager;  ///< Branching dialogue tree manager
    GameStateManager m_GameState;       ///< Game flags and state for consequences
    /** @} */

    /**
     * @name Input Toggle State
     * @brief Debounced key toggles for one-shot actions (moved from function-local statics).
     * @{
     */
    KeyToggle<GLFW_KEY_Z> m_KeyZ;
    KeyToggle<GLFW_KEY_SPACE> m_KeySpaceFreeCamera;
    KeyToggle<GLFW_KEY_B> m_KeyB;
    /**
     * X drives the debug-only corner-cut toggle in IsDebugMode (gameplay
     * X for appearance copy lives in the developer console as
     * `appearance.copy` / `appearance.restore`).
     */
    KeyToggle<GLFW_KEY_X> m_KeyX;
    KeyToggle<GLFW_KEY_F> m_KeyF;
    /// Toggles the developer console. F12 is layout-independent.
    KeyToggle<GLFW_KEY_F12> m_KeyConsole;

    // Dialogue-mode input toggles
    KeyToggle<GLFW_KEY_UP, GLFW_KEY_W> m_KeyDialogueUp;
    KeyToggle<GLFW_KEY_DOWN, GLFW_KEY_S> m_KeyDialogueDown;
    KeyToggle<GLFW_KEY_ENTER> m_KeyDialogueEnterTree;
    KeyToggle<GLFW_KEY_SPACE> m_KeyDialogueSpaceTree;
    KeyToggle<GLFW_KEY_ESCAPE> m_KeyDialogueEscapeTree;
    KeyToggle<GLFW_KEY_ENTER> m_KeyDialogueEnter;
    KeyToggle<GLFW_KEY_SPACE> m_KeyDialogueSpace;
    KeyToggle<GLFW_KEY_ESCAPE> m_KeyDialogueEscape;

    // Title / Pause menu input toggles. Independent KeyToggle instances from
    // the dialogue ones (each tracks its own edge state); modes are mutually
    // exclusive at runtime, so the per-instance state never crosses over.
    KeyToggle<GLFW_KEY_UP, GLFW_KEY_W> m_KeyMenuUp;
    KeyToggle<GLFW_KEY_DOWN, GLFW_KEY_S> m_KeyMenuDown;
    KeyToggle<GLFW_KEY_LEFT, GLFW_KEY_A> m_KeyMenuLeft;
    KeyToggle<GLFW_KEY_RIGHT, GLFW_KEY_D> m_KeyMenuRight;
    KeyToggle<GLFW_KEY_ENTER, GLFW_KEY_SPACE> m_KeyMenuConfirm;
    KeyToggle<GLFW_KEY_ESCAPE> m_KeyEscape;
    /// @}

    /**
     * @name Top-Level Mode + Menu State
     * @{
     */
    GameMode m_GameMode = GameMode::Title;     ///< Top-level game state.
    MenuLogic::ItemList m_TitleMenu;           ///< Title menu (4 items).
    MenuLogic::ItemList m_PauseMenu;           ///< Pause menu (2 items).
    bool m_ConfirmOverwriteShown = false;      ///< New-Game-with-save modal visible.
    MenuLogic::ConfirmPrompt m_ConfirmPrompt;  ///< State of the modal.
    /**
     * Mouse cursor + click state for menu hit-testing. Hover only updates
     * the selected item when the cursor actually moved this frame, so
     * keyboard nav isn't fought by a stationary cursor. Left-click edge
     * (down this frame, up last frame) triggers confirm on the hovered item.
     */
    double m_MenuLastMouseX = -1.0;
    double m_MenuLastMouseY = -1.0;
    bool m_MenuMouseLeftPrev = false;
    /**
     * One-shot latch: the first time the console opens during a title
     * session, the title's ambient particle zones AND the initial weather
     * are cleared so the user can `weather <state>` against a clean canvas.
     * Stays cleared for the rest of the title session, even after the
     * console is closed. Reset to false when @ref LoadTitleScreenWorld
     * re-enters the title world (e.g., on boot or Quit-to-Title).
     */
    bool m_TitleAmbientCleared = false;
    int m_DefaultMapWidth = 64;   ///< Cached from manifest for ResetWorldToDefaults.
    int m_DefaultMapHeight = 48;  ///< Cached from manifest for ResetWorldToDefaults.
    /**
     * Player character types declared in the project manifest, in
     * declaration order. Cached during Initialize() so LoadGameWorld can
     * pick a default character without re-reading the manifest.
     */
    std::vector<CharacterType> m_ConfiguredCharacters;
    /// @}

    /**
     * @name Developer Console
     * @{
     * Drains console-mode key events while the console is open. Reads
     * the polled GLFW key state via local KeyToggle<> instances and
     * forwards edge transitions to m_Console. Called as the early-return
     * path in ProcessInput when the console has focus.
     */
    void PumpConsoleKeys();

    Console m_Console{*this};  ///< In-game developer REPL toggled with F12.
    /**
     * Edge-detection state for mouse clicks while the console is open. The
     * console eats clicks that land on the suggestion dropdown; this tracks
     * the previous-frame button state so we only fire on the press edge.
     */
    bool m_ConsoleMouseLeftPrev = false;
    /// @}
};
