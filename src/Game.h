#pragma once

#include "CameraController.h"
#include "DialogueManager.h"
#include "Editor.h"
#include "GameStateManager.h"
#include "IRenderer.h"
#include "KeyToggle.h"
#include "NonPlayerCharacter.h"
#include "ParticleSystem.h"
#include "PlayerCharacter.h"
#include "RendererAPI.h"
#include "RendererFactory.h"
#include "SkyRenderer.h"
#include "Tilemap.h"
#include "TimeManager.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

/**
 * @struct FPSCounter
 * @brief Frame rate measurement and display state.
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
 *
 * Player + NPC slide into final talk positions before dialogue begins.
 */
struct DialogueSnapState
{
    bool active = false;                          ///< Whether snap animation is in progress
    float timer = 0.0f;                           ///< Elapsed time during snap animation
    float duration = 0.4f;                        ///< Total snap animation duration in seconds
    glm::vec2 playerStart{0.0f};                  ///< Player position at snap start
    glm::vec2 playerTarget{0.0f};                 ///< Player target position (facing NPC)
    glm::vec2 npcStart{0.0f};                     ///< NPC position at snap start
    glm::vec2 npcTarget{0.0f};                    ///< NPC target position (facing player)
    bool hasPlayerTile = true;                    ///< Whether player has a valid target tile
    int playerTileX = 0;                          ///< Player target tile column
    int playerTileY = 0;                          ///< Player target tile row
    int npcTileX = 0;                             ///< NPC target tile column
    int npcTileY = 0;                             ///< NPC target tile row
    Direction playerFacing = Direction::DOWN;     ///< Player facing after snap
    NPCDirection npcFacing = NPCDirection::DOWN;  ///< NPC facing after snap
    bool prefersTree = false;                     ///< Use branching tree dialogue after snap
    std::string fallbackText;                     ///< Simple text if no tree available
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
 *     float deltaTime = currentTime - lastTime;
 *     ProcessInput(deltaTime);
 *     Update(deltaTime);
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
 * Toggle editor mode with **E**. Dialogue activates on NPC interaction.
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
 * The game renders in this order for correct depth:
 * 1. Background layers (Ground, Ground Detail, Objects, Objects2, Objects3) -
 *    skips Y-sorted/no-projection tiles
 * 2. Background no-projection tiles (buildings rendered upright, perspective suspended)
 * 3. Y-sorted pass: Y-sorted tiles from ALL layers + NPCs + Player (sorted by Y)
 * 4. Foreground no-projection tiles (rendered upright)
 * 5. No-projection particles (perspective suspended)
 * 6. Foreground layers (Foreground, Foreground2, Overlay, Overlay2, Overlay3) -
 *    skips Y-sorted/no-projection tiles
 * 7. Regular particles
 * 8. Sky/ambient overlay (stars, rays, atmospheric effects)
 * 9. Editor UI (if active)
 * 10. Debug overlays (collision, navigation, layer indicators)
 *
 * @par Viewport Configuration
 * The game uses a tile-based virtual resolution:
 * - Visible tile counts are derived from current window size.
 * - Default startup target is 17x12 tiles (at 16px per tile = 272x192 virtual pixels).
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
 * Game --> Player[PlayerCharacter]:::entity
 * Game --> NPCs[vector of NPC]:::entity
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
 * @see PlayerCharacter, Tilemap, IRenderer
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

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    /**
     * @brief Initialize all game systems.
     *
     * Performs the following initialization sequence:
     * 1. Initialize GLFW and create window
     * 2. Create renderer (OpenGL, can switch to Vulkan)
     * 3. Load tileset and create tilemap
     * 4. Load player character sprites
     * 5. Load map from JSON (or generate default)
     * 6. Set up camera position
     *
     * NPC patrol routes are initialized lazily during NPC update when needed.
     *
     * @par Error Handling
     * Returns false if any critical initialization fails.
     * Error messages are printed to stderr.
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
     * - Compute @p deltaTime since the previous frame
     * - @ref ProcessInput(float) "ProcessInput(deltaTime)"
     * - @ref Update(float) "Update(deltaTime)"
     * - @ref Render() "Render()"
     * - Poll GLFW events via @c glfwPollEvents()
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

private:
    /**
     * @brief Process keyboard and mouse input.
     *
     * Handles both gameplay and editor input based on current mode.
     * See @ref GameInput for complete input documentation.
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
     * @brief Set up the snap alignment animation before starting dialogue.
     * @param npcIndex Index into m_NPCs of the NPC to talk to.
     */
    void BeginDialogueSnap(size_t npcIndex);

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

    /// @name Window Management
    /// @{
    GLFWwindow* m_Window = nullptr;  ///< GLFW window handle
    int m_ScreenWidth = 1360;        ///< Window width in pixels
    int m_ScreenHeight = 960;        ///< Window height in pixels
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
    int m_TilesVisibleWidth = 17;   ///< Tiles visible horizontally (based on window width)
    int m_TilesVisibleHeight = 12;  ///< Tiles visible vertically (based on window height)
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
    Tilemap m_Tilemap;                       ///< The game world
    PlayerCharacter m_Player;                ///< Player-controlled character
    std::vector<NonPlayerCharacter> m_NPCs;  ///< All NPCs in the world
    ParticleSystem m_Particles;              ///< Ambient particle effects (fireflies, etc.)
    TimeManager m_TimeManager;               ///< Day/night cycle time management
    SkyRenderer m_SkyRenderer;               ///< Sky rendering (sun, moon, stars)
    std::unique_ptr<IRenderer> m_Renderer;   ///< Graphics renderer
    RendererAPI m_RendererAPI = RendererAPI::OpenGL;  ///< Active renderer type
    /** @} */

    CameraController m_Camera;   ///< Camera controller (position, zoom, perspective)
    bool m_IsRendering = false;  ///< Reentrancy guard for Render()

    /// @name Frame Timing
    /// @{
    float m_LastFrameTime = 0.0f;  ///< Timestamp of last frame (for delta calculation)
    /// @}

    FPSCounter m_Fps;  ///< Frame rate measurement

    /// @name Editor
    /// @{
    Editor m_Editor;  ///< Level editor (extracted from Game)
    /// @}

    /**
     * @name Collision Resolution
     * @brief For movement rollback.
     * @{
     */
    glm::vec2 m_PlayerPreviousPosition{0.0f};  ///< Position before movement (for rollback)
    std::vector<glm::vec2> m_NpcPositions;     ///< Pre-allocated for per-frame NPC collision checks
    /** @} */

    /// @name Render Sorting
    /// @brief Y-sorted render list reused each frame to avoid allocation.
    /// @{
    struct RenderItem
    {
        enum Type
        {
            PLAYER_TOP = 0,
            PLAYER_BOTTOM = 1,
            NPC_TOP = 2,
            NPC_BOTTOM = 3,
            TILE = 4
        } type;
        float sortY;
        Tilemap::YSortPlusTile tile;
        const NonPlayerCharacter* npc;
    };
    std::vector<RenderItem> m_RenderList;
    /// @}

    /**
     * @name Dialogue System
     * @brief NPC dialogue UI state.
     * @{
     */
    bool m_InDialogue = false;            ///< Dialogue mode active (simple dialogue)
    int m_DialogueNPCIndex = -1;          ///< Index into m_NPCs of NPC being talked to (-1 = none)
    std::string m_DialogueText;           ///< Current dialogue text (simple dialogue)
    DialogueManager m_DialogueManager;    ///< Branching dialogue tree manager
    GameStateManager m_GameState;         ///< Game flags and state for consequences
    int m_DialoguePage = 0;               ///< Current page of dialogue text (for pagination)
    int m_DialogueTotalPages = 1;         ///< Total pages (cached during rendering)
    float m_DialogueBoxFadeTimer = 0.0f;  ///< Fade-in timer for dialogue box (seconds)
    float m_DialogueCharReveal = -1.0f;   ///< Typewriter char count (<0 = fully revealed)

    DialogueSnapState m_DialogueSnap;  ///< Snap alignment animation state
    /** @} */

    /// @name Input Toggle State
    /// @brief Debounced key toggles for one-shot actions (moved from function-local statics).
    /// @{
    KeyToggle<GLFW_KEY_E> m_KeyE;
    KeyToggle<GLFW_KEY_Z> m_KeyZ;
    KeyToggle<GLFW_KEY_F1> m_KeyF1;
    KeyToggle<GLFW_KEY_F2> m_KeyF2;
    KeyToggle<GLFW_KEY_F3> m_KeyF3;
    KeyToggle<GLFW_KEY_F4> m_KeyF4;
    KeyToggle<GLFW_KEY_F5> m_KeyF5;
    KeyToggle<GLFW_KEY_F6> m_KeyF6;
    KeyToggle<GLFW_KEY_SPACE> m_KeySpaceFreeCamera;
    KeyToggle<GLFW_KEY_PAGE_UP> m_KeyPageUp;
    KeyToggle<GLFW_KEY_PAGE_DOWN> m_KeyPageDown;
    KeyToggle<GLFW_KEY_C> m_KeyC;
    KeyToggle<GLFW_KEY_B> m_KeyB;
    KeyToggle<GLFW_KEY_X> m_KeyX;
    KeyToggle<GLFW_KEY_F> m_KeyF;
    int m_TimeOfDayCycle = 0;  ///< Current time-of-day preset index for F4 cycling

    // Dialogue-mode input toggles
    KeyToggle<GLFW_KEY_UP, GLFW_KEY_W> m_KeyDialogueUp;
    KeyToggle<GLFW_KEY_DOWN, GLFW_KEY_S> m_KeyDialogueDown;
    KeyToggle<GLFW_KEY_ENTER> m_KeyDialogueEnterTree;
    KeyToggle<GLFW_KEY_SPACE> m_KeyDialogueSpaceTree;
    KeyToggle<GLFW_KEY_ESCAPE> m_KeyDialogueEscapeTree;
    KeyToggle<GLFW_KEY_ENTER> m_KeyDialogueEnter;
    KeyToggle<GLFW_KEY_SPACE> m_KeyDialogueSpace;
    KeyToggle<GLFW_KEY_ESCAPE> m_KeyDialogueEscape;
    /// @}
};
