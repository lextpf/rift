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
 * @ingroup Core
 */
struct FPSCounter
{
    float updateTimer = 0.0f;     ///< Accumulator for FPS update interval (seconds).
    float consoleTimer = 0.0f;    ///< Unused: the per-second console stats dump is disabled.
    int frameCount = 0;           ///< Frames since last FPS update.
    float currentFps = 0.0f;      ///< Calculated FPS for display.
    float targetFps = 0.0f;       ///< Target FPS limit; &lt;= 0 means unlimited.
    int drawCallAccumulator = 0;  ///< Accumulated draw calls since last update.
    int currentDrawCalls = 0;     ///< Average draw calls per frame for display.
};

/**
 * @struct DialogueSnapState
 * @brief Smooth pre-dialogue alignment animation state.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * Player + NPC slide into final talk positions before dialogue begins.
 */
struct DialogueSnapState
{
    bool active = false;                       ///< Whether snap animation is in progress.
    float timer = 0.0f;                        ///< Elapsed time during snap animation.
    float duration = 0.4f;                     ///< Total snap animation duration in seconds.
    glm::vec2 playerStart{0.0f};               ///< Player position at snap start.
    glm::vec2 playerTarget{0.0f};              ///< Player target position (facing NPC).
    glm::vec2 npcStart{0.0f};                  ///< NPC position at snap start.
    glm::vec2 npcTarget{0.0f};                 ///< NPC target position (facing player).
    bool hasPlayerTile = true;                 ///< Whether player has a valid target tile.
    int playerTileX = 0;                       ///< Player target tile column.
    int playerTileY = 0;                       ///< Player target tile row.
    int npcTileX = 0;                          ///< NPC target tile column.
    int npcTileY = 0;                          ///< NPC target tile row.
    Direction playerFacing = Direction::DOWN;  ///< Player facing after snap.
    Direction npcFacing = Direction::DOWN;     ///< NPC facing after snap.
    bool prefersTree = false;                  ///< Use branching tree dialogue after snap.
    std::string fallbackText;                  ///< Simple text if no tree available.
};

/**
 * @struct DialogueUiState
 * @brief Presentation state of the single active conversation.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * Groups the Game members describing the one active dialogue. The
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
    float charReveal = -1.0f;   ///< Typewriter char count; &lt; 0 means fully revealed.
    DialogueSnapState snap;     ///< Pre-dialogue snap-alignment animation state.
};

/**
 * @class Game
 * @brief Central game manager handling the main loop and all subsystems.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Game is the composition root. It owns the window, the renderer, the tilemap and every
 * subsystem by value, and drives their lifecycle from Initialize() through Run() to Shutdown().
 *
 * It also owns the ECS registry @c m_World. Shared services do not live on entities: Game
 * publishes non-owning pointers to them into `m_World.globals()` as a @ref WorldServices bundle,
 * which is how stateless systems reach a texture store or a dialogue tree.
 *
 * The implementation is split across partials by concern: `Game.cpp` for lifecycle and the frame
 * loop, `GameInput.cpp` for input routing, `GameMenus.cpp` for title and pause menus plus the
 * title and 3D render paths, and `GameDialogue.cpp` for dialogue presentation.
 *
 * @par Game loop
 * A variable-timestep loop. The delta is sampled before polling, and polling runs before input so
 * handlers see this frame's key state.
 * @code{.cpp}
 * while (!shouldClose)
 * {
 *     float deltaTime = currentTime - lastTime;
 *     glfwPollEvents();
 *     deltaTime = std::min(deltaTime, MAX_DELTA_TIME);
 *     ProcessInput(deltaTime);
 *     Update(deltaTime);
 *     Render();
 * }
 * @endcode
 *
 * @par Frame timing
 * Run() clamps delta time to MAX_DELTA_TIME, which is 0.1 s. Without the clamp, a debugger pause
 * or a window drag stall would deliver one enormous frame and push characters through walls.
 *
 * @par Game modes
 * @ref GameMode is the mutually exclusive top-level state. Editor and dialogue are
 * not modes: they never change @c m_GameMode, and both are only reachable inside
 * @c GameMode::Playing. They are also mutually exclusive with each other in
 * practice, because the F-to-talk branch refuses to run while the editor is active
 * (GameInput.cpp asserts this when the snap starts).
 *
 * | Mode    | Input source | Features                                                 |
 * |---------|--------------|----------------------------------------------------------|
 * | Title   | Title menu   | Cosmetic world; New Game / Continue / Settings(*) / Quit |
 * | Playing | WASD + mouse | Player control, NPC interaction, editor                  |
 * | Paused  | Pause menu   | Frozen world; Resume / Quit to Title                     |
 *
 * (*) Settings is a permanently disabled stub; Continue is greyed out with no save.
 *
 * Toggle editor mode from the developer console with
 * `editor [on|off|toggle]` (alias: `ed`).
 * Dialogue activates on NPC interaction.
 *
 * @htmlonly
 * <pre class="mermaid">
 * stateDiagram-v2
 *     [*] --> Title
 *     Title --> Playing: Continue / New Game
 *     Playing --> Paused: Esc (only when no dialogue is open)
 *     Paused --> Playing: Esc / Resume
 *     Paused --> Title: Quit to Title
 *     state Playing {
 *         [*] --> Roaming
 *         Roaming --> Editing: console command editor on
 *         Editing --> Roaming: console command editor off
 *         Roaming --> Snapping: F near an NPC (editor off)
 *         Snapping --> Talking: snap timer elapsed
 *         Talking --> Roaming: Enter / Space / Esc
 *     }
 * </pre>
 * @endhtmlonly
 *
 * @par Camera system
 * The camera follows the player with smooth interpolation:
 * @f[
 * camera_{new} = camera_{old} + (target - camera_{old}) \times \alpha
 * @f]
 *
 * Where @f$ \alpha @f$ comes from @c rift::ExpApproachAlpha for a fixed settle time,
 * which makes the approach frame-rate independent. The camera is then clamped to the
 * map bounds, so near a map edge the viewport stops and the player drifts off-center
 * rather than the camera revealing space outside the map.
 *
 * @par Render order
 * The playing render path draws world content into an offscreen scene target,
 * composites PostFX, then draws sharp UI directly to the swapchain. Title mode
 * returns early through RenderTitleFrame(), which renders a cosmetic title world
 * plus menu UI without the player/NPC gameplay passes.
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart TD
 *     Begin[BeginFrame] --> Scene[BeginScene offscreen]
 *     Scene --> World[Background and no-projection layers]
 *     World --> Sort[Y-sort tiles, NPCs, player by Y plus support height]
 *     Sort --> Foreground[Foreground layers and particles]
 *     Foreground --> Lighting[World lights, sky overlay]
 *     Lighting --> PostFX[EndSceneApplyPostFX]
 *     PostFX --> UI[Editor, debug, dialogue, menu, console UI]
 *     UI --> End[EndFrame]
 * </pre>
 * @endhtmlonly
 *
 * @par Viewport configuration
 * The game renders at a tile-based virtual resolution.
 * - Visible tile counts derive from the current window size.
 * - The startup target is 19x10 tiles, which is 304x160 virtual pixels at 16px per tile.
 * - The result scales to fit the window and keeps its aspect ratio.
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
 * @code{.cpp}
 * Game g;
 * g.Initialize();  // Create the window and load assets.
 * g.Run();         // Main loop; blocks until the window closes.
 * g.Shutdown();    // Release resources.
 * @endcode
 *
 * @see PlayerSystem, Tilemap, IRenderer, WorldServices, GameMode
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

    /// @brief Destructor calls Shutdown() if not already called.
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
     * 1. Install the process-global ECS violation handler (game only, never in tests),
     *    publish @ref WorldServices into `m_World.globals()`, and mint the player entity,
     *    so @c m_PlayerEntity is valid from the title screen on, before any world loads.
     * 2. Initialize GLFW and load/validate `rift.project.json` (or built-in defaults).
     * 3. Select the startup renderer from the manifest and create the window.
     * 4. Create and initialize the renderer, then configure the initial viewport.
     * 5. Load tilesets, NPC sprites, fonts, and player character sprite assets.
     * 6. Initialize the editor, particles, time, sky, and dialogue subsystems.
     * 7. Load the cosmetic title-screen world.
     *
     * The day/night cycle is left at TimeManager's own default length: step 6 calls
     * bare `TimeManager::Initialize()` and nothing calls `SetDayDuration`, so one
     * in-game day lasts the class default (24 real seconds), not a configured value.
     *
     * NPC patrol routes are initialized lazily during NPC update when needed.
     *
     * The player's save/default gameplay world is not loaded until the user chooses
     * Continue or New Game from the title menu.
     *
     * @par Error handling
     * Returns false if any critical initialization fails.
     * Errors are logged via Logger.
     *
     * @return `true` if initialization succeeded.
     */
    bool Initialize();

    /**
     * @brief Start and run the engine's main game loop.
     *
     * Blocking. It returns when the window is asked to close, or when an exception escapes
     * a frame stage - the exception is logged and the loop is abandoned, so a return does
     * not imply a clean exit.
     *
     * The loop uses a **variable timestep**: each iteration computes a frame-to-frame
     * delta time based on the current GLFW time and forwards it to the simulation and
     * rendering stages.
     *
     * @par Per-frame execution order
     * Each frame performs the following steps in order:
     * - Sample the frame start time and compute deltaTime since the previous frame
     * - Poll GLFW events via @c glfwPollEvents() (before input, so the polled key
     *   state is this frame's)
     * - Clamp deltaTime to MAX_DELTA_TIME (0.1s)
     * - @ref ProcessInput(float) "ProcessInput(deltaTime)"
     * - @ref Update(float) "Update(deltaTime)"
     * - @ref Render() "Render()"
     * - Optional FPS limiter: sleep then spin-yield to the frame deadline, skipped
     *   entirely when @c FPSCounter::targetFps is 0
     *
     * @htmlonly
     * <pre class="mermaid">
     * sequenceDiagram
     *     participant Loop as Game::Run
     *     participant GLFW as GLFW
     *     participant Input as ProcessInput
     *     participant Update as Update
     *     participant Render as Render
     *     Loop->>Loop: sample frame start, compute deltaTime
     *     Loop->>GLFW: glfwPollEvents()
     *     Loop->>Loop: clamp deltaTime to 0.1s
     *     Loop->>Input: ProcessInput(deltaTime)
     *     Loop->>Update: Update(deltaTime)
     *     Loop->>Render: Render()
     *     Loop->>Loop: FPS limiter (sleep + spin)
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
     * @param fps Target FPS; &lt;= 0 disables the limiter (the default).
     */
    void SetTargetFps(float fps) { m_Fps.targetFps = fps; }

    /**
     * @name World-space 3D path (console-facing)
     * @brief Accessors for the `world3d` and `cam.*` commands.
     *
     * Public rather than friend-only because the console's command handlers are free
     * functions reaching Game through @c CommandContext::game.
     * @{
     */
    bool IsWorld3DEnabled() const { return m_World3DEnabled; }
    void SetWorld3DEnabled(bool enabled) { m_World3DEnabled = enabled; }
    cameraRig::Preset GetCameraPreset() const { return m_CameraPreset; }
    float GetCameraYaw() const { return m_CameraYaw; }
    float GetCameraPitch() const { return m_CameraPitch; }

    // Defined inline rather than in GameMenus.cpp so the console's command
    // handlers link into rift_tests, which compiles ConsoleCommands.cpp but not
    // the Game translation units.

    /// @brief Select a preset and adopt its angles, so `cam.preset ds` snaps the
    /// live yaw/pitch rather than leaving a stale value from a previous preset.
    void SetCameraPreset(cameraRig::Preset preset)
    {
        m_CameraPreset = preset;

        cameraRig::RigParams probe;
        probe.yawRadians = m_CameraYaw;
        probe.pitchRadians = m_CameraPitch;
        cameraRig::ApplyPreset(probe, preset);
        m_CameraYaw = probe.yawRadians;
        m_CameraPitch = probe.pitchRadians;
    }

    /// @brief Set the orbit yaw; wrapped to (-pi, pi]. Implies the Free preset,
    /// because Classic and DS pin their angles and would discard the change.
    void SetCameraYaw(float radians)
    {
        m_CameraYaw = cameraRig::WrapYaw(radians);
        m_CameraPreset = cameraRig::Preset::Free;
    }

    /// @brief Set the orbit pitch; clamped above the horizon. Implies Free.
    void SetCameraPitch(float radians)
    {
        m_CameraPitch = cameraRig::ClampPitch(radians);
        m_CameraPreset = cameraRig::Preset::Free;
    }
    /// @}

    /**
     * @brief Switch to a different renderer API at runtime.
     *
     * Both the GLFW window and the renderer are destroyed and recreated, because the two
     * APIs need incompatible window hints. Any cached @c GLFWwindow* or @c IRenderer*
     * dangles afterwards; window position is the only preserved state. Textures are
     * re-uploaded and characters re-packed into the tile atlas before the call returns.
     *
     * @param api  The renderer API to switch to (OpenGL or Vulkan).
     * @return     `true` when the switch succeeded, or when @p api was already active (no-op).
     *             `false` when @p api is unavailable, and also after a successful rollback
     *             to the previous API. If the rollback fails too, this calls Shutdown() and
     *             the application cannot continue.
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
     * Game.hpp, including the test target (which does not link GameInput.cpp).
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
     * @brief GLFW scroll callback: console scrollback, editor scroll, camera zoom.
     *
     * Static callback registered with GLFW. The wheel is claimed by the first
     * branch that applies, in this order:
     * 1. **Console open** - exclusive. The suggestion dropdown gets first refusal
     *    (wheel over it scrolls suggestions); otherwise the wheel scrolls the
     *    console scrollback. Returns without reaching the editor or the camera.
     * 2. **Editor active** - forwarded to @c Editor::HandleScroll, which owns every
     *    editor-specific wheel behavior (tile-picker navigation/zoom, elevation
     *    paint value, ...). If the tile picker is open it swallows the event and
     *    this returns.
     * 3. **Ctrl held** - camera zoom around the player's visual center. Gameplay
     *    clamps zoom to 0.4x-4.0x; the wider 0.1x floor applies only in editor
     *    free-camera mode (see @ref CameraController::HandleZoomScroll).
     *
     * A bare wheel with no console, no editor and no Ctrl does nothing.
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
     * m_PlayerEntity / m_GameState / m_TimeManager / m_Tilemap / m_World without
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
     * Mode-gated, with two early returns:
     * 1. Every mode: title ambient latch, FPS counters, deferred window snap.
     *    Returns here when Paused, which freezes everything.
     * 2. Playing only: player, time, weather director. Other modes instead fade the
     *    weather overlay alone, so the authored title hour holds.
     * 3. Every non-Paused mode: sky, particles, PostFX clock, animated tiles.
     *    Returns here unless Playing.
     * 4. Playing only, in this order: dialogue snap, typewriter reveal, stale-speaker
     *    cleanup, NPC AI, editor, camera follow, NPC-vs-player overlap stop.
     *
     * @param deltaTime Frame time in seconds.
     */
    void Update(float deltaTime);

    /**
     * @brief Render all game elements.
     *
     * A no-op while another Render() or Update() is on the stack, so a
     * WindowRefreshCallback that fires during a resize drag may produce no frame.
     *
     * Title mode and the `world3d` toggle each return early into a self-contained
     * pipeline (@ref RenderTitleFrame, @ref RenderFrame3D). The steps below are the flat
     * gameplay path only:
     * 1. Begin frame, then begin the offscreen scene target.
     * 2. Background layers, then one Y-sorted pass over tiles, NPCs and the player
     *    assembled into a single Drawable list.
     * 3. Foreground layers, particles, world lights, sky.
     * 4. EndSceneApplyPostFX composites the scene into the swapchain.
     * 5. UI straight to the swapchain, so it is neither graded nor bloomed: editor,
     *    dialogue, debug HUD, pause overlay, console.
     * 6. End frame.
     */
    void Render();

    /**
     * @brief Build an EditorContext from current Game state.
     *
     * @warning The returned context holds references into Game-owned state and
     * is only valid for the current frame. It must be passed straight to the
     * Editor and discarded; storing it across frames will leave dangling
     * references when Game state is rebuilt (e.g. on `renderer.set` or
     * map reload). See `Editor.hpp` for the full lifetime contract.
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
     * Lines are wrapped to `boxSize.x - 20`, centered horizontally across the box
     * width, and dropped once the next line would pass `boxPos.y + boxSize.y`.
     *
     * @param boxPos  Top-left corner of the text area, in screen pixels.
     * @param boxSize Dimensions of the text area.
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
     *
     * Only meaningful while a tree dialogue is being rendered: the page count is written
     * by RenderDialogueTreeBox(), so this input-time query lags the layout by one frame
     * and keeps answering from the last conversation once dialogue closes.
     *
     * @return True when the current page is the last one according to the page count
     *         cached by the most recent RenderDialogueTreeBox() call.
     */
    bool IsDialogueOnLastPage();

    /// @brief Un-stop the NPC currently held in dialogue and clear @c m_DialogueUi.npcId.
    /// Leaves @c inDialogue, @c text, @c page and @c snap to the callers.
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

    /// @brief Close simple (non-tree) dialogue and release the NPC.
    void CloseSimpleDialogue();

    /**
     * @brief Handle a confirm press in tree dialogue.
     *
     * Three outcomes, in priority order. While the typewriter is still revealing, the
     * press completes the reveal and does nothing else - this is what a player hits on
     * every page. Otherwise it advances one page. On the last page it confirms the
     * selected option, restarts pagination at page 0, and releases the NPC if the tree
     * ended.
     */
    void ConfirmOrAdvanceTreeDialogue();

    /// @brief Force-close tree dialogue via Escape.
    void ForceCloseTreeDialogue();

    /**
     * @name Title screen / pause overlay
     * @brief Top-level menu state and dispatch (see @c GameMenus.cpp).
     * @{
     */

    /**
     * True when @c rift.save.json (or whatever the manifest configures)
     * exists as a regular file. Used to grey out "Continue" and to gate the
     * overwrite-confirmation prompt on "New Game".
     */
    [[nodiscard]] bool CheckSaveExists() const;

    /**
     * Load the game world: tilemap, NPCs, player position, camera target.
     *
     * Called from "Continue" and "New Game" in the title menu. Boot uses
     * @c LoadTitleScreenWorld instead so gameplay content is not loaded until the
     * player leaves the title screen.
     *
     * @param loadSave  True when the caller is continuing the configured map;
     *                  false for New Game. Both paths load the configured authored
     *                  map when it exists, falling back to generated terrain when
     *                  unavailable; New Game separately resets transient state.
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
     * Load the cosmetic title-screen world: a grass map carrying one whole-map zone
     * per atmospheric particle type, time frozen at 23:00 with the weather director
     * disabled and Aurora imposed, the per-zone particle cap raised and the pool
     * pre-warmed. NPCs are destroyed; the player entity survives, parked at tile
     * (0,0) and skipped by the render pass rather than removed. Used at boot and on
     * "Quit to Title", which keeps the title's scenic background separate from the
     * player's save.
     */
    void LoadTitleScreenWorld();

    /**
     * Reset world + per-session state back to a fresh start.
     * Wraps @c LoadGameWorld(false) plus @c TimeManager::Initialize and
     * @c GameStateManager::Clear. It does not touch the on-disk save.
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
     * Render an entire gameplay frame through the world-space 3D path.
     *
     * Called as an early return from @c Render(), the same way
     * @c RenderTitleFrame is, so the flat path below it stays completely
     * untouched while the two coexist. That is deliberate: the 3D path is opt-in
     * via the `world3d` console command, and the flat path remains the default
     * and the comparison baseline until the new look is signed off.
     *
     * @see BuildCameraRig, Tilemap::RenderWorld3D
     */
    void RenderFrame3D();

    /**
     * Derive this frame's camera parameters from the live camera state.
     *
     * Bridges the flat camera (a viewport top-left corner plus a zoom) to the
     * orbit rig (a ground focus point plus angles): the focus is the center of
     * what the 2D camera was showing, so switching paths does not jump the view.
     */
    cameraRig::RigParams BuildCameraRig() const;

    /**
     * Render the title screen content (logo + menu + version) into the
     * current frame after @c EndSceneApplyPostFX. Used by
     * @c RenderTitleFrame.
     */
    void RenderTitleContent();

    /**
     * Draw the "\@lextpf" watermark in the bottom-left and the
     * "rift <version>" label in the bottom-right (the same footer shown on the
     * title screen). Self-contained: it suspends perspective and switches to a
     * screen-space UI projection, so a caller that draws in world space
     * afterward must restore its own projection. Shared by the title screen
     * and the in-game HUD.
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
     * A tile is valid when it is in bounds, is not the NPC's own tile, and is not
     * flagged as blocking in the collision map. Resolution order:
     * 1. **Stay put** - if the player's current tile is already valid and cardinally
     *    adjacent to the NPC, it is returned unchanged (no snap movement at all).
     *    This is checked first, before any search.
     * 2. **Preferred cardinal**, then down, up, right, left from the NPC tile.
     * 3. **Current player tile** again, even when not adjacent.
     * 4. `(-1, -1)` when nothing is safe.
     *
     * A zero preferred direction (player and NPC resolved to the same tile) is
     * rewritten to `(0, +1)` - down, since world Y grows downward - so step 2 always
     * has a first candidate to try.
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
     * @name Window management
     * @{
     */
    GLFWwindow* m_Window = nullptr;  ///< GLFW window handle.
    int m_ScreenWidth = 1520;        ///< Window width in pixels (19 tiles * 80 px).
    int m_ScreenHeight = 800;        ///< Window height in pixels (10 tiles * 80 px).
    bool m_GlfwInitialized = false;  ///< Whether glfwInit() succeeded (for safe Shutdown).
    /// @}

    /**
     * @name Viewport settings
     * @brief Define the virtual game resolution based on window size.
     *
     * The number of visible tiles is derived from the window size by integer
     * division, and the window itself is snapped to whole tiles for clean
     * rendering. One tile occupies `TILE_PIXEL_SIZE * PIXEL_SCALE` = 80 screen
     * pixels, so the snap increment is 80 px, not 16.
     * @{
     */
    int m_TilesVisibleWidth = 19;   ///< Tiles visible horizontally (based on window width).
    int m_TilesVisibleHeight = 10;  ///< Tiles visible vertically (based on window height).
    static constexpr int TILE_PIXEL_SIZE = 16;  ///< Tile size in world pixels.
    static constexpr int PIXEL_SCALE = 5;       ///< World-pixel to screen-pixel scale (5x).
    float m_ResizeSnapTimer = 0.0f;             ///< Countdown (seconds) to the deferred snap.
    bool m_PendingWindowSnap = false;           ///< Whether a window snap is pending.
    /** @} */

    /**
     * @brief Handle window resize - updates viewport immediately, defers snap.
     * @param width  New framebuffer width
     * @param height New framebuffer height
     */
    void OnFramebufferResized(int width, int height);

    /**
     * @brief Snap the window to whole tiles (called after resize settles).
     *
     * Rounds the client area DOWN to a multiple of 80 screen pixels, with a floor
     * of 5x4 tiles (400x320). A no-op when the size is already aligned.
     */
    void SnapWindowToTileBoundaries();

    /// @brief GLFW framebuffer size callback.
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

    /// @brief GLFW window refresh callback - redraws during resize drag.
    static void WindowRefreshCallback(GLFWwindow* window);

    /**
     * @name Game entities
     * @brief Core game objects.
     * @{
     */
    TextureStore m_TextureStore;    ///< Owns sprite textures (player/NPC) keyed by handle.
    DialogueStore m_DialogueStore;  ///< Owns NPC dialogue trees keyed by handle.
    AssetRegistry m_Assets;         ///< Character/NPC sprite asset paths.
    /**
     * @brief The world's NPC-AI random source, owned here and published into
     * globals() via WorldServices::npcRng so every NpcAiSystem call draws from
     * one world-scoped stream that a test can seed.
     */
    std::mt19937 m_NpcRng{std::random_device{}()};
    /**
     * @brief The ECS world: every player/NPC entity plus the shared services
     * published into its globals() as a @ref WorldServices. Systems reach the
     * services through the registry rather than through per-entity back-pointers.
     */
    ecs::registry m_World;
    Tilemap m_Tilemap;                      ///< The game world.
    ecs::entity m_PlayerEntity{};           ///< Player entity in m_World (PlayerTag + components).
    ParticleSystem m_Particles;             ///< Ambient particle effects (fireflies, etc.).
    TimeManager m_TimeManager;              ///< Day/night cycle time management.
    WeatherDirector m_WeatherDirector;      ///< Weather transition choreography.
    SkyRenderer m_SkyRenderer;              ///< Sky rendering (sun, moon, stars).
    std::unique_ptr<IRenderer> m_Renderer;  ///< Graphics renderer.
    RendererAPI m_RendererAPI = RendererAPI::OpenGL;  ///< Active renderer type.
    std::vector<std::string> m_FontCandidates;     ///< Project-configured renderer font candidates.
    std::string m_SaveMapPath = "rift.save.json";  ///< Project-configured save/load JSON path.
    /** @} */

    CameraController m_Camera;   ///< Camera controller (position, zoom, perspective).
    bool m_IsRendering = false;  ///< Reentrancy guard for Render().
    /**
     * @brief True while Update() runs.
     *
     * Prevents WindowRefreshCallback from firing Render() on mid-Update state via
     * synchronous WM_SIZE from SnapWindowToTileBoundaries().
     */
    bool m_IsUpdating = false;

    /**
     * @name Frame timing
     * @{
     */
    float m_LastFrameTime = 0.0f;  ///< Timestamp of last frame (for delta calculation).

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

    FPSCounter m_Fps;  ///< Frame rate measurement.

    /**
     * @name Editor
     * @{
     */
    Editor m_Editor;  ///< Level editor; owns its own input/undo state.
    /// @}

    /**
     * @name Collision resolution
     * @brief Per-frame NPC feet/support scratch for same-surface collision.
     * @{
     */
    std::vector<CharacterCollisionBody>
        m_NpcBodies;  ///< Pre-allocated feet/support records for NPC collision checks.
    /** @} */

    /**
     * @name Render sorting
     * @brief Y-sorted render list reused each frame to avoid allocation.
     * @{
     */
    std::vector<Drawable> m_RenderList;
    /// @}

    /**
     * @name World-space 3D path
     * @{
     */
    /**
     * @brief Whether gameplay frames render through @c RenderFrame3D instead of the flat pipeline.
     *
     * Toggled by the `world3d` console command. Off by default while the two paths
     * coexist, so nothing changes until asked for.
     */
    bool m_World3DEnabled = false;
    /// Camera preset the 3D path builds its rig from (`cam.preset`).
    cameraRig::Preset m_CameraPreset = cameraRig::Preset::DS;
    /// Live orbit angles, so `cam.yaw` / `cam.pitch` survive between frames.
    float m_CameraYaw = 0.0f;
    float m_CameraPitch = cameraRig::DS_PITCH_RADIANS;
    /**
     * @brief True while the mouse button that orbits the camera is held.
     *
     * Tracked so a press only starts accumulating from the second frame - otherwise the
     * first frame's delta would be measured against a stale cursor position and snap the
     * camera on click.
     */
    bool m_CameraDragActive = false;
    glm::dvec2 m_CameraDragCursor{0.0};  ///< Cursor position at the previous drag frame.
    /// @}

    /**
     * @name Dialogue system
     * @brief NPC dialogue UI state.
     * @{
     */
    DialogueUiState m_DialogueUi;       ///< Active-conversation presentation state (grouped).
    DialogueManager m_DialogueManager;  ///< Branching dialogue tree manager.
    GameStateManager m_GameState;       ///< Game flags and state for consequences.
    /** @} */

    /**
     * @name Input toggle state
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
     * @name Top-level mode + menu state
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
     * session, the title's ambient particle zones and the initial weather
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
     * @name Developer console
     * @{
     */

    /**
     * @brief Drain console-mode key events while the console is open.
     *
     * Reads the polled GLFW key state via local KeyToggle<> instances and forwards
     * edge transitions to m_Console. Called as the early-return path in ProcessInput
     * when the console has focus.
     */
    void PumpConsoleKeys();

    Console m_Console{*this};  ///< In-game developer REPL toggled with F12.
    /**
     * Edge-detection state for mouse clicks while the console is open. The
     * console eats clicks that land on the suggestion dropdown; this tracks
     * the previous-frame button state so the action fires only on the press edge.
     */
    bool m_ConsoleMouseLeftPrev = false;
    /// @}
};
