#ifdef _WIN32
#define NOMINMAX
#endif

#include "Game.hpp"

#include "AmbienceConfig.hpp"
#include "DrawTracer.hpp"
#include "Logger.hpp"
#include "MathConstants.hpp"
#include "MathUtils.hpp"
#include "NonPlayerCharacter.hpp"
#include "OpenGLRenderer.hpp"
#include "PlayerCharacter.hpp"
#include "PostFXParams.hpp"
#include "ProjectManifest.hpp"
#include "RendererFactory.hpp"
#include "Version.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <optional>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#undef DrawText
#undef near
#undef far
#pragma comment(lib, "winmm.lib")
#endif

// SetDebugDrawSleep, ResetDebugDrawCallIndex, IsDebugDrawSleepEnabled declared in OpenGLRenderer.h

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Game";
constexpr float HORIZON_SCALE_BASE = 0.6f;
constexpr float HORIZON_SCALE_TILT_RANGE = 0.15f;
constexpr float DEBUG_TEXT_MARGIN = 12.0f;
constexpr float DEBUG_CHAR_WIDTH = 12.0f;
constexpr float SEAM_FIX_OVERLAP = 0.1f;

std::string ToLowerCopy(std::string value)
{
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

RendererAPI RendererApiFromManifestName(const std::string& name)
{
    return ToLowerCopy(name) == "vulkan" ? RendererAPI::Vulkan : RendererAPI::OpenGL;
}

const char* RendererApiName(RendererAPI api)
{
    return api == RendererAPI::Vulkan ? "Vulkan" : "OpenGL";
}

void PrintManifestDiagnostics(const ManifestValidationResult& result)
{
    for (const ManifestDiagnostic& diagnostic : result.diagnostics)
    {
        std::string fieldSuffix =
            diagnostic.fieldPath.empty() ? std::string() : " [" + diagnostic.fieldPath + "]";
        if (diagnostic.severity == ManifestDiagnosticSeverity::Error)
        {
            Logger::ErrorF(
                LOG_SUBSYSTEM, "project manifest{}: {}", fieldSuffix, diagnostic.message);
        }
        else
        {
            Logger::WarnF(LOG_SUBSYSTEM, "project manifest{}: {}", fieldSuffix, diagnostic.message);
        }
    }
}
}  // namespace

Game::Game() = default;

Game::~Game()
{
    Shutdown();
}

bool Game::Initialize()
{
    Logger::Info(LOG_SUBSYSTEM, "Initialize() step 1: Initializing GLFW...");

    // Initialize GLFW
    if (!glfwInit())
    {
        Logger::Error(LOG_SUBSYSTEM, "Failed to initialize GLFW");
        return false;
    }
    m_GlfwInitialized = true;

    Logger::Info(LOG_SUBSYSTEM, "Initialize() step 2: Loading project manifest...");

    ManifestValidationResult manifestResult;
    ProjectManifest manifest = ProjectManifest::LoadDefaultOrFallback(manifestResult);
    PrintManifestDiagnostics(manifestResult);
    if (manifestResult.HasErrors())
    {
        Logger::Error(LOG_SUBSYSTEM, "Project manifest validation failed; aborting startup.");
        return false;
    }

    Logger::InfoF(
        LOG_SUBSYSTEM,
        "Project manifest: {}",
        manifest.loadedFromFile ? manifest.sourcePath.string() : std::string("built-in defaults"));

    Logger::Info(LOG_SUBSYSTEM, "Initialize() step 3: Selecting Renderer API...");

    m_RendererAPI = RendererApiFromManifestName(manifest.startupRenderer);
    m_FontCandidates = manifest.ResolvePathStrings(manifest.fonts);
    m_SaveMapPath = manifest.defaultMap.empty() ? "rift.save.json"
                                                : manifest.ResolvePathString(manifest.defaultMap);

    Logger::InfoF(
        LOG_SUBSYSTEM, "Renderer API: {} (press F1 to switch)", RendererApiName(m_RendererAPI));
    Logger::Info(LOG_SUBSYSTEM, "Available renderers: OpenGL, Vulkan");

    Logger::Info(LOG_SUBSYSTEM, "Initialize() step 4: Setting window hints...");

    // Set window hints based on selected renderer API
    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }
    else if (m_RendererAPI == RendererAPI::Vulkan)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    Logger::Info(LOG_SUBSYSTEM, "Initialize() step 5: Creating GLFW window...");

    m_Window =
        glfwCreateWindow(m_ScreenWidth, m_ScreenHeight, "rift " RIFT_VERSION, nullptr, nullptr);
    if (!m_Window)
    {
        Logger::Error(LOG_SUBSYSTEM, "Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    Logger::Info(LOG_SUBSYSTEM, "Initialize() step 6: Setting window callbacks...");

    // Store Game instance pointer in window for callbacks
    glfwSetWindowUserPointer(m_Window, this);

    // Set callbacks
    glfwSetScrollCallback(m_Window, ScrollCallback);
    glfwSetCharCallback(m_Window, CharCallback);
    glfwSetFramebufferSizeCallback(m_Window, FramebufferSizeCallback);
    glfwSetWindowRefreshCallback(m_Window, WindowRefreshCallback);

    // Sleep 2 seconds after each draw call, set to true to enable
    SetDebugDrawSleep(m_Window, false);

    Logger::Info(LOG_SUBSYSTEM, "Initialize() step 7: Creating Renderer...");

    // Create renderer based on selected API
    m_Renderer = CreateRenderer(m_RendererAPI, m_Window);
    if (!m_Renderer)
    {
        Logger::Error(LOG_SUBSYSTEM, "Failed to create Renderer");
        glfwTerminate();
        return false;
    }
    m_Renderer->SetFontCandidates(m_FontCandidates);

    Logger::Info(LOG_SUBSYSTEM, "Initialize() step 8: Renderer created successfully");

    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        // Make OpenGL context current and initialize GLAD
        glfwMakeContextCurrent(m_Window);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            Logger::Error(LOG_SUBSYSTEM, "Failed to initialize GLAD");
            m_Renderer->Shutdown();
            m_Renderer.reset();
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
            glfwTerminate();
            return false;
        }
        Texture::AdvanceOpenGLContextGeneration();

        // OpenGL settings
        glViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glfwSwapInterval(0);  // VSync: 0 = disabled, 1 = enabled
    }

    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        Logger::InfoF(
            LOG_SUBSYSTEM, "OpenGL: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        Logger::InfoF(LOG_SUBSYSTEM,
                      "GLSL: {}",
                      reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));
    }
    else
    {
        // TODO: report Vulkan device/driver info via renderer instead of calling glGetString().
    }

    // Initialize renderer
    Logger::Info(LOG_SUBSYSTEM, "About to call Renderer->Init()...");
    if (!m_Renderer->Init())
    {
        Logger::Error(LOG_SUBSYSTEM,
                      "Renderer->Init() failed; aborting startup rather than shipping a black "
                      "frame.");
        m_Renderer->Shutdown();
        m_Renderer.reset();
        return false;
    }
    Logger::Info(LOG_SUBSYSTEM, "Renderer->Init() completed successfully");

    // Some drivers/middleware paths can reset swap interval during init.
    // Re-apply no-vsync after renderer initialization.
    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        glfwSwapInterval(0);
    }

    // Set viewport
    m_Renderer->SetViewport(0, 0, m_ScreenWidth, m_ScreenHeight);

    // World viewport size based on tiles visible
    float initWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float initWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    m_Camera.ConfigurePerspective(*m_Renderer, initWorldWidth, initWorldHeight);
    glm::mat4 projection = CameraController::GetOrthoProjection(initWorldWidth, initWorldHeight);
    m_Renderer->SetProjection(projection);

    std::vector<std::string> tilesetPaths = manifest.ResolvePathStrings(manifest.tilesets);
    bool loaded =
        m_Tilemap.LoadCombinedTilesets(tilesetPaths, manifest.tileWidth, manifest.tileHeight);
    if (!loaded)
    {
        Logger::Error(LOG_SUBSYSTEM,
                      "Failed to load combined tileset from project manifest. Tried:");
        for (const auto& path : tilesetPaths)
        {
            Logger::ErrorF(LOG_SUBSYSTEM, "    {}", path);
        }
        return false;
    }

    std::vector<std::string> npcSpritePaths = manifest.ResolvePathStrings(manifest.npcSprites);
    for (const std::string& npcSpritePath : npcSpritePaths)
    {
        NonPlayerCharacter::SetNpcAsset(NonPlayerCharacter::TypeFromSpritePath(npcSpritePath),
                                        npcSpritePath);
    }
    m_Editor.Initialize(npcSpritePaths);

    // Cache manifest fields needed by LoadGameWorld() so it can be invoked
    // again at runtime (Continue / New Game) without re-reading the file.
    m_DefaultMapWidth = manifest.defaultMapWidth;
    m_DefaultMapHeight = manifest.defaultMapHeight;

    // Register player character sprite assets (static, one-time) and cache the
    // configured character list so LoadGameWorld can pick a default.
    m_ConfiguredCharacters.clear();
    for (const auto& [characterName, character] : manifest.playerCharacters)
    {
        std::optional<CharacterType> characterType =
            EnumTraits<CharacterType>::FromString(characterName);
        if (!characterType.has_value())
        {
            continue;
        }

        m_ConfiguredCharacters.push_back(*characterType);
        for (const auto& [spriteType, path] : character.sprites)
        {
            PlayerCharacter::SetCharacterAsset(
                *characterType, spriteType, manifest.ResolvePathString(path));
        }
    }

    m_LastFrameTime = static_cast<float>(glfwGetTime());

    // Initialize particle system. Tile size and zones are set inside
    // LoadTitleScreenWorld below; here we just bring the system online.
    m_Particles.LoadTextures();
    m_Particles.SetTileSize(m_Tilemap.GetTileWidth(), m_Tilemap.GetTileHeight());
    m_Particles.SetMaxParticlesPerZone(50);

    // Initialize day & night cycle and sky. LoadTitleScreenWorld will
    // override the time-of-day to night before the first frame renders.
    m_TimeManager.Initialize();
    m_TimeManager.SetDayDuration(1200.0f);  // 20 real minutes = 1 in-game day
    m_SkyRenderer.Initialize();

    // Initialize dialogue system
    m_DialogueManager.Initialize(&m_GameState);

    // Show the title screen first; the player's save file is left untouched
    // until they pick "Continue" or "New Game" from the title menu. Done
    // last so the world setup overrides any earlier defaults (time of day,
    // particle zones, camera anchor).
    LoadTitleScreenWorld();

    float camWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float camWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    float mapWidth = static_cast<float>(m_Tilemap.GetMapWidth() * m_Tilemap.GetTileWidth());
    float mapHeight = static_cast<float>(m_Tilemap.GetMapHeight() * m_Tilemap.GetTileHeight());
    Logger::InfoF(LOG_SUBSYSTEM,
                  "Map size: {}x{} tiles = {}x{} pixels",
                  m_Tilemap.GetMapWidth(),
                  m_Tilemap.GetMapHeight(),
                  mapWidth,
                  mapHeight);
    Logger::InfoF(LOG_SUBSYSTEM,
                  "Camera view: {}x{} pixels ({} tiles wide, {} tiles tall)",
                  camWorldWidth,
                  camWorldHeight,
                  m_TilesVisibleWidth,
                  m_TilesVisibleHeight);
    Logger::InfoF(LOG_SUBSYSTEM,
                  "Camera position: ({}, {})",
                  m_Camera.GetState().position.x,
                  m_Camera.GetState().position.y);

    return true;
}

void Game::Run()
{
#ifdef _WIN32
    // RAII guard: raises Windows timer resolution from ~15.6ms to 1ms so that
    // sleep_for() in the FPS limiter sleeps accurately. Automatically restored
    // when Run() exits (normal return or exception).
    struct TimerPeriodGuard
    {
        TimerPeriodGuard() { timeBeginPeriod(1); }
        ~TimerPeriodGuard() { timeEndPeriod(1); }
        TimerPeriodGuard(const TimerPeriodGuard&) = delete;
        TimerPeriodGuard& operator=(const TimerPeriodGuard&) = delete;
    } timerGuard;
#endif

    // Main game loop. Processes input, updates game state, and renders each frame.
    // Delta time is computed from wall-clock time for frame-rate independent movement.
    try
    {
        while (!glfwWindowShouldClose(m_Window))
        {
            // Sample the frame start before polling so the FPS limiter's
            // deadline covers event processing. Otherwise the next frame's
            // glfwPollEvents() runs after the current deadline expires but
            // before the next frameStartTime is taken, pushing the actual
            // frame-to-frame interval above target by the poll cost and
            // jittering FPS with input-event volume.
            double frameStartTime = glfwGetTime();
            float deltaTime = static_cast<float>(frameStartTime) - m_LastFrameTime;
            m_LastFrameTime = static_cast<float>(frameStartTime);

            // Poll events before ProcessInput so input sees this frame's
            // key/mouse state (GLFW only updates cached state during poll).
            glfwPollEvents();

            // Clamp deltaTime to prevent physics explosions after debugger pauses or window drag
            // stalls
            static constexpr float MAX_DELTA_TIME = 0.1f;
            deltaTime = std::min(deltaTime, MAX_DELTA_TIME);

            try
            {
                ProcessInput(deltaTime);
                Update(deltaTime);
                Render();
            }
            catch (const std::exception& e)
            {
                Logger::ErrorF(LOG_SUBSYSTEM, "Exception in game loop: {}", e.what());
                break;  // Exit loop on error
            }
            catch (...)
            {
                Logger::Error(LOG_SUBSYSTEM, "Unknown exception in game loop");
                break;
            }

            // FPS limiter: busy-wait until target frame time is reached.
            // Busy-waiting is used instead of sleep() for sub-millisecond accuracy,
            // but this does consume CPU cycles. When m_Fps.targetFps is 0, no limiting.
            if (m_Fps.targetFps > 0.0f)
            {
                double targetFrameTime = 1.0 / static_cast<double>(m_Fps.targetFps);
                double elapsed = glfwGetTime() - frameStartTime;
                double remaining = targetFrameTime - elapsed;

                if (remaining > 0.0)
                {
                    using clock = std::chrono::steady_clock;
                    const auto sleepDuration = std::chrono::duration_cast<clock::duration>(
                        std::chrono::duration<double>(remaining));
                    const auto frameDeadline = clock::now() + sleepDuration;

                    // Sleep most of the remaining frame time, then spin-yield for accuracy.
                    // Windows default timer resolution is ~15.6ms, so sleep_for(1ms) can
                    // sleep 15ms+.  Keep spinThreshold above the OS granularity so that
                    // high-FPS targets (e.g. 500 fps = 2ms budget) never call sleep_for.
                    constexpr auto spinThreshold = std::chrono::milliseconds(2);
                    while (true)
                    {
                        const auto now = clock::now();
                        if (now >= frameDeadline)
                            break;

                        const auto timeLeft = frameDeadline - now;
                        if (timeLeft > spinThreshold)
                        {
                            std::this_thread::sleep_for(timeLeft - spinThreshold);
                        }
                        else
                        {
                            std::this_thread::yield();
                        }
                    }
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Exception in Run(): {}", e.what());
    }
    catch (...)
    {
        Logger::Error(LOG_SUBSYSTEM, "Unknown exception in Run()");
    }
}

void Game::Update(float deltaTime)
{
    // Guard so that if SnapWindowToTileBoundaries() (called from within Update)
    // triggers a synchronous WindowRefreshCallback -> Render(), that Render
    // sees mid-Update state and bails instead of rendering garbage.
    struct UpdateGuard
    {
        bool& flag;
        UpdateGuard(bool& f)
            : flag(f)
        {
            flag = true;
        }
        ~UpdateGuard() { flag = false; }
    } updateGuard(m_IsUpdating);

    // Update FPS counter
    m_Fps.frameCount++;
    m_Fps.updateTimer += deltaTime;
    if (m_Fps.updateTimer >= 1.0f)  // Update FPS display every second
    {
        m_Fps.currentFps = m_Fps.frameCount / m_Fps.updateTimer;
        m_Fps.currentDrawCalls =
            (m_Fps.frameCount > 0) ? m_Fps.drawCallAccumulator / m_Fps.frameCount : 0;
        m_Fps.frameCount = 0;
        m_Fps.updateTimer = 0.0f;
        m_Fps.drawCallAccumulator = 0;
    }

    // Output stats to console every second [deprecated]
    m_Fps.consoleTimer += deltaTime;
    if (m_Fps.consoleTimer >= 1.0f)
    {
        const char* renderer = (m_RendererAPI == RendererAPI::OpenGL) ? "OpenGL" : "Vulkan";
        float frameTimeMs = (m_Fps.currentFps > 0) ? (1000.0f / m_Fps.currentFps) : 0.0f;
        /*std::cout << "[" << renderer << "] "
                  << static_cast<int>(m_Fps.currentFps) << " FPS | "
                  << std::fixed << std::setprecision(4) << frameTimeMs << "ms | "
                  << m_ScreenWidth << "x" << m_ScreenHeight << " | "
                  << std::setprecision(2) << m_Camera.GetState().zoom << "x zoom"
                  << std::endl;*/
        m_Fps.consoleTimer = 0.0f;
    }

    // Handle deferred window snap after resize settles
    if (m_PendingWindowSnap)
    {
        m_ResizeSnapTimer -= deltaTime;
        if (m_ResizeSnapTimer <= 0.0f)
        {
            SnapWindowToTileBoundaries();
        }
    }

    // Pause freezes the world entirely. Title leaves cosmetic systems
    // (sky, particles, animated tiles) running so fireflies still drift
    // behind the menu, but locks the time-of-day, player, and NPCs.
    if (m_GameMode == GameMode::Paused)
    {
        return;
    }
    const bool isPlaying = (m_GameMode == GameMode::Playing);

    if (isPlaying)
    {
        m_Player.Update(deltaTime);

        // Update day & night cycle (frozen in Title so the night setting holds).
        m_TimeManager.Update(deltaTime);
    }
    m_SkyRenderer.Update(deltaTime, m_TimeManager);

    // Update particle system
    float pWorldW = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float pWorldH = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    glm::vec2 particleCullCam = m_Camera.GetState().position;
    glm::vec2 viewSize(pWorldW / m_Camera.GetState().zoom, pWorldH / m_Camera.GetState().zoom);
    if (m_Camera.GetState().enable3DEffect)
    {
        float horizonScale =
            HORIZON_SCALE_BASE + (1.0f - m_Camera.GetState().tilt) * HORIZON_SCALE_TILT_RANGE;
        float expansion = 1.0f / std::max(horizonScale, 0.001f);
        float cullWidthScale =
            static_cast<float>(perspectiveTransform::GetPerspectiveCullWidthScale(true));
        float cullHeightScale =
            static_cast<float>(perspectiveTransform::GetPerspectiveCullHeightScale(true));
        glm::vec2 expandedSize(viewSize.x * expansion * cullWidthScale,
                               viewSize.y * expansion * cullHeightScale);
        glm::vec2 padding = (expandedSize - viewSize) * 0.5f;
        particleCullCam -= padding;
        viewSize = expandedSize;
    }
    // Set night factor for lantern glows and rays
    m_Particles.SetNightFactor(m_TimeManager.GetStarVisibility());
    m_Particles.SetTimeOfDay(m_TimeManager.GetTimeOfDay());
    // Push the active weather into the particle system so it can drive global
    // weather spawning (rain/snow/ash/etc.) across the visible viewport.
    m_Particles.SetWeatherState(&GetWeatherDefinition(m_TimeManager.GetWeather()),
                                m_TimeManager.GetWeatherIntensity());
    m_Particles.Update(deltaTime, particleCullCam, viewSize);

    // Advance the post-process time accumulator (drives grain noise + any
    // subtle time-based motion in the post pass). Wrap periodically so that
    // long sessions don't lose float precision.
    m_PostFXTime += deltaTime;
    if (m_PostFXTime > 86400.0f)  // 24h wrap
    {
        m_PostFXTime -= 86400.0f;
    }

    // Update animated tiles
    m_Tilemap.UpdateAnimations(deltaTime);

    // Title: stop here. No player to elevate, no NPCs to step, no dialogue.
    if (!isPlaying)
    {
        return;
    }

    // Get player position, needed for NPC updates and collision
    glm::vec2 playerPos = m_Player.GetPosition();

    if (m_DialogueSnap.active)
    {
        if (m_DialogueNPCIndex < 0 || m_DialogueNPCIndex >= static_cast<int>(m_NPCs.size()))
        {
            m_DialogueSnap.active = false;
        }
        else
        {
            m_DialogueSnap.timer += deltaTime;
            float duration = std::max(0.05f, m_DialogueSnap.duration);
            float t = std::clamp(m_DialogueSnap.timer / duration, 0.0f, 1.0f);
            float smoothT = t * t * (3.0f - 2.0f * t);  // Smoothstep easing

            glm::vec2 blendedPlayer =
                m_DialogueSnap.playerStart +
                (m_DialogueSnap.playerTarget - m_DialogueSnap.playerStart) * smoothT;
            glm::vec2 blendedNPC = m_DialogueSnap.npcStart +
                                   (m_DialogueSnap.npcTarget - m_DialogueSnap.npcStart) * smoothT;

            m_Player.SetPositionRaw(blendedPlayer);
            GetDialogueNPC().SetPosition(blendedNPC);
            GetDialogueNPC().SetStopped(true);
            GetDialogueNPC().ResetAnimationToIdle();
            playerPos = blendedPlayer;

            if (t >= 1.0f)
            {
                if (m_DialogueSnap.hasPlayerTile)
                {
                    m_Player.SetTilePosition(m_DialogueSnap.playerTileX,
                                             m_DialogueSnap.playerTileY);
                }
                else
                {
                    m_Player.SetPositionRaw(m_DialogueSnap.playerTarget);
                }
                GetDialogueNPC().SetTilePosition(
                    m_DialogueSnap.npcTileX, m_DialogueSnap.npcTileY, 16, true);

                m_Player.Stop();
                m_Player.SetDirection(m_DialogueSnap.playerFacing);
                GetDialogueNPC().SetDirection(m_DialogueSnap.npcFacing);
                GetDialogueNPC().SetStopped(true);
                GetDialogueNPC().ResetAnimationToIdle();

                bool startedTree = false;
                if (m_DialogueSnap.prefersTree)
                {
                    startedTree = m_DialogueManager.StartDialogue(&GetDialogueNPC());
                    if (startedTree)
                    {
                        m_DialoguePage = 0;
                        m_DialogueBoxFadeTimer = 0.0f;
                        m_DialogueCharReveal = 0.0f;
                    }
                }
                if (!startedTree)
                {
                    m_InDialogue = true;
                    m_DialogueText = m_DialogueSnap.fallbackText;
                }

                m_DialogueSnap.active = false;
                playerPos = m_Player.GetPosition();
            }
        }
    }

    if (m_DialogueManager.IsActive())
    {
        m_DialogueBoxFadeTimer += deltaTime;
        if (m_DialogueCharReveal >= 0.0f)
            m_DialogueCharReveal += 35.0f * deltaTime;
    }

    // Update player logical plane (z-axis) using axis-aware engagement.
    // Movement direction comes from this frame's delta against m_PlayerPreviousPosition,
    // which ProcessPlayerMovement captures right before m_Player.Move().
    {
        glm::vec2 movement = playerPos - m_PlayerPreviousPosition;
        int dx = movement.x > 0.01f ? 1 : (movement.x < -0.01f ? -1 : 0);
        int dy = movement.y > 0.01f ? 1 : (movement.y < -0.01f ? -1 : 0);

        int playerTileX = 0;
        int playerTileY = 0;
        m_Tilemap.WorldToTileCoord(playerPos.x, playerPos.y, playerTileX, playerTileY);
        int destElev = m_Tilemap.GetElevation(playerTileX, playerTileY);
        ElevationAxis destAxis = m_Tilemap.GetElevationAxisAt(playerTileX, playerTileY);
        m_Player.UpdatePlane(destElev, destAxis, dx, dy);
    }

    // Update NPCs
    // During dialogue, freeze the NPC being talked to
    bool inAnyDialogue = m_InDialogue || m_DialogueManager.IsActive() || m_DialogueSnap.active;
    for (auto& npc : m_NPCs)
    {
        // Skip updating the NPC in dialogue
        if (inAnyDialogue && m_DialogueNPCIndex >= 0 &&
            m_DialogueNPCIndex < static_cast<int>(m_NPCs.size()) && &GetDialogueNPC() == &npc)
        {
            continue;
        }
        glm::vec2 npcPosBefore = npc.GetPosition();
        npc.Update(deltaTime, &m_Tilemap, &playerPos);

        // Derive NPC plane the same way as the player.
        glm::vec2 npcPosAfter = npc.GetPosition();
        glm::vec2 npcMovement = npcPosAfter - npcPosBefore;
        int ndx = npcMovement.x > 0.01f ? 1 : (npcMovement.x < -0.01f ? -1 : 0);
        int ndy = npcMovement.y > 0.01f ? 1 : (npcMovement.y < -0.01f ? -1 : 0);

        int npcTileX = 0;
        int npcTileY = 0;
        m_Tilemap.WorldToTileCoord(npcPosAfter.x, npcPosAfter.y, npcTileX, npcTileY);
        int npcDestElev = m_Tilemap.GetElevation(npcTileX, npcTileY);
        ElevationAxis npcDestAxis = m_Tilemap.GetElevationAxisAt(npcTileX, npcTileY);
        npc.UpdatePlane(npcDestElev, npcDestAxis, ndx, ndy);
    }

    // Update editor (tile picker smooth panning, etc.)
    m_Editor.Update(deltaTime, MakeEditorContext());

    // Build camera update parameters from current frame state
    float baseWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float baseWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    float worldWidth = baseWorldWidth / m_Camera.GetZoom();
    float worldHeight = baseWorldHeight / m_Camera.GetZoom();

    // Check if arrow keys are pressed for manual camera control
    bool arrowUp = glfwGetKey(m_Window, GLFW_KEY_UP) == GLFW_PRESS;
    bool arrowDown = glfwGetKey(m_Window, GLFW_KEY_DOWN) == GLFW_PRESS;
    bool arrowLeft = glfwGetKey(m_Window, GLFW_KEY_LEFT) == GLFW_PRESS;
    bool arrowRight = glfwGetKey(m_Window, GLFW_KEY_RIGHT) == GLFW_PRESS;

    // When the tile picker is open, arrow keys are repurposed for tilepicker panning
    if (m_Editor.IsActive() && m_Editor.IsShowTilePicker())
    {
        arrowUp = arrowDown = arrowLeft = arrowRight = false;
    }

    // When in dialogue, arrow keys are used for navigating dialogue options
    if (m_DialogueManager.IsActive() || m_InDialogue || m_DialogueSnap.active)
    {
        arrowUp = arrowDown = arrowLeft = arrowRight = false;
    }

    // When the developer console is open, arrows belong to text editing
    // (cursor movement / history). Don't pan the camera underneath.
    if (m_Console.IsOpen())
    {
        arrowUp = arrowDown = arrowLeft = arrowRight = false;
    }

    // Check if WASD keys are pressed for player movement
    bool wasdPressed = (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS ||
                        glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS ||
                        glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS ||
                        glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS);

    // When the developer console is open, those keystrokes belong to text
    // input, not camera-follow. Player movement WASD is already gated up in
    // ProcessInput; this signal is the camera's smooth-vs-grid follow toggle,
    // which would otherwise jitter the view as the user types into the console.
    if (m_Console.IsOpen())
    {
        wasdPressed = false;
    }

    // Camera follow target: use actual player position while moving for smooth tracking,
    // and tile center when idle so the camera settles on the grid.
    glm::vec2 playerCamPos = m_Player.GetPosition();
    glm::vec2 playerVisualCenter =
        glm::vec2(playerCamPos.x, playerCamPos.y - PlayerCharacter::HITBOX_HEIGHT);
    glm::vec2 smoothTarget = playerVisualCenter - glm::vec2(worldWidth / 2.0f, worldHeight / 2.0f);

    glm::vec2 playerBottomTileCenter = m_Player.GetCurrentTileCenter();
    glm::vec2 tileVisualCenter = glm::vec2(
        playerBottomTileCenter.x, playerBottomTileCenter.y - PlayerCharacter::HITBOX_HEIGHT);
    glm::vec2 gridTarget = tileVisualCenter - glm::vec2(worldWidth / 2.0f, worldHeight / 2.0f);

    // While WASD is held, follow the player directly; otherwise settle onto the grid
    glm::vec2 snappedTarget = wasdPressed ? smoothTarget : gridTarget;

    CameraUpdateParams camParams;
    camParams.deltaTime = deltaTime;
    camParams.playerFollowTarget = snappedTarget;
    camParams.playerMoving = wasdPressed;
    camParams.arrowUp = arrowUp;
    camParams.arrowDown = arrowDown;
    camParams.arrowLeft = arrowLeft;
    camParams.arrowRight = arrowRight;
    camParams.shiftHeld = (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                           glfwGetKey(m_Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
    camParams.baseWorldWidth = baseWorldWidth;
    camParams.baseWorldHeight = baseWorldHeight;
    camParams.mapPixelWidth =
        static_cast<float>(m_Tilemap.GetMapWidth() * m_Tilemap.GetTileWidth());
    camParams.mapPixelHeight =
        static_cast<float>(m_Tilemap.GetMapHeight() * m_Tilemap.GetTileHeight());
    camParams.skipMapClamping = m_Editor.IsActive() && m_Camera.IsFreeMode();
    camParams.tileWidth = m_Tilemap.GetTileWidth();
    camParams.tileHeight = m_Tilemap.GetTileHeight();
    m_Camera.Update(camParams);

    // Resolve player vs NPC collisions using axis-aligned bounding boxes.
    // Both player and NPCs use bottom-center anchored hitboxes (16x16 pixels).
    // When collision is detected, the NPC is stopped to prevent overlap.
    const float PLAYER_HALF_W = PlayerCharacter::HITBOX_WIDTH * 0.5f;
    const float PLAYER_BOX_H = PlayerCharacter::HITBOX_HEIGHT;

    // Build an AABB from a bottom-center anchor point.
    // The anchor is at the character's feet; the box extends upward and outward.
    auto makePlayerAABB = [&](const glm::vec2& anchorPos) -> auto
    {
        struct AABB
        {
            float minX, minY, maxX, maxY;
        };

        AABB box;

        // Horizontal extents around the centerline.
        box.minX = anchorPos.x - PLAYER_HALF_W;
        box.maxX = anchorPos.x + PLAYER_HALF_W;

        // Vertical extents from the bottom of the collider.
        box.maxY = anchorPos.y;
        box.minY = anchorPos.y - PLAYER_BOX_H;

        return box;
    };

    auto playerBox = makePlayerAABB(playerPos);
    auto overlaps = [](const auto& a, const auto& b)
    { return (a.minX < b.maxX && a.maxX > b.minX && a.minY < b.maxY && a.maxY > b.minY); };

    // Check for player-NPC collisions and stop NPCs when colliding
    for (auto& npc : m_NPCs)
    {
        auto npcBox = makePlayerAABB(npc.GetPosition());
        if (overlaps(playerBox, npcBox))
        {
            // Stop NPC while colliding with the player
            npc.SetStopped(true);
        }
        else
        {
            // If not overlapping this frame, allow it to move again
            npc.SetStopped(false);
        }
    }
}

void Game::Render()
{
    // Guard against reentrant calls: WindowRefreshCallback can re-enter during
    // resize, and SnapWindowToTileBoundaries() called from Update() can fire
    // the same callback synchronously. Bail in both cases.
    if (m_IsRendering || m_IsUpdating)
    {
        return;
    }
    struct RenderGuard
    {
        bool& flag;
        RenderGuard(bool& f)
            : flag(f)
        {
            flag = true;
        }
        ~RenderGuard() { flag = false; }
    } renderGuard(m_IsRendering);

    // DIAGNOSTIC: temporarily restore the V1 minimal Title render path
    // (clear + UI only, no world geometry) to isolate whether the white-screen
    // bug is in running the regular Render pipeline for Title, or elsewhere.
    if (m_GameMode == GameMode::Title)
    {
        RenderTitleFrame();
        return;
    }

    // Render order: see docs/RENDERING.md (also summarized in CLAUDE.md).

    // Debug draw sleep: pauses after each draw call for visual debugging
    if (IsDebugDrawSleepEnabled())
    {
        ResetDebugDrawCallIndex();
        Logger::Debug(LOG_SUBSYSTEM, "===== FRAME START =====");
    }

    m_Renderer->BeginFrame();

    // Redirect the world+sky+lights pass to an offscreen scene FBO so the
    // post-FX pipeline can composite bloom, color grading, vignette, and
    // film grain. UI passes (editor, dialogue, debug) draw directly to the
    // swapchain after EndSceneApplyPostFX, keeping text sharp and ungrained.
    m_Renderer->BeginScene();

    DrawTracer::Mark("== gameplay frame ==", m_Renderer->GetDrawCallCount());

    // Use sky color from TimeManager for clear color
    glm::vec3 skyColor = m_TimeManager.GetSkyColor();
    m_Renderer->Clear(skyColor.r, skyColor.g, skyColor.b, 1.0f);

    // Calculate world space size from actual screen dimensions (not truncated tile count)
    // This ensures viewport calculations match the true visible area
    float worldWidth = static_cast<float>(m_ScreenWidth) / static_cast<float>(PIXEL_SCALE);
    float worldHeight = static_cast<float>(m_ScreenHeight) / static_cast<float>(PIXEL_SCALE);

    // Set ambient color for world rendering (day & night tint)
    m_Renderer->SetAmbientColor(m_TimeManager.GetAmbientColor());

    // Apply camera zoom to the projection matrix
    // Zoom > 1.0 = smaller world view, Zoom < 1.0 = larger world view
    float zoomedWidth = worldWidth / m_Camera.GetState().zoom;
    float zoomedHeight = worldHeight / m_Camera.GetState().zoom;
    m_Camera.ConfigurePerspective(*m_Renderer, zoomedWidth, zoomedHeight);
    glm::mat4 projection = CameraController::GetOrthoProjection(zoomedWidth, zoomedHeight);
    m_Renderer->SetProjection(projection);

    // Snap camera to pixel grid for rendering to avoid per-frame jitter seams (OpenGL only)
    const glm::vec2 originalCamera = m_Camera.GetState().position;
    glm::vec2 renderCam = originalCamera;
    glm::vec2 renderSize(zoomedWidth, zoomedHeight);
    glm::vec2 cullCam = originalCamera;  // use unsnapped camera for visibility tests
    glm::vec2 cullSize(zoomedWidth, zoomedHeight);
    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        const float pixelStepX = zoomedWidth / static_cast<float>(m_ScreenWidth);
        const float pixelStepY = zoomedHeight / static_cast<float>(m_ScreenHeight);
        auto snapToPixel = [](float value, float step)
        { return (step > 0.0f) ? std::round(value / step) * step : value; };
        renderCam.x = snapToPixel(originalCamera.x, pixelStepX);
        renderCam.y = snapToPixel(originalCamera.y, pixelStepY);
    }

    // @author Codex (https://github.com/codex)
    // With perspective off, the cull rect is exactly the
    // camera viewport. With perspective on, the horizon foreshortens distant
    // tiles into a smaller screen-space footprint, so much *more* world fits
    // above the horizon than the viewport's world-space size would imply.
    // We compensate by inflating the cull rect by 1/horizonScale (the smaller
    // horizonScale gets, the more we expand) and then further by the projection
    // mode's width/height scales (globe/fisheye warp differently from a flat
    // tilt). renderCam may be pixel-snapped (OpenGL) for drawing; cullCam stays
    // unsnapped so the visibility test is stable across sub-pixel camera moves.
    if (m_Camera.GetState().enable3DEffect)
    {
        float horizonScale =
            HORIZON_SCALE_BASE + (1.0f - m_Camera.GetState().tilt) * HORIZON_SCALE_TILT_RANGE;
        float expansion = 1.0f / horizonScale;
        auto persp = m_Renderer->GetPerspectiveState();
        bool hasGlobe = persp.enabled && (persp.mode == IRenderer::ProjectionMode::Globe ||
                                          persp.mode == IRenderer::ProjectionMode::Fisheye);
        float cullWidthScale =
            static_cast<float>(perspectiveTransform::GetPerspectiveCullWidthScale(hasGlobe));
        float cullHeightScale =
            static_cast<float>(perspectiveTransform::GetPerspectiveCullHeightScale(hasGlobe));
        float expandedWidth = zoomedWidth * expansion * cullWidthScale;
        float expandedHeight = zoomedHeight * expansion * cullHeightScale;

        // Center the expanded cull rect on the camera position
        float widthDiff = (expandedWidth - zoomedWidth) * 0.5f;
        float heightDiff = (expandedHeight - zoomedHeight) * 0.5f;
        cullCam.x = originalCamera.x - widthDiff;
        cullCam.y = originalCamera.y - heightDiff;
        cullSize = glm::vec2(expandedWidth, expandedHeight);
    }

    // Use snapped camera for rendering when OpenGL (restore at end of function)
    m_Camera.GetState().position = renderCam;

    // Render layers in order with Y-sorted tiles:
    // 1. Background layers (Ground, Ground Detail, Objects, Objects2)
    // 2. Y-sorted pass: Y-sorted tiles from ALL layers + NPCs + player
    // 3. Foreground layers (Foreground, Foreground2, Overlay, Overlay2)

    // Render background layers - Y-sorted and no-projection tiles are skipped
    DrawTracer::Mark("section: BackgroundLayers", m_Renderer->GetDrawCallCount());
    m_Tilemap.RenderBackgroundLayers(*m_Renderer, renderCam, renderSize, cullCam, cullSize);

    // Suspend perspective for character rendering
    m_Renderer->SuspendPerspective(true);

    // Render no-projection tiles from background layers (buildings & entities that should appear
    // upright)
    DrawTracer::Mark("section: BackgroundLayersNoProjection", m_Renderer->GetDrawCallCount());
    m_Tilemap.RenderBackgroundLayersNoProjection(
        *m_Renderer, renderCam, renderSize, cullCam, cullSize);

    // Collect Y-sorted tiles from all layers
    auto ySortPlusTiles = m_Tilemap.GetVisibleYSortPlusTiles(cullCam, cullSize);

    // Build unified render list for Y-sorted tiles and entities.
    // Items are sorted by Y coordinate so objects lower on screen (higher Y)
    // render on top of objects higher on screen (lower Y), creating depth.
    // Characters are split into top/bottom halves for proper occlusion with tiles.
    m_RenderList.clear();
    size_t estimatedSize = ySortPlusTiles.size() + m_NPCs.size() * 2 + 2;
    if (m_RenderList.capacity() < estimatedSize)
    {
        m_RenderList.reserve(estimatedSize);
    }

    // Add Y-sorted tiles (sort by bottom edge of tile)
    // Skip tiles behind the sphere when full globe is visible
    int tileW = m_Tilemap.GetTileWidth();
    int tileH = m_Tilemap.GetTileHeight();
    for (const auto& tile : ySortPlusTiles)
    {
        float tileX = static_cast<float>(tile.x * tileW) - renderCam.x;
        float tileY = static_cast<float>(tile.y * tileH) - renderCam.y;
        glm::vec2 corners[4] = {
            glm::vec2(tileX, tileY),
            glm::vec2(tileX + tileW, tileY),
            glm::vec2(tileX + tileW, tileY + tileH),
            glm::vec2(tileX, tileY + tileH),
        };
        if (m_Renderer->IsPointBehindSphere(corners[0]) &&
            m_Renderer->IsPointBehindSphere(corners[1]) &&
            m_Renderer->IsPointBehindSphere(corners[2]) &&
            m_Renderer->IsPointBehindSphere(corners[3]))
            continue;

        RenderItem item;
        item.type = RenderItem::TILE;
        item.sortY = tile.anchorY;
        item.tile = tile;
        item.npc = nullptr;
        m_RenderList.push_back(item);
    }

    // Add NPCs split into bottom/top halves for proper tile occlusion.
    // The bottom half sorts at the character's anchor (feet) position.
    // The top half sorts slightly higher so it can appear behind tiles
    // that the character is walking past.
    // Skip NPCs behind the sphere when full globe is visible.
    for (const auto& npc : m_NPCs)
    {
        glm::vec2 npcPos = npc.GetPosition();
        float screenX = npcPos.x - renderCam.x;
        float screenY = npcPos.y - renderCam.y;
        if (m_Renderer->IsPointBehindSphere(glm::vec2(screenX, screenY)))
            continue;

        float anchorY = npcPos.y;
        // Bottom half renders at anchor position
        RenderItem bottomItem;
        bottomItem.type = RenderItem::NPC_BOTTOM;
        bottomItem.sortY = anchorY;
        bottomItem.tile = {};
        bottomItem.npc = &npc;
        m_RenderList.push_back(bottomItem);
        // Top half renders slightly above
        RenderItem topItem;
        topItem.type = RenderItem::NPC_TOP;
        topItem.sortY = anchorY - PlayerCharacter::HALF_HITBOX_HEIGHT;
        topItem.tile = {};
        topItem.npc = &npc;
        m_RenderList.push_back(topItem);
    }

    // Add player.
    // Both halves use anchor position for sorting.
    // Skip player if behind the sphere (edge case when zoomed way out).
    // Title mode also skips the player so the menu shows a clean scenic world.
    if (!m_Editor.IsActive() && m_GameMode != GameMode::Title)
    {
        glm::vec2 playerPos = m_Player.GetPosition();
        float playerScreenX = playerPos.x - renderCam.x;
        float playerScreenY = playerPos.y - renderCam.y;
        if (!m_Renderer->IsPointBehindSphere(glm::vec2(playerScreenX, playerScreenY)))
        {
            float playerAnchorY = playerPos.y;  // Bottom-center point
            RenderItem playerBottomItem;
            playerBottomItem.type = RenderItem::PLAYER_BOTTOM;
            playerBottomItem.sortY = playerAnchorY;
            playerBottomItem.tile = {};
            playerBottomItem.npc = nullptr;
            m_RenderList.push_back(playerBottomItem);
            RenderItem playerTopItem;
            playerTopItem.type = RenderItem::PLAYER_TOP;
            playerTopItem.sortY = playerAnchorY;
            playerTopItem.tile = {};
            playerTopItem.npc = nullptr;
            m_RenderList.push_back(playerTopItem);
        }
    }

    // Lower Y renders first. Y-sort-minus tiles (anchor at top) get a half-tile
    // offset; tight epsilon avoids transition flicker. On ties higher enum wins
    // (TILE > PLAYER) so entities sit in front of terrain at equal depth.
    // Half-tile offset for Y-sort-minus tiles (tall features anchored at their top)
    constexpr float YSORT_MINUS_OFFSET = 8.0f;
    // Sub-pixel epsilon for Y-sort-minus vs entity tiebreaking
    constexpr float YSORT_MINUS_EPSILON = 0.1f;
    // General epsilon band for sort stability (prevents z-fighting within ~1 pixel)
    constexpr float YSORT_DEPTH_EPSILON = 1.0f;

    std::stable_sort(
        m_RenderList.begin(),
        m_RenderList.end(),
        [](const RenderItem& a, const RenderItem& b)
        {
            bool aIsYSortMinusTile = (a.type == RenderItem::TILE && a.tile.ySortMinus);
            bool bIsYSortMinusTile = (b.type == RenderItem::TILE && b.tile.ySortMinus);

            bool aIsEntity = (a.type <= RenderItem::NPC_BOTTOM);
            bool bIsEntity = (b.type <= RenderItem::NPC_BOTTOM);

            if ((aIsYSortMinusTile && bIsEntity) || (bIsYSortMinusTile && aIsEntity))
            {
                float aSortY = a.sortY + (aIsYSortMinusTile ? YSORT_MINUS_OFFSET : 0.0f);
                float bSortY = b.sortY + (bIsYSortMinusTile ? YSORT_MINUS_OFFSET : 0.0f);
                if (std::abs(aSortY - bSortY) > YSORT_MINUS_EPSILON)
                {
                    return aSortY < bSortY;
                }
                return a.type < b.type;
            }

            if (std::abs(a.sortY - b.sortY) > YSORT_DEPTH_EPSILON)
            {
                return a.sortY < b.sortY;
            }

            return a.type > b.type;
        });

    // Render sorted list. Perspective state is sticky across iterations:
    // tiles want perspective enabled, entities want it suspended. Flip only
    // on transitions so contiguous runs of either stay in one sprite batch.
    // Pre/post-loop contract: enter and leave with perspective suspended.
    {
        char ysortLabel[64];
        std::snprintf(ysortLabel,
                      sizeof(ysortLabel),
                      "section: Y-sorted pass (%zu items)",
                      m_RenderList.size());
        DrawTracer::Mark(ysortLabel, m_Renderer->GetDrawCallCount());
    }
    bool ySortSuspended = true;
    for (const auto& item : m_RenderList)
    {
        bool wantSuspend = (item.type != RenderItem::TILE);
        if (ySortSuspended != wantSuspend)
        {
            m_Renderer->SuspendPerspective(wantSuspend);
            ySortSuspended = wantSuspend;
        }
        switch (item.type)
        {
            case RenderItem::TILE:
                m_Tilemap.RenderSingleTile(*m_Renderer,
                                           item.tile.x,
                                           item.tile.y,
                                           item.tile.layer,
                                           m_Camera.GetState().position,
                                           item.tile.noProjection ? 1 : 0);
                break;
            case RenderItem::NPC_BOTTOM:
                item.npc->RenderBottomHalf(*m_Renderer, m_Camera.GetState().position);
                break;
            case RenderItem::NPC_TOP:
                item.npc->RenderTopHalf(*m_Renderer, m_Camera.GetState().position);
                break;
            case RenderItem::PLAYER_BOTTOM:
                m_Player.RenderBottomHalf(*m_Renderer, m_Camera.GetState().position);
                break;
            case RenderItem::PLAYER_TOP:
                m_Player.RenderTopHalf(*m_Renderer, m_Camera.GetState().position);
                break;
        }
    }
    if (!ySortSuspended)
    {
        m_Renderer->SuspendPerspective(true);
    }

    // Render no-projection tiles from foreground layers
    DrawTracer::Mark("section: ForegroundLayersNoProjection", m_Renderer->GetDrawCallCount());
    m_Tilemap.RenderForegroundLayersNoProjection(
        *m_Renderer, renderCam, renderSize, cullCam, cullSize);

    // Render noProjection particles, particle system handles suspend internally
    DrawTracer::Mark("section: Particles(noProjection)", m_Renderer->GetDrawCallCount());
    m_Particles.Render(*m_Renderer, m_Camera.GetState().position, true, false);

    // Resume perspective for normal foreground rendering
    // (perspective may still be suspended from Y-sorted loop or RenderForegroundLayersNoProjection
    // if no noProjection structures were processed)
    m_Renderer->SuspendPerspective(false);

    // Render foreground layers, Y-sorted and no-projection tiles are skipped
    DrawTracer::Mark("section: ForegroundLayers", m_Renderer->GetDrawCallCount());
    m_Tilemap.RenderForegroundLayers(*m_Renderer, renderCam, renderSize, cullCam, cullSize);

    // Render regular particles on top of world
    DrawTracer::Mark("section: Particles(world)", m_Renderer->GetDrawCallCount());
    m_Particles.Render(*m_Renderer, m_Camera.GetState().position, false, false);

    // Cloud shadows drift across the world (multiplicative-style darkening).
    // Rendered BEFORE the screen-space sky overlay so shadows darken ground
    // tiles + entities but don't dim sun rays / stars / atmospheric glow.
    DrawTracer::Mark("section: CloudShadows", m_Renderer->GetDrawCallCount());
    m_SkyRenderer.RenderCloudShadows(*m_Renderer,
                                     m_Camera.GetState().position,
                                     glm::vec2(zoomedWidth, zoomedHeight),
                                     m_PostFXTime,
                                     m_TimeManager.GetStarVisibility());

    // World-anchored light pools (lamps, lit windows). Drawn under the world
    // projection with perspective ON so they distort with the world plane and
    // walk past as the player moves. Only contribute when the scene is dim
    // enough to read them (ramps with night factor).
    DrawTracer::Mark("section: WorldLights", m_Renderer->GetDrawCallCount());
    const float nightFactor = m_TimeManager.GetStarVisibility();
    if (nightFactor > 0.01f && !m_Tilemap.GetLights().empty())
    {
        const float hour = m_TimeManager.GetTimeOfDay();
        for (const auto& light : m_Tilemap.GetLights())
        {
            float intensity = ComputeLightIntensity(light.schedule, hour) * nightFactor;
            if (intensity < 0.01f)
                continue;
            glm::vec2 screenPos = light.position - renderCam;
            float diameter = light.radius * 2.0f;
            m_Renderer->DrawSpriteAlpha(m_SkyRenderer.GetLightPoolTexture(),
                                        screenPos - glm::vec2(light.radius),
                                        glm::vec2(diameter),
                                        0.0f,
                                        glm::vec4(light.color, intensity * 0.6f),
                                        true);  // additive
        }
    }

    // Sky pass: rendered under the WORLD projection (no projection swap).
    // Sky elements compute their own parallax offset against `cameraPos`, so
    // they drift slowly with player movement instead of being glued to the
    // screen. Perspective is suspended so the sky doesn't 3D-distort.
    DrawTracer::Mark("section: Sky", m_Renderer->GetDrawCallCount());
    m_Renderer->SuspendPerspective(true);
    m_SkyRenderer.Render(*m_Renderer,
                         m_TimeManager,
                         m_Camera.GetState().position,
                         static_cast<int>(worldWidth),
                         static_cast<int>(worldHeight));
    m_Renderer->SuspendPerspective(false);

    // Composite the offscreen scene through the post-FX chain (bloom +
    // grading + vignette + grain) into the swapchain. Subsequent UI draws
    // (editor overlays, dialogue, debug HUD) go directly to the swapchain
    // and are NOT post-processed.
    {
        PostFXParams postFX;
        postFX.timeOfDay = m_TimeManager.GetTimeOfDay();
        postFX.nightFactor = m_TimeManager.GetStarVisibility();
        postFX.time = m_PostFXTime;
        postFX.postFXEnabled = m_PostFXEnabled;
        if (m_PostFXEnabled)
        {
            postFX.vignetteIntensity = ambience::VIGNETTE_INTENSITY;
            postFX.grainIntensity = ambience::GRAIN_INTENSITY;
            postFX.bloomIntensity = ambience::BLOOM_INTENSITY;
            postFX.gradingParams = ComputeGradingParams(postFX.timeOfDay, postFX.nightFactor);
        }
        else
        {
            postFX.vignetteIntensity = 0.0f;
            postFX.grainIntensity = 0.0f;
            postFX.bloomIntensity = 0.0f;
            postFX.saturation = 1.0f;
            // gradingParams default-constructs to identity (lift=0,gamma=1,gain=1).
            // The shader-side gate (postFXEnabled=false) is the real off-switch;
            // these zeroes are a defensive fallback in case the uniform fails to bind.
        }

        DrawTracer::Mark("section: PostFX", m_Renderer->GetDrawCallCount());
        m_Renderer->EndSceneApplyPostFX(postFX);
    }

    DrawTracer::Mark("section: UI overlays", m_Renderer->GetDrawCallCount());

    // Render editor overlays and tile picker
    if (m_Editor.IsActive() || m_Editor.IsDebugMode())
    {
        m_Editor.Render(MakeEditorContext());
        // Restore world projection after editor rendering (tile picker changes projection)
        m_Renderer->SetProjection(projection);
    }

    // Reset ambient color to white for UI elements (not affected by day/night cycle)
    m_Renderer->SetAmbientColor(glm::vec3(1.0f));

    // Render simple dialogue text above NPC head (fallback for NPCs without dialogue trees)
    if (m_InDialogue)
    {
        IRenderer::PerspectiveSuspendGuard guard(*m_Renderer);
        RenderNPCHeadText();
    }

    // Render branching dialogue tree UI
    if (m_DialogueManager.IsActive())
    {
        IRenderer::PerspectiveSuspendGuard guard(*m_Renderer);
        RenderDialogueTreeBox();
    }

    // Render debug info in top left corner (F4 toggle)
    if (m_Editor.IsShowDebugInfo())
    {
        // Set up UI projection
        glm::mat4 uiProjection = glm::ortho(0.0f,
                                            static_cast<float>(m_ScreenWidth),
                                            static_cast<float>(m_ScreenHeight),
                                            0.0f,
                                            -1.0f,
                                            1.0f);
        m_Renderer->SetProjection(uiProjection);

        // Format FPS text - integer only; the perf command exposes the
        // float value if a precise readout is needed.
        char fpsText[32];
        snprintf(fpsText, sizeof(fpsText), "FPS: %d", static_cast<int>(m_Fps.currentFps + 0.5f));

        // Get player position and tile
        glm::vec2 playerPos = m_Player.GetPosition();
        int playerTileX = static_cast<int>(std::floor(playerPos.x / m_Tilemap.GetTileWidth()));
        int playerTileY = static_cast<int>(std::floor(playerPos.y / m_Tilemap.GetTileHeight()));

        // Format position text
        char posText[64];
        snprintf(posText, sizeof(posText), "Pos: (%.1f, %.1f)", playerPos.x, playerPos.y);

        // Format tile text
        char tileText[32];
        snprintf(tileText, sizeof(tileText), "Tile: (%d, %d)", playerTileX, playerTileY);

        // Draw debug info on left side
        float lineHeight = 28.0f;
        float currentLine = 0.0f;
        m_Renderer->DrawText(fpsText,
                             glm::vec2(DEBUG_TEXT_MARGIN, 32.0f + lineHeight * currentLine++),
                             1.0f,
                             glm::vec3(1.0f, 1.0f, 0.0f),
                             2.0f,
                             0.85f);
        m_Renderer->DrawText(posText,
                             glm::vec2(DEBUG_TEXT_MARGIN, 32.0f + lineHeight * currentLine++),
                             1.0f,
                             glm::vec3(1.0f, 1.0f, 0.0f),
                             2.0f,
                             0.85f);
        m_Renderer->DrawText(tileText,
                             glm::vec2(DEBUG_TEXT_MARGIN, 32.0f + lineHeight * currentLine++),
                             1.0f,
                             glm::vec3(1.0f, 1.0f, 0.0f),
                             2.0f,
                             0.85f);

        // Draw active quests section (with spacing and descriptions)
        auto activeQuests = m_GameState.GetActiveQuests();
        if (!activeQuests.empty())
        {
            currentLine += 0.5f;  // Add spacing before quests section
            glm::vec3 questGold(1.0f, 0.85f, 0.2f);
            glm::vec3 descColor(0.9f, 0.75f, 0.5f);

            for (const auto& quest : activeQuests)
            {
                // Format quest name: "wolf_quest" -> "Wolf Quest"
                std::string displayName = quest;
                // Replace underscores with spaces and capitalize
                for (size_t i = 0; i < displayName.size(); ++i)
                {
                    if (displayName[i] == '_')
                    {
                        displayName[i] = ' ';
                        if (i + 1 < displayName.size())
                        {
                            displayName[i + 1] =
                                static_cast<char>(std::toupper(displayName[i + 1]));
                        }
                    }
                }
                if (!displayName.empty())
                {
                    displayName[0] = static_cast<char>(std::toupper(displayName[0]));
                }

                // Draw quest title with exclamation mark
                float questTextX = 52.0f;  // X position where quest name starts
                glm::vec3 exclamYellow(1.0f, 1.0f, 0.0f);
                m_Renderer->DrawText(">!<",
                                     glm::vec2(DEBUG_TEXT_MARGIN, 32.0f + lineHeight * currentLine),
                                     1.0f,
                                     exclamYellow,
                                     2.0f,
                                     0.85f);
                m_Renderer->DrawText(displayName,
                                     glm::vec2(questTextX, 32.0f + lineHeight * currentLine++),
                                     1.0f,
                                     questGold,
                                     2.0f,
                                     0.85f);

                // Draw quest description if available
                std::string description = m_GameState.GetQuestDescription(quest);
                if (!description.empty())
                {
                    // Truncate after 20 chars at word boundary
                    if (description.size() > 20)
                    {
                        size_t cutPos = 20;
                        // Find end of current word (next space or end of string)
                        while (cutPos < description.size() && description[cutPos] != ' ')
                            ++cutPos;
                        description = description.substr(0, cutPos) + "...";
                    }
                    m_Renderer->DrawText(description,
                                         glm::vec2(questTextX, 32.0f + lineHeight * currentLine++),
                                         0.8f,
                                         descColor,
                                         2.0f,
                                         0.7f);
                }
            }
        }

        // Draw renderer info on right side
        const char* rendererName = (m_RendererAPI == RendererAPI::OpenGL) ? "OpenGL" : "Vulkan";
        float rightMargin = static_cast<float>(m_ScreenWidth) - DEBUG_TEXT_MARGIN;

        // Renderer name
        char rendererText[32];
        snprintf(rendererText, sizeof(rendererText), "%s", rendererName);
        float textWidth = strnlen(rendererText, sizeof(rendererText)) * DEBUG_CHAR_WIDTH;
        m_Renderer->DrawText(rendererText,
                             glm::vec2(rightMargin - textWidth, 32.0f),
                             1.0f,
                             glm::vec3(1.0f, 0.3f, 0.3f),
                             2.0f,
                             0.85f);

        // Resolution
        char resText[32];
        snprintf(resText, sizeof(resText), "%dx%d", m_ScreenWidth, m_ScreenHeight);
        textWidth = strnlen(resText, sizeof(resText)) * DEBUG_CHAR_WIDTH;
        m_Renderer->DrawText(resText,
                             glm::vec2(rightMargin - textWidth, 32.0f + lineHeight),
                             1.0f,
                             glm::vec3(1.0f, 0.3f, 0.3f),
                             2.0f,
                             0.85f);

        // Frame time
        char frameTimeText[32];
        float frameTimeMs = (m_Fps.currentFps > 0) ? (1000.0f / m_Fps.currentFps) : 0.0f;
        snprintf(frameTimeText, sizeof(frameTimeText), "%.2fms", frameTimeMs);
        textWidth = strnlen(frameTimeText, sizeof(frameTimeText)) * DEBUG_CHAR_WIDTH;
        m_Renderer->DrawText(frameTimeText,
                             glm::vec2(rightMargin - textWidth, 32.0f + lineHeight * 2),
                             1.0f,
                             glm::vec3(1.0f, 0.3f, 0.3f),
                             2.0f,
                             0.85f);

        // Zoom level
        char zoomText[32];
        snprintf(zoomText, sizeof(zoomText), "Zoom: %.1fx", m_Camera.GetState().zoom);
        textWidth = strnlen(zoomText, sizeof(zoomText)) * DEBUG_CHAR_WIDTH;
        m_Renderer->DrawText(zoomText,
                             glm::vec2(rightMargin - textWidth, 32.0f + lineHeight * 3),
                             1.0f,
                             glm::vec3(1.0f, 0.3f, 0.3f),
                             2.0f,
                             0.85f);

        // Draw calls (averaged over last second)
        char drawCallText[32];
        snprintf(drawCallText, sizeof(drawCallText), "Draws: %d", m_Fps.currentDrawCalls);
        textWidth = m_Renderer->GetTextWidth(drawCallText, 1.0f);
        m_Renderer->DrawText(drawCallText,
                             glm::vec2(rightMargin - textWidth, 32.0f + lineHeight * 4),
                             1.0f,
                             glm::vec3(1.0f, 0.3f, 0.3f),
                             2.0f,
                             0.85f);

        // Restore world projection (in case EndFrame flushes any batches)
        m_Renderer->SetProjection(projection);
    }

    // Render no-projection anchors on top of everything - but only outside
    // the editor. The editor draws its own structure manipulation visuals,
    // so layering the debug-mode anchor markers on top of it just clutters
    // the active edit view.
    if (m_Editor.IsShowNoProjectionAnchors() && !m_Editor.IsActive())
    {
        IRenderer::PerspectiveSuspendGuard guard(*m_Renderer);
        m_Editor.RenderNoProjectionAnchors(MakeEditorContext());
    }

    // Pause overlay sits on top of the gameplay UI (editor, dialogue, debug)
    // but below the console so the developer REPL stays accessible.
    if (m_GameMode == GameMode::Paused)
    {
        RenderPauseOverlay();
    }

    // Title menu UI sits on top of the (rendered) scenic world.
    if (m_GameMode == GameMode::Title)
    {
        RenderTitleContent();
        if (m_ConfirmOverwriteShown)
        {
            RenderConfirmOverwritePrompt();
        }
    }

    // Developer console renders last so it sits on top of every other layer.
    m_Console.Render(*m_Renderer, m_ScreenWidth, m_ScreenHeight);

    m_Renderer->EndFrame();

    // Restore unsnapped camera for game state updates
    m_Camera.GetState().position = originalCamera;

    // Accumulate draw calls for averaging (calculated in Update())
    m_Fps.drawCallAccumulator += m_Renderer->GetDrawCallCount();

    // Swap buffers
    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        // DEBUG: Print frame end marker
        if (IsDebugDrawSleepEnabled())
        {
            Logger::Debug(LOG_SUBSYSTEM, "===== FRAME END =====");
        }
        glfwSwapBuffers(m_Window);
    }
    // Vulkan handles its own presentation in EndFrame()
    // s_Rendering is reset by RenderGuard destructor
}

void Game::Shutdown()
{
    // Note: Windows timer period (timeBeginPeriod/timeEndPeriod) is managed
    // by the RAII TimerPeriodGuard in Run(), not here.

    if (m_Renderer)
    {
        m_Renderer->Shutdown();
        m_Renderer.reset();
    }

    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    if (m_GlfwInitialized)
    {
        glfwTerminate();
        m_GlfwInitialized = false;
    }
}

bool Game::SwitchRenderer(RendererAPI api)
{
    // Hot-swap between OpenGL and Vulkan renderers at runtime.
    // This requires destroying and recreating the GLFW window because:
    // - OpenGL needs GLFW_OPENGL_CORE_PROFILE context
    // - Vulkan needs GLFW_NO_API (no OpenGL context)
    // All GPU resources (textures, shaders) must be re-uploaded after the switch.

    if (api == m_RendererAPI)
    {
        Logger::InfoF(
            LOG_SUBSYSTEM, "Already using {}", api == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");
        return true;
    }

    if (!IsRendererAvailable(api))
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "Renderer API not available: {}",
                       api == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");
        return false;
    }

    Logger::InfoF(LOG_SUBSYSTEM,
                  "Switching renderer from {} to {}...",
                  m_RendererAPI == RendererAPI::OpenGL ? "OpenGL" : "Vulkan",
                  api == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");

    RendererAPI oldAPI = m_RendererAPI;

    // Shutdown current renderer
    if (m_Renderer)
    {
        m_Renderer->Shutdown();
        m_Renderer.reset();
    }

    // Save window position before destroying (for user convenience)
    int windowX = 0, windowY = 0;
    glfwGetWindowPos(m_Window, &windowX, &windowY);

    // Destroy current window (Vulkan requires GLFW_NO_API, OpenGL needs context)
    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    // Helper lambda: create window, renderer, initialize OpenGL/GLAD, and finalize.
    // Returns true on success, false on failure. On failure, m_Window and m_Renderer
    // may be partially initialized and should be cleaned up by the caller.
    auto setupRendererForAPI = [&](RendererAPI targetAPI) -> bool
    {
        m_RendererAPI = targetAPI;

        // Reset and set window hints for target API
        glfwDefaultWindowHints();
        if (m_RendererAPI == RendererAPI::OpenGL)
        {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        }
        else if (m_RendererAPI == RendererAPI::Vulkan)
        {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }

        // Create new window at same position
        m_Window =
            glfwCreateWindow(m_ScreenWidth, m_ScreenHeight, "rift " RIFT_VERSION, nullptr, nullptr);
        if (!m_Window)
        {
            Logger::ErrorF(LOG_SUBSYSTEM,
                           "Failed to create GLFW window for {}",
                           targetAPI == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");
            return false;
        }
        glfwSetWindowPos(m_Window, windowX, windowY);

        // Restore window callbacks
        glfwSetWindowUserPointer(m_Window, this);
        glfwSetScrollCallback(m_Window, ScrollCallback);
        glfwSetCharCallback(m_Window, CharCallback);
        glfwSetFramebufferSizeCallback(m_Window, FramebufferSizeCallback);
        glfwSetWindowRefreshCallback(m_Window, WindowRefreshCallback);

        // Create new renderer
        m_Renderer = CreateRenderer(m_RendererAPI, m_Window);
        if (!m_Renderer)
        {
            Logger::ErrorF(LOG_SUBSYSTEM,
                           "Failed to create renderer for {}",
                           targetAPI == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
            return false;
        }
        m_Renderer->SetFontCandidates(m_FontCandidates);

        // Initialize OpenGL-specific stuff
        if (m_RendererAPI == RendererAPI::OpenGL)
        {
            // Bind this window's OpenGL context to the current thread.
            glfwMakeContextCurrent(m_Window);
            // Load OpenGL function pointers via GLAD.
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
            {
                Logger::ErrorF(LOG_SUBSYSTEM,
                               "Failed to initialize GLAD for {}",
                               targetAPI == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");
                m_Renderer->Shutdown();
                m_Renderer.reset();
                glfwDestroyWindow(m_Window);
                m_Window = nullptr;
                return false;
            }
            Texture::AdvanceOpenGLContextGeneration();
            // This maps normalized device coordinates to the full framebuffer size.
            glViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
            // Standard alpha blending
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            // Uncapped FPS, potentially tearing
            glfwSwapInterval(0);
        }

        // Initialize renderer
        if (!m_Renderer->Init())
        {
            Logger::Error(LOG_SUBSYSTEM, "Renderer->Init() failed during SwitchRenderer");
            m_Renderer->Shutdown();
            m_Renderer.reset();
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
            return false;
        }

        // Re-apply no-vsync after renderer initialization.
        if (m_RendererAPI == RendererAPI::OpenGL)
        {
            glfwSwapInterval(0);
        }

        // Set viewport and projection
        m_Renderer->SetViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
        float worldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth()) /
                           m_Camera.GetState().zoom;
        float worldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight()) /
                            m_Camera.GetState().zoom;
        m_Camera.ConfigurePerspective(*m_Renderer, worldWidth, worldHeight);
        glm::mat4 projection = CameraController::GetOrthoProjection(worldWidth, worldHeight);
        m_Renderer->SetProjection(projection);

        // Re-upload textures to new renderer
        m_Renderer->UploadTexture(m_Tilemap.GetTilesetTexture());
        m_Player.UploadTextures(*m_Renderer);
        for (auto& npc : m_NPCs)
        {
            npc.UploadTextures(*m_Renderer);
        }
        m_Particles.UploadTextures(*m_Renderer);
        m_SkyRenderer.UploadTextures(*m_Renderer);

        return true;
    };

    // Try to set up the new renderer
    if (setupRendererForAPI(api))
    {
        Logger::InfoF(LOG_SUBSYSTEM,
                      "Renderer switch complete! Now using {}",
                      m_RendererAPI == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");
        // Swap cost (texture re-upload, window recreate) is typically 100-500ms.
        // Re-stamp so the first post-swap frame doesn't see that gap as its
        // dt (which the clamp would truncate to MAX_DELTA_TIME anyway).
        m_LastFrameTime = static_cast<float>(glfwGetTime());
        return true;
    }

    // New renderer failed - attempt rollback to the old renderer
    Logger::WarnF(LOG_SUBSYSTEM,
                  "New renderer failed, attempting rollback to {}...",
                  oldAPI == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");

    if (setupRendererForAPI(oldAPI))
    {
        Logger::WarnF(LOG_SUBSYSTEM,
                      "Rollback successful, still using {}",
                      m_RendererAPI == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");
        m_LastFrameTime = static_cast<float>(glfwGetTime());
        return false;
    }

    // Both renderers failed - fatal, shut down to avoid crash on next frame
    Logger::Fatal(LOG_SUBSYSTEM, "Rollback also failed, shutting down");
    Shutdown();
    return false;
}

void Game::OnFramebufferResized(int width, int height)
{
    // Handle window resize events from GLFW.
    // Updates internal dimensions immediately but defers window snapping to avoid
    // fighting with the user during an active resize drag. After 150ms of no
    // resize events, SnapWindowToTileBoundaries() adjusts the window to align
    // with tile boundaries for pixel-perfect rendering.

    if (!m_Window || width <= 0 || height <= 0)
        return;

    m_ScreenWidth = width;
    m_ScreenHeight = height;

    // Each tile occupies TILE_PIXEL_SIZE * PIXEL_SCALE screen pixels (16 * 5 = 80)
    const int tileScreenSize = TILE_PIXEL_SIZE * PIXEL_SCALE;

    // Calculate visible tiles
    m_TilesVisibleWidth = std::max(1, m_ScreenWidth / tileScreenSize);
    m_TilesVisibleHeight = std::max(1, m_ScreenHeight / tileScreenSize);

    // Update renderer viewport to current size
    if (m_Renderer)
    {
        m_Renderer->SetViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
    }

    // Update OpenGL viewport if using OpenGL
    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        glViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
    }

    // Title-mode camera follows the window: re-anchor the visible rect onto
    // the map center so the grass + particle backdrop stays centered behind
    // the menu when the user grows or shrinks the window. Gameplay keeps
    // its existing follow-the-player camera, which already updates each
    // frame and doesn't need a resize hook.
    if (m_GameMode == GameMode::Title && m_Tilemap.GetMapWidth() > 0 &&
        m_Tilemap.GetMapHeight() > 0)
    {
        const float mapCenterX =
            static_cast<float>(m_Tilemap.GetMapWidth() * m_Tilemap.GetTileWidth()) * 0.5f;
        const float mapCenterY =
            static_cast<float>(m_Tilemap.GetMapHeight() * m_Tilemap.GetTileHeight()) * 0.5f;
        const float worldW = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
        const float worldH = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
        m_Camera.Initialize(glm::vec2(mapCenterX, mapCenterY), worldW, worldH);
    }

    // Schedule a snap after resize settles
    m_ResizeSnapTimer = 0.15f;
    m_PendingWindowSnap = true;
}

void Game::SnapWindowToTileBoundaries()
{
    // Adjust window size to be an exact multiple of tile size.
    // This ensures pixel-perfect tile rendering without fractional scaling.
    // Enforces minimum window size of 5x4 tiles (400x320 at 5x scale).

    if (!m_Window)
        return;

    const int tileScreenSize = TILE_PIXEL_SIZE * PIXEL_SCALE;

    // Round down to nearest tile boundary, enforcing minimum dimensions
    int snappedWidth =
        std::max(5 * tileScreenSize, (m_ScreenWidth / tileScreenSize) * tileScreenSize);
    int snappedHeight =
        std::max(4 * tileScreenSize, (m_ScreenHeight / tileScreenSize) * tileScreenSize);

    // Only resize if not already snapped
    if (snappedWidth != m_ScreenWidth || snappedHeight != m_ScreenHeight)
    {
        glfwSetWindowSize(m_Window, snappedWidth, snappedHeight);
        Logger::InfoF(LOG_SUBSYSTEM,
                      "Window snapped to {}x{} ({}x{} tiles)",
                      snappedWidth,
                      snappedHeight,
                      snappedWidth / tileScreenSize,
                      snappedHeight / tileScreenSize);
    }

    m_PendingWindowSnap = false;
}

void Game::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    // Get "this" back from the window's user data
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (game)
    {
        // Now we can call member functions
        game->OnFramebufferResized(width, height);
    }
}

void Game::WindowRefreshCallback(GLFWwindow* window)
{
    // Called by the OS when the window needs repainting (e.g. during resize drag).
    // Re-renders the scene so the user sees game content instead of white fill.
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (game)
    {
        game->Render();
    }
}

EditorContext Game::MakeEditorContext()
{
    return EditorContext{m_Window,
                         m_ScreenWidth,
                         m_ScreenHeight,
                         m_TilesVisibleWidth,
                         m_TilesVisibleHeight,
                         m_Camera.GetState().position,
                         m_Camera.GetState().followTarget,
                         m_Camera.GetState().hasFollowTarget,
                         m_Camera.GetState().zoom,
                         m_Camera.GetState().freeMode,
                         m_Camera.GetState().enable3DEffect,
                         m_Camera.GetState().tilt,
                         m_Camera.GetState().globeSphereRadius,
                         m_Tilemap,
                         m_Player,
                         m_NPCs,
                         *m_Renderer,
                         m_Particles,
                         m_SaveMapPath};
}
