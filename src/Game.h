#pragma once

#include "Tilemap.h"
#include "PlayerCharacter.h"
#include "NonPlayerCharacter.h"
#include "ParticleSystem.h"
#include "DialogueManager.h"
#include "GameStateManager.h"
#include "TimeManager.h"
#include "SkyRenderer.h"
#include "Editor.h"
#include "IRenderer.h"
#include "RendererAPI.h"
#include "RendererFactory.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

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
 * 1. Background layers (Ground, Ground Detail, Objects, Objects2, Objects3) - skips Y-sorted/no-projection tiles
 * 2. Background no-projection tiles (buildings rendered upright, perspective suspended)
 * 3. Y-sorted pass: Y-sorted tiles from ALL layers + NPCs + Player (sorted by Y coordinate)
 * 4. Foreground no-projection tiles (rendered upright)
 * 5. No-projection particles (perspective suspended)
 * 6. Foreground layers (Foreground, Foreground2, Overlay, Overlay2, Overlay3) - skips Y-sorted/no-projection tiles
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
    void SetTargetFps(float fps) { m_TargetFps = fps; }
    
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
    static void ScrollCallback(GLFWwindow *window, double xoffset, double yoffset);

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
     * @brief Compute a projection matrix with 3D globe effect.
     * 
     * Configures the renderer's perspective settings based on whether
     * the 3D globe effect is enabled.
     *
     * @param width  World width in pixels.
     * @param height World height in pixels.
     */
    void ConfigureRendererPerspective(float width, float height);

    /**
     * @brief Get a standard orthographic projection matrix.
     *
     * @param width  World width in pixels.
     * @param height World height in pixels.
     * @return Orthographic projection matrix.
     */
    glm::mat4 GetOrthoProjection(float width, float height);

    /**
     * @brief Toggle the 3D globe effect on/off.
     */
    void Toggle3DEffect();

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
    bool IsDialogueOnLastPage() const;


    /// @name Window Management
    /// @{
    GLFWwindow *m_Window;   ///< GLFW window handle
    int m_ScreenWidth;      ///< Window width in pixels
    int m_ScreenHeight;     ///< Window height in pixels
    /// @}

    /**
     * @name Viewport Settings
     * @brief Define the virtual game resolution based on window size.
     * 
     * The number of visible tiles is calculated from window size, with the
     * window size snapped to tile boundaries (16 pixel increments) for clean rendering.
     * @{
     */
    int m_TilesVisibleWidth;                    ///< Tiles visible horizontally (based on window width)
    int m_TilesVisibleHeight;                   ///< Tiles visible vertically (based on window height)
    static constexpr int TILE_PIXEL_SIZE = 16;  ///< Size of a tile in pixels
    static constexpr int PIXEL_SCALE = 5;       ///< Scale factor for rendering (5x)
    float m_ResizeSnapTimer;                    ///< Timer for deferred window snap after resize
    bool m_PendingWindowSnap;                   ///< Whether a window snap is pending
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
    RendererAPI m_RendererAPI;               ///< Active renderer type
    /** @} */

    /**
     * @name Camera State
     * @brief Variables controlling camera movement.
     *
     * The camera uses exponential smoothing to follow the player. The "follow target"
     * pattern enables smooth transitions: when m_HasCameraFollowTarget is true, the
     * camera interpolates from m_CameraPosition toward m_CameraFollowTarget each frame.
     * When false, the camera snaps instantly to the player position (used on load/teleport).
     * @{
     */
    glm::vec2 m_CameraPosition;      ///< Current camera world position (rendered position)
    glm::vec2 m_CameraFollowTarget;  ///< Target position camera is smoothing toward
    bool m_HasCameraFollowTarget;    ///< True = smooth follow mode, false = instant snap mode
    float m_CameraZoom;              ///< Zoom multiplier (1.0 = 100%)
    float m_CameraTilt;              ///< Tilt angle for 3D effect (0.0 = flat, 1.0 = max tilt)
    bool m_Enable3DEffect;           ///< Whether 3D tilt effect is active
    float m_GlobeSphereRadius;       ///< Radius for globe + vanishing point projection (larger = subtler)
    bool m_FreeCameraMode;           ///< Free camera mode (Space toggle) - camera doesn't follow player
    /** @} */

    float m_LastFrameTime;  ///< Timestamp of last frame (for delta calculation)

    /**
     * @name FPS Counter
     * @brief Frame rate measurement.
     * @{
     */
    float m_FpsUpdateTimer;     ///< Accumulator for FPS update interval
    float m_FpsConsoleTimer;    ///< Timer for console FPS output (every 2s)
    int m_FrameCount;           ///< Frames since last FPS update
    float m_CurrentFps;         ///< Calculated FPS for display
    float m_TargetFps;          ///< Target FPS limit (<=0 = unlimited)
    int m_DrawCallAccumulator;  ///< Accumulated draw calls since last update
    int m_CurrentDrawCalls;     ///< Average draw calls per frame for display
    /** @} */

    /// @name Editor
    /// @{
    Editor m_Editor;  ///< Level editor (extracted from Game)
    /// @}

    /**
     * @name Collision Resolution
     * @brief For movement rollback.
     * @{
     */
    glm::vec2 m_PlayerPreviousPosition;  ///< Position before movement (for rollback)
    /** @} */

    /**
     * @name Dialogue System
     * @brief NPC dialogue UI state.
     * @{
     */
    bool m_InDialogue;                     ///< Dialogue mode active (simple dialogue)
    NonPlayerCharacter *m_DialogueNPC;     ///< NPC being talked to
    std::string m_DialogueText;            ///< Current dialogue text (simple dialogue)
    DialogueManager m_DialogueManager;     ///< Branching dialogue tree manager
    GameStateManager m_GameState;          ///< Game flags and state for consequences
    int m_DialoguePage = 0;                ///< Current page of dialogue text (for pagination)
    mutable int m_DialogueTotalPages = 1;  ///< Total pages (cached during rendering)

    // Smooth pre-dialogue alignment state (player + NPC slide into final talk positions).
    bool m_DialogueSnapActive = false;
    float m_DialogueSnapTimer = 0.0f;
    float m_DialogueSnapDuration = 0.4f;
    glm::vec2 m_DialogueSnapPlayerStart{0.0f};
    glm::vec2 m_DialogueSnapPlayerTarget{0.0f};
    glm::vec2 m_DialogueSnapNPCStart{0.0f};
    glm::vec2 m_DialogueSnapNPCTarget{0.0f};
    bool m_DialogueSnapHasPlayerTile = true;
    int m_DialogueSnapPlayerTileX = 0;
    int m_DialogueSnapPlayerTileY = 0;
    int m_DialogueSnapNPCTileX = 0;
    int m_DialogueSnapNPCTileY = 0;
    Direction m_DialogueSnapPlayerFacing = Direction::DOWN;
    NPCDirection m_DialogueSnapNPCFacing = NPCDirection::DOWN;
    bool m_DialogueSnapPrefersTree = false;
    std::string m_DialogueSnapFallbackText;
    /** @} */
};
