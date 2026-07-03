#ifdef _WIN32
#define NOMINMAX
#endif

#include "Game.hpp"

#include "AmbienceConfig.hpp"
#include "AnimationState.hpp"
#include "CharacterConstants.hpp"
#include "CharacterKinematics.hpp"
#include "CollisionGeometry.hpp"
#include "DrawTracer.hpp"
#include "Elevation.hpp"
#include "ElevationAxis.hpp"
#include "Facing.hpp"
#include "Identity.hpp"
#include "Logger.hpp"
#include "MathConstants.hpp"
#include "MathUtils.hpp"
#include "Motor.hpp"
#include "NpcAiSystem.hpp"
#include "NpcIdle.hpp"
#include "NpcRender.hpp"
#include "NpcTag.hpp"
#include "OpenGLRenderer.hpp"
#include "Patrol.hpp"
#include "PatrolRoute.hpp"
#include "PlayerModes.hpp"
#include "PlayerMovementSystem.hpp"
#include "PlayerSprite.hpp"
#include "PlayerSystem.hpp"
#include "PostFXParams.hpp"
#include "ProjectManifest.hpp"
#include "RendererFactory.hpp"
#include "Speed.hpp"
#include "Transform.hpp"
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

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Game";
constexpr float HORIZON_SCALE_BASE = 0.6f;
constexpr float HORIZON_SCALE_TILT_RANGE = 0.15f;
constexpr float DEBUG_TEXT_MARGIN = 12.0f;
constexpr float DEBUG_CHAR_WIDTH = 12.0f;
constexpr float DEBUG_HUD_ALPHA = 0.6f;      // Debug/FPS HUD text opacity (slightly transparent).
constexpr float DEBUG_HUD_ALPHA_DIM = 0.5f;  // Dimmer secondary HUD lines (quest descriptions).
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
    // Route ECS contract violations (stale handle, duplicate add, iteration
    // lock) into the Rift log instead of the library default (stderr + abort),
    // so a breach lands in rift.txt with context. Capture-free lambda decays to
    // the required function pointer. Installed only here (the game), never in
    // test setup, since the handler slot is process-global.
    ecs::set_violation_handler([](const char* message) { Logger::Error("ECS", message); });

    // Publish the shared services into the ECS world's globals() singleton so the
    // systems (and spawn) reach them through the world rather than via per-entity
    // back-pointers. The services outlive the registry (Game owns both).
    m_World.globals().obtain<WorldServices>() =
        WorldServices{&m_TextureStore, &m_DialogueStore, &m_Assets, &m_NpcRng, &m_GameState};

    // Mint the player entity (PlayerTag + the player components). Sprite sheets are
    // bound later by PlayerSystem::SwitchCharacter, which reads the services above.
    m_PlayerEntity = EntityStore::SpawnPlayer(m_World);

    Logger::Info(LOG_SUBSYSTEM, "Initialize() step 1: Initializing GLFW...");

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

    glfwSetWindowUserPointer(m_Window, this);

    glfwSetScrollCallback(m_Window, ScrollCallback);
    glfwSetCharCallback(m_Window, CharCallback);
    glfwSetFramebufferSizeCallback(m_Window, FramebufferSizeCallback);
    glfwSetWindowRefreshCallback(m_Window, WindowRefreshCallback);

    // Set true to sleep 2s after each draw call (visual debugging).
    SetDebugDrawSleep(m_Window, false);

    Logger::Info(LOG_SUBSYSTEM, "Initialize() step 7: Creating Renderer...");

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

        glViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glfwSwapInterval(0);  // 0 = no VSync.
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

    // Some drivers/middleware reset swap interval during init; re-apply no-vsync.
    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        glfwSwapInterval(0);
    }

    m_Renderer->SetViewport(0, 0, m_ScreenWidth, m_ScreenHeight);

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
        m_Assets.SetNpcAsset(NpcType::FromSpritePath(npcSpritePath), npcSpritePath);
    }
    m_Editor.Initialize(npcSpritePaths);

    // Cache manifest fields so LoadGameWorld() can re-run (Continue / New Game)
    // without re-reading the file.
    m_DefaultMapWidth = manifest.defaultMapWidth;
    m_DefaultMapHeight = manifest.defaultMapHeight;

    // Register player sprites (static, one-time) and cache the character list
    // so LoadGameWorld can pick a default.
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
            m_Assets.SetCharacterAsset(
                *characterType, spriteType, manifest.ResolvePathString(path));
        }
    }

    m_LastFrameTime = static_cast<float>(glfwGetTime());

    // Bring particles online (textures + tile size set here);
    // zones are set later in LoadTitleScreenWorld.
    m_Particles.LoadTextures(m_TextureStore);
    m_Particles.SetTileSize(m_Tilemap.GetTileWidth(), m_Tilemap.GetTileHeight());
    m_Particles.SetMaxParticlesPerZone(50);

    m_TimeManager.Initialize();
    m_SkyRenderer.Initialize(m_TextureStore);

    m_DialogueManager.Initialize(&m_GameState);

    // Title screen first; the save file is untouched until "Continue"/"New Game".
    // Called last so world setup overrides any earlier defaults (time, particles, camera).
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

    try
    {
        while (!glfwWindowShouldClose(m_Window))
        {
            // Sample frame start before polling so the FPS limiter's deadline
            // covers event processing. Otherwise poll cost lands outside the
            // deadline and jitters FPS with input-event volume.
            double frameStartTime = glfwGetTime();
            float deltaTime = static_cast<float>(frameStartTime) - m_LastFrameTime;
            m_LastFrameTime = static_cast<float>(frameStartTime);

            // Poll before ProcessInput so input sees this frame's key/mouse state
            // (GLFW only updates cached state during poll).
            glfwPollEvents();

            // Clamp dt to survive debugger pauses and window-drag stalls.
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

            // FPS limiter: sleep most of the remaining time, then spin-yield
            // for accuracy. Disabled when targetFps is 0.
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

                    // Even with TimerPeriodGuard raising resolution to 1ms, sleep_for
                    // can still overshoot by about a tick, so keep spinThreshold above
                    // that granularity; high-FPS targets (e.g. 500fps = 2ms budget) then
                    // never call sleep_for.
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
    // SnapWindowToTileBoundaries() can synchronously fire WindowRefreshCallback,
    // which would re-enter Render() mid-Update. The guard makes Render() bail.
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

    // One-shot: first console open during a title session permanently
    // strips the title's ambient zones AND the initial weather so the rest
    // of the session shows only what the user sets via the console. Cleared
    // state persists after the console closes (still in Title); only
    // LoadTitleScreenWorld resets the latch. In-game is unaffected because
    // LoadGameWorld replaced the zone list with the gameplay map's zones.
    if (m_GameMode == GameMode::Title && m_Console.IsOpen() && !m_TitleAmbientCleared)
    {
        m_TitleAmbientCleared = true;
        m_Particles.SetZones(nullptr);
        m_Particles.Clear();
        if (auto* zones = m_Tilemap.GetParticleZonesMutable())
        {
            zones->clear();
        }
        m_TimeManager.SetWeather(WeatherState::Clear);
        m_TimeManager.SetWeatherIntensity(0.0f);
    }

    m_Fps.frameCount++;
    m_Fps.updateTimer += deltaTime;
    if (m_Fps.updateTimer >= 1.0f)  // Refresh FPS display once per second.
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

    // Deferred window snap after resize settles.
    if (m_PendingWindowSnap)
    {
        m_ResizeSnapTimer -= deltaTime;
        if (m_ResizeSnapTimer <= 0.0f)
        {
            SnapWindowToTileBoundaries();
        }
    }

    // Pause freezes everything. Title freezes time/player/NPCs but lets
    // cosmetic systems (sky, particles, animated tiles) keep running so
    // fireflies still drift behind the menu.
    if (m_GameMode == GameMode::Paused)
    {
        return;
    }
    const bool isPlaying = (m_GameMode == GameMode::Playing);

    if (isPlaying)
    {
        PlayerSystem::Update(m_World, m_PlayerEntity, deltaTime);
        m_TimeManager.Update(deltaTime);  // Frozen in Title so night setting holds.
        m_WeatherDirector.Update(deltaTime, m_TimeManager);
    }
    m_SkyRenderer.Update(deltaTime, m_TimeManager);

    glm::vec2 particleCullCam = m_Camera.GetState().position;
    // Accurate pixel-based extent (matches the render projection) so weather +
    // ambient particles cover the true viewport, not a truncated tile count.
    glm::vec2 viewSize = VisibleWorldSizeZoomed();
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
    m_Particles.SetNightFactor(m_TimeManager.GetStarVisibility());
    m_Particles.SetTimeOfDay(m_TimeManager.GetTimeOfDay());
    // Bottom-center of the player sprite; used by PollenStorm / FallingLeaves
    // for hitbox-anchored avoidance.
    m_Particles.SetPlayerPosition(m_World.get<Transform>(m_PlayerEntity).position);
    // Push active weather so the particle system can drive global weather
    // spawning (rain/snow/ash/etc.) across the viewport. The effective def is
    // the director's blended definition mid-transition, the table def otherwise
    // (stable storage either way - see TimeManager::GetEffectiveWeatherDefinition).
    // Wind + spawn streams come from the director's choreography; the
    // effective def keeps feeding the live-read channels (fog alpha). In
    // Title the director never updates, so wind stays at the calm default
    // and the streams stay idle - the backdrop is unchanged.
    m_Particles.SetWind(m_WeatherDirector.GetWindDirection(), m_WeatherDirector.GetWindStrength());
    const WeatherDirector::SpawnStreams streams = m_WeatherDirector.GetSpawnStreams();
    m_Particles.SetWeatherTransition(streams.outgoing, streams.incoming, streams.weight);
    m_Particles.SetWeatherState(&m_TimeManager.GetEffectiveWeatherDefinition(),
                                m_TimeManager.GetWeatherIntensity());
    m_Particles.Update(deltaTime, particleCullCam, viewSize);

    // Post-FX time accumulator (grain noise, subtle time-based motion).
    // Wrap to keep float precision over long sessions.
    m_PostFXTime += deltaTime;
    if (m_PostFXTime > 86400.0f)  // 24h wrap
    {
        m_PostFXTime -= 86400.0f;
    }

    m_Tilemap.UpdateAnimations(deltaTime);

    // Title: nothing else to update - no player, NPCs, or dialogue.
    if (!isPlaying)
    {
        return;
    }

    glm::vec2 playerPos = m_World.get<Transform>(m_PlayerEntity).position;

    if (m_DialogueUi.snap.active)
    {
        if (!HasDialogueNPC())
        {
            m_DialogueUi.snap.active = false;
        }
        else
        {
            const ecs::entity npcE = FindNPCById(m_DialogueUi.npcId);
            m_DialogueUi.snap.timer += deltaTime;
            float duration = std::max(0.05f, m_DialogueUi.snap.duration);
            float t = std::clamp(m_DialogueUi.snap.timer / duration, 0.0f, 1.0f);
            float smoothT = t * t * (3.0f - 2.0f * t);  // Smoothstep easing

            glm::vec2 blendedPlayer =
                m_DialogueUi.snap.playerStart +
                (m_DialogueUi.snap.playerTarget - m_DialogueUi.snap.playerStart) * smoothT;
            glm::vec2 blendedNPC =
                m_DialogueUi.snap.npcStart +
                (m_DialogueUi.snap.npcTarget - m_DialogueUi.snap.npcStart) * smoothT;

            PlayerSystem::SetPositionRaw(m_World, m_PlayerEntity, blendedPlayer);
            m_World.get<Transform>(npcE).position = blendedNPC;
            m_World.get<NpcIdle>(npcE).isStopped = true;
            CharacterKinematics::ResetAnimation(m_World.get<AnimationState>(npcE));
            playerPos = blendedPlayer;

            if (t >= 1.0f)
            {
                if (m_DialogueUi.snap.hasPlayerTile)
                {
                    PlayerSystem::SetTilePosition(m_World,
                                                  m_PlayerEntity,
                                                  m_DialogueUi.snap.playerTileX,
                                                  m_DialogueUi.snap.playerTileY);
                }
                else
                {
                    PlayerSystem::SetPositionRaw(
                        m_World, m_PlayerEntity, m_DialogueUi.snap.playerTarget);
                }
                EntityStore::SetNpcTile(m_World.get<Transform>(npcE),
                                        m_World.get<Patrol>(npcE),
                                        m_World.get<PatrolRoute>(npcE),
                                        m_DialogueUi.snap.npcTileX,
                                        m_DialogueUi.snap.npcTileY,
                                        16,
                                        /*preserveRoute=*/true);

                PlayerSystem::Stop(m_World, m_PlayerEntity);
                // Re-derive the player's logical plane for the snapped-to tile: a snap
                // can cross an elevation boundary, and ProcessPlayerMovement's plane
                // derive is gated off during a snap. (The NPC is repositioned the same
                // way; its plane re-derives next frame in NpcAiSystem::UpdateAll.)
                CharacterKinematics::DerivePlane(m_World.get<Elevation>(m_PlayerEntity),
                                                 m_DialogueUi.snap.playerStart,
                                                 m_World.get<Transform>(m_PlayerEntity).position,
                                                 m_Tilemap);
                m_World.get<Facing>(m_PlayerEntity).dir = m_DialogueUi.snap.playerFacing;
                m_World.get<Facing>(npcE).dir = m_DialogueUi.snap.npcFacing;
                m_World.get<NpcIdle>(npcE).isStopped = true;
                CharacterKinematics::ResetAnimation(m_World.get<AnimationState>(npcE));

                bool startedTree = false;
                if (m_DialogueUi.snap.prefersTree)
                {
                    startedTree = m_DialogueManager.StartDialogue(npcE, m_World);
                    if (startedTree)
                    {
                        m_DialogueUi.page = 0;
                        m_DialogueUi.boxFadeTimer = 0.0f;
                        m_DialogueUi.charReveal = 0.0f;
                    }
                }
                if (!startedTree)
                {
                    m_DialogueUi.inDialogue = true;
                    m_DialogueUi.text = m_DialogueUi.snap.fallbackText;
                }

                m_DialogueUi.snap.active = false;
                playerPos = m_World.get<Transform>(m_PlayerEntity).position;
            }
        }
    }

    if (m_DialogueManager.IsActive())
    {
        m_DialogueUi.boxFadeTimer += deltaTime;
        if (m_DialogueUi.charReveal >= 0.0f)
            m_DialogueUi.charReveal += 35.0f * deltaTime;
    }

    // If the dialogue speaker was removed (editor/console/nav) its id no longer
    // resolves; drop the stale reference so nothing keeps it pinned. The old
    // index-based identity could silently retarget to a different NPC here.
    if (m_DialogueUi.npcId != 0 && !HasDialogueNPC())
    {
        m_DialogueUi.npcId = 0;
    }

    // Update every NPC's patrol/idle AI + logical plane; freeze the active dialogue
    // speaker (id 0 = nobody). The per-NPC orchestration lives in NpcAiSystem::UpdateAll,
    // symmetric with the player's PlayerSystem::Move/Update path.
    const bool inAnyDialogue =
        m_DialogueUi.inDialogue || m_DialogueManager.IsActive() || m_DialogueUi.snap.active;
    const std::uint64_t frozenNpcId = inAnyDialogue ? m_DialogueUi.npcId : 0;
    NpcAiSystem::UpdateAll(m_World, m_Tilemap, playerPos, m_NpcRng, frozenNpcId, deltaTime);

    m_Editor.Update(deltaTime, MakeEditorContext());

    float baseWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float baseWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    float worldWidth = baseWorldWidth / m_Camera.GetZoom();
    float worldHeight = baseWorldHeight / m_Camera.GetZoom();

    bool arrowUp = glfwGetKey(m_Window, GLFW_KEY_UP) == GLFW_PRESS;
    bool arrowDown = glfwGetKey(m_Window, GLFW_KEY_DOWN) == GLFW_PRESS;
    bool arrowLeft = glfwGetKey(m_Window, GLFW_KEY_LEFT) == GLFW_PRESS;
    bool arrowRight = glfwGetKey(m_Window, GLFW_KEY_RIGHT) == GLFW_PRESS;

    // Tile picker and dialogue both repurpose arrow keys; console uses them for
    // text editing. In all three cases, don't also pan the camera.
    if (m_Editor.IsActive() && m_Editor.IsShowTilePicker())
    {
        arrowUp = arrowDown = arrowLeft = arrowRight = false;
    }
    if (m_DialogueManager.IsActive() || m_DialogueUi.inDialogue || m_DialogueUi.snap.active)
    {
        arrowUp = arrowDown = arrowLeft = arrowRight = false;
    }
    if (m_Console.IsOpen())
    {
        arrowUp = arrowDown = arrowLeft = arrowRight = false;
    }

    bool wasdPressed = (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS ||
                        glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS ||
                        glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS ||
                        glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS);

    // When the console is open, WASD belongs to text input. This signal is the
    // camera's smooth-vs-grid follow toggle and would jitter the view as the
    // user types. (Player movement WASD is gated earlier in ProcessInput.)
    if (m_Console.IsOpen())
    {
        wasdPressed = false;
    }

    // Follow actual player position while moving (smooth), tile center when idle
    // (settles on the grid).
    glm::vec2 playerCamPos = m_World.get<Transform>(m_PlayerEntity).position;
    glm::vec2 playerVisualCenter =
        glm::vec2(playerCamPos.x, playerCamPos.y - CharacterConstants::HITBOX_HEIGHT);
    glm::vec2 smoothTarget = playerVisualCenter - glm::vec2(worldWidth / 2.0f, worldHeight / 2.0f);

    glm::vec2 playerBottomTileCenter = PlayerMovementSystem::CurrentTileCenter(
        m_World.get<Transform>(m_PlayerEntity).position, 16.0f);
    glm::vec2 tileVisualCenter = glm::vec2(
        playerBottomTileCenter.x, playerBottomTileCenter.y - CharacterConstants::HITBOX_HEIGHT);
    glm::vec2 gridTarget = tileVisualCenter - glm::vec2(worldWidth / 2.0f, worldHeight / 2.0f);

    glm::vec2 snappedTarget = wasdPressed ? smoothTarget : gridTarget;

    CameraUpdateParams camParams;
    camParams.deltaTime = deltaTime;
    camParams.playerFollowTarget = snappedTarget;
    camParams.playerMoving = wasdPressed;
    camParams.playerVelocity = m_World.get<Motor>(m_PlayerEntity).velocity;
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

    // Stop NPCs overlapping the player (visual de-overlap), after all positions settle.
    NpcAiSystem::ApplyPlayerOverlapStop(m_World, playerPos);
}

void Game::Render()
{
    // Reentrancy guard: WindowRefreshCallback can re-enter during resize, and
    // SnapWindowToTileBoundaries() from Update() can fire it synchronously.
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

    // Title has its own self-contained render path (world tiles, particles, sky,
    // PostFX) and returns before the gameplay Y-sort/entity assembly below.
    if (m_GameMode == GameMode::Title)
    {
        RenderTitleFrame();
        return;
    }

    // Render order: see docs/RENDERING.md (also summarized in CLAUDE.md).

    // Visual-debug pause after each draw call.
    if (IsDebugDrawSleepEnabled())
    {
        ResetDebugDrawCallIndex();
        Logger::Debug(LOG_SUBSYSTEM, "===== FRAME START =====");
    }

    m_Renderer->BeginFrame();

    // World+sky+lights render to an offscreen scene FBO so the post-FX chain
    // can apply bloom/grading/vignette/grain. UI (editor, dialogue, debug)
    // draws directly to the swapchain after EndSceneApplyPostFX, keeping text
    // sharp and ungrained.
    m_Renderer->BeginScene();

    DrawTracer::Mark("== gameplay frame ==", m_Renderer->GetDrawCallCount());

    glm::vec3 skyColor = m_TimeManager.GetSkyColor();
    m_Renderer->Clear(skyColor.r, skyColor.g, skyColor.b, 1.0f);

    // World size from actual screen pixels (not truncated tile count) so the
    // viewport matches the true visible area.
    const glm::vec2 world = VisibleWorldSize();
    float worldWidth = world.x;
    float worldHeight = world.y;

    m_Renderer->SetAmbientColor(m_TimeManager.GetAmbientColor());

    // Apply camera zoom (>1 = smaller world view, <1 = larger).
    float zoomedWidth = worldWidth / m_Camera.GetState().zoom;
    float zoomedHeight = worldHeight / m_Camera.GetState().zoom;
    m_Camera.ConfigurePerspective(*m_Renderer, zoomedWidth, zoomedHeight);
    glm::mat4 projection = CameraController::GetOrthoProjection(zoomedWidth, zoomedHeight);
    m_Renderer->SetProjection(projection);

    // Snap render camera to pixel grid to avoid per-frame jitter seams (OpenGL only).
    // Cull tests use the unsnapped camera so visibility is stable across sub-pixel moves.
    const glm::vec2 originalCamera = m_Camera.GetState().position;
    glm::vec2 renderCam = originalCamera;
    glm::vec2 renderSize(zoomedWidth, zoomedHeight);
    glm::vec2 cullCam = originalCamera;
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

    // Perspective off: cull rect == camera viewport. Perspective on: the horizon
    // foreshortens distant tiles, so much *more* world fits above the horizon
    // than the viewport's world-space size implies. Compensate by inflating the
    // cull rect by 1/horizonScale, then by the projection mode's width/height
    // scales (globe/fisheye warp differently from flat tilt).
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

        float widthDiff = (expandedWidth - zoomedWidth) * 0.5f;
        float heightDiff = (expandedHeight - zoomedHeight) * 0.5f;
        cullCam.x = originalCamera.x - widthDiff;
        cullCam.y = originalCamera.y - heightDiff;
        cullSize = glm::vec2(expandedWidth, expandedHeight);
    }

    // Render uses snapped camera (restored at end of function).
    m_Camera.GetState().position = renderCam;

    // Background layers (Y-sorted and no-projection tiles are skipped here).
    DrawTracer::Mark("section: BackgroundLayers", m_Renderer->GetDrawCallCount());
    m_Tilemap.RenderBackgroundLayers(*m_Renderer, renderCam, renderSize, cullCam, cullSize);

    // Suspend perspective for character rendering.
    m_Renderer->SuspendPerspective(true);

    // No-projection background tiles (buildings/entities that stay upright).
    DrawTracer::Mark("section: BackgroundLayersNoProjection", m_Renderer->GetDrawCallCount());
    m_Tilemap.RenderBackgroundLayersNoProjection(
        *m_Renderer, renderCam, renderSize, cullCam, cullSize);

    auto ySortPlusTiles = m_Tilemap.GetVisibleYSortPlusTiles(cullCam, cullSize);

    // Build unified render list: Y-sorted tiles + entities, sorted by Y so
    // objects lower on screen render on top. Characters split into top/bottom
    // halves for proper tile occlusion.
    m_RenderList.clear();
    size_t estimatedSize = ySortPlusTiles.size() + EntityStore::Count(m_World) * 2 + 2;
    if (m_RenderList.capacity() < estimatedSize)
    {
        m_RenderList.reserve(estimatedSize);
    }

    // Y-sorted tiles (sort key = bottom edge). Skip tiles behind the sphere
    // when the full globe is visible.
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

        Drawable item;
        item.cls = DrawableClass::Tile;
        item.sortY = tile.anchorY;
        item.isYSortMinus = tile.ySortMinus;
        item.tieBias = TIE_TILE;
        item.tile = tile;
        m_RenderList.push_back(item);
    }

    // NPCs split into bottom/top halves for tile occlusion: bottom sorts at the
    // feet anchor, top sorts slightly higher so it can pass behind tall tiles.
    // Skip NPCs behind the sphere when the full globe is visible.
    m_World.each<const Transform, const NpcTag>(
        [&](ecs::entity e, const Transform& xf)
        {
            glm::vec2 npcPos = xf.position;
            float screenX = npcPos.x - renderCam.x;
            float screenY = npcPos.y - renderCam.y;
            if (m_Renderer->IsPointBehindSphere(glm::vec2(screenX, screenY)))
            {
                return;
            }

            AddNpcDrawables(m_RenderList,
                            m_World,
                            e,
                            npcPos,
                            CharacterConstants::HALF_HITBOX_HEIGHT,
                            TIE_NPC_BOTTOM,
                            TIE_NPC_TOP);
        });

    // Player. Both halves sort at the anchor. Skipped behind the sphere (edge
    // case when zoomed out) and in Title (menu shows a clean scenic world).
    if (!m_Editor.IsActive() && m_GameMode != GameMode::Title)
    {
        glm::vec2 playerPos = m_World.get<Transform>(m_PlayerEntity).position;
        float playerScreenX = playerPos.x - renderCam.x;
        float playerScreenY = playerPos.y - renderCam.y;
        if (!m_Renderer->IsPointBehindSphere(glm::vec2(playerScreenX, playerScreenY)))
        {
            AddPlayerDrawables(m_RenderList,
                               m_World,
                               m_PlayerEntity,
                               playerPos,
                               0.0f,
                               TIE_PLAYER_BOTTOM,
                               TIE_PLAYER_TOP);
        }
    }

    // Lower Y renders first; ySortMinus tiles (anchored at top) get a half-tile
    // offset for fair comparison against entity feet; equal-depth ties resolve
    // by tieBias so entities sit in front of terrain. The ordering lives in
    // DrawableDepthLess (RenderDrawable.hpp) so it is unit-tested in isolation.
    std::stable_sort(m_RenderList.begin(), m_RenderList.end(), DrawableDepthLess);

    // Perspective state is sticky across iterations: tiles want it enabled,
    // entities suspended. Flip only on transitions so contiguous runs stay in
    // one sprite batch. Contract: enter and leave with perspective suspended.
    // SuspendPerspective is reference-counted, so any guard a render method
    // constructs internally cycles depth 1<->2 without crossing the 0/1
    // boundary (no flush). Any unbalanced suspend/resume pair inside this
    // loop would leak depth across frames and the BeginFrame assert will fire.
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
        bool wantSuspend = (item.cls == DrawableClass::Entity);
        if (ySortSuspended != wantSuspend)
        {
            m_Renderer->SuspendPerspective(wantSuspend);
            ySortSuspended = wantSuspend;
        }
        if (item.cls == DrawableClass::Entity)
        {
            item.drawHalf(item, *m_Renderer, m_Camera.GetState().position, item.topHalf);
        }
        else
        {
            m_Tilemap.RenderSingleTile(*m_Renderer,
                                       item.tile.x,
                                       item.tile.y,
                                       item.tile.layer,
                                       m_Camera.GetState().position,
                                       item.tile.noProjection ? 1 : 0);
        }
    }
    if (!ySortSuspended)
    {
        m_Renderer->SuspendPerspective(true);
    }

    // No-projection foreground tiles.
    DrawTracer::Mark("section: ForegroundLayersNoProjection", m_Renderer->GetDrawCallCount());
    m_Tilemap.RenderForegroundLayersNoProjection(
        *m_Renderer, renderCam, renderSize, cullCam, cullSize);

    // No-projection particles (particle system handles suspend internally).
    DrawTracer::Mark("section: Particles(noProjection)", m_Renderer->GetDrawCallCount());
    m_Particles.Render(*m_Renderer, m_Camera.GetState().position, true, false);

    // Resume perspective for the regular foreground (may still be suspended from
    // the Y-sorted loop if no no-projection structures were processed).
    m_Renderer->SuspendPerspective(false);

    // Foreground layers (Y-sorted and no-projection tiles are skipped here).
    DrawTracer::Mark("section: ForegroundLayers", m_Renderer->GetDrawCallCount());
    m_Tilemap.RenderForegroundLayers(*m_Renderer, renderCam, renderSize, cullCam, cullSize);

    DrawTracer::Mark("section: Particles(world)", m_Renderer->GetDrawCallCount());
    m_Particles.Render(*m_Renderer, m_Camera.GetState().position, false, false);

    // Cloud shadows (multiplicative darkening). Drawn BEFORE the screen-space
    // sky overlay so they darken ground/entities but not sun rays/stars/glow.
    DrawTracer::Mark("section: CloudShadows", m_Renderer->GetDrawCallCount());
    m_SkyRenderer.RenderCloudShadows(*m_Renderer,
                                     m_Camera.GetState().position,
                                     glm::vec2(zoomedWidth, zoomedHeight),
                                     m_PostFXTime,
                                     m_TimeManager.GetStarVisibility());

    // World-anchored light pools (lamps, lit windows). Drawn under the world
    // projection with perspective ON so they warp with the world plane. Only
    // contribute once the scene is dim enough (ramps with night factor).
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
            m_SkyRenderer.DrawLightPool(*m_Renderer,
                                        screenPos - glm::vec2(light.radius),
                                        glm::vec2(diameter),
                                        0.0f,
                                        glm::vec4(light.color, intensity * 0.6f),
                                        true);  // additive
        }
    }

    // Sky pass under the WORLD projection (no projection swap). Sky elements
    // compute their own parallax against `cameraPos` so they drift slowly with
    // the player. Perspective is suspended so the sky doesn't 3D-distort.
    DrawTracer::Mark("section: Sky", m_Renderer->GetDrawCallCount());
    m_Renderer->SuspendPerspective(true);
    m_SkyRenderer.Render(*m_Renderer,
                         m_TimeManager,
                         m_Camera.GetState().position,
                         static_cast<int>(worldWidth),
                         static_cast<int>(worldHeight));
    m_Renderer->SuspendPerspective(false);

    // Composite the offscreen scene through the post-FX chain (bloom + grading
    // + vignette + grain) into the swapchain. Subsequent UI draws (editor,
    // dialogue, debug HUD) go directly to the swapchain and are NOT post-processed.
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
            // gradingParams default-constructs to identity (lift=0, gamma=1, gain=1).
            // postFXEnabled=false is the real off-switch; these zeroes are a
            // defensive fallback in case the uniform fails to bind.
        }

        DrawTracer::Mark("section: PostFX", m_Renderer->GetDrawCallCount());
        m_Renderer->EndSceneApplyPostFX(postFX);
    }

    DrawTracer::Mark("section: UI overlays", m_Renderer->GetDrawCallCount());

    if (m_Editor.IsActive() || m_Editor.IsDebugMode())
    {
        m_Editor.Render(MakeEditorContext());
        // Tile picker changes projection; restore the world projection.
        m_Renderer->SetProjection(projection);
    }

    // UI ambient is white (not affected by day/night).
    m_Renderer->SetAmbientColor(glm::vec3(1.0f));

    // Fallback head text for NPCs without dialogue trees.
    if (m_DialogueUi.inDialogue)
    {
        IRenderer::PerspectiveSuspendGuard guard(*m_Renderer);
        RenderNPCHeadText();
    }

    if (m_DialogueManager.IsActive())
    {
        IRenderer::PerspectiveSuspendGuard guard(*m_Renderer);
        RenderDialogueTreeBox();
    }

    // Debug HUD in top-left corner (toggled via the debug.info console command).
    if (m_Editor.IsShowDebugInfo())
    {
        glm::mat4 uiProjection = glm::ortho(0.0f,
                                            static_cast<float>(m_ScreenWidth),
                                            static_cast<float>(m_ScreenHeight),
                                            0.0f,
                                            -1.0f,
                                            1.0f);
        m_Renderer->SetProjection(uiProjection);

        // FPS as integer; use the perf command for the precise float.
        char fpsText[32];
        snprintf(fpsText, sizeof(fpsText), "FPS: %d", static_cast<int>(m_Fps.currentFps + 0.5f));

        glm::vec2 playerPos = m_World.get<Transform>(m_PlayerEntity).position;
        int playerTileX = static_cast<int>(std::floor(playerPos.x / m_Tilemap.GetTileWidth()));
        int playerTileY = static_cast<int>(std::floor(playerPos.y / m_Tilemap.GetTileHeight()));

        char posText[64];
        snprintf(posText, sizeof(posText), "Pos: (%.1f, %.1f)", playerPos.x, playerPos.y);

        char tileText[32];
        snprintf(tileText, sizeof(tileText), "Tile: (%d, %d)", playerTileX, playerTileY);

        float lineHeight = 28.0f;
        float currentLine = 0.0f;
        m_Renderer->DrawText(fpsText,
                             glm::vec2(DEBUG_TEXT_MARGIN, 32.0f + lineHeight * currentLine++),
                             1.0f,
                             glm::vec3(1.0f, 1.0f, 0.0f),
                             2.0f,
                             DEBUG_HUD_ALPHA);
        m_Renderer->DrawText(posText,
                             glm::vec2(DEBUG_TEXT_MARGIN, 32.0f + lineHeight * currentLine++),
                             1.0f,
                             glm::vec3(1.0f, 1.0f, 0.0f),
                             2.0f,
                             DEBUG_HUD_ALPHA);
        m_Renderer->DrawText(tileText,
                             glm::vec2(DEBUG_TEXT_MARGIN, 32.0f + lineHeight * currentLine++),
                             1.0f,
                             glm::vec3(1.0f, 1.0f, 0.0f),
                             2.0f,
                             DEBUG_HUD_ALPHA);

        char particlesText[64];
        snprintf(particlesText,
                 sizeof(particlesText),
                 "Particles: %zu live / %zu drawn",
                 m_Particles.GetParticles().size(),
                 m_Particles.GetLastDrawnCount());
        m_Renderer->DrawText(particlesText,
                             glm::vec2(DEBUG_TEXT_MARGIN, 32.0f + lineHeight * currentLine++),
                             1.0f,
                             glm::vec3(1.0f, 1.0f, 0.0f),
                             2.0f,
                             DEBUG_HUD_ALPHA);

        // Active quests with descriptions.
        auto activeQuests = m_GameState.GetActiveQuests();
        if (!activeQuests.empty())
        {
            currentLine += 0.5f;  // Spacing before the quests section.
            glm::vec3 questGold(1.0f, 0.85f, 0.2f);
            glm::vec3 descColor(0.9f, 0.75f, 0.5f);

            for (const auto& quest : activeQuests)
            {
                // "wolf_quest" -> "Wolf Quest".
                std::string displayName = quest;
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

                float questTextX = 52.0f;
                glm::vec3 exclamYellow(1.0f, 1.0f, 0.0f);
                m_Renderer->DrawText(">!<",
                                     glm::vec2(DEBUG_TEXT_MARGIN, 32.0f + lineHeight * currentLine),
                                     1.0f,
                                     exclamYellow,
                                     2.0f,
                                     DEBUG_HUD_ALPHA);
                m_Renderer->DrawText(displayName,
                                     glm::vec2(questTextX, 32.0f + lineHeight * currentLine++),
                                     1.0f,
                                     questGold,
                                     2.0f,
                                     DEBUG_HUD_ALPHA);

                std::string description = m_GameState.GetQuestDescription(quest);
                if (!description.empty())
                {
                    // Truncate after 20 chars, but extend to a word boundary first.
                    if (description.size() > 20)
                    {
                        size_t cutPos = 20;
                        while (cutPos < description.size() && description[cutPos] != ' ')
                            ++cutPos;
                        description = description.substr(0, cutPos) + "...";
                    }
                    m_Renderer->DrawText(description,
                                         glm::vec2(questTextX, 32.0f + lineHeight * currentLine++),
                                         0.8f,
                                         descColor,
                                         2.0f,
                                         DEBUG_HUD_ALPHA_DIM);
                }
            }
        }

        // Right side: renderer, resolution, frame time, zoom, draw calls.
        const char* rendererName = (m_RendererAPI == RendererAPI::OpenGL) ? "OpenGL" : "Vulkan";
        float rightMargin = static_cast<float>(m_ScreenWidth) - DEBUG_TEXT_MARGIN;

        char rendererText[32];
        snprintf(rendererText, sizeof(rendererText), "%s", rendererName);
        float textWidth = strnlen(rendererText, sizeof(rendererText)) * DEBUG_CHAR_WIDTH;
        m_Renderer->DrawText(rendererText,
                             glm::vec2(rightMargin - textWidth, 32.0f),
                             1.0f,
                             glm::vec3(1.0f, 0.3f, 0.3f),
                             2.0f,
                             DEBUG_HUD_ALPHA);

        char resText[32];
        snprintf(resText, sizeof(resText), "%dx%d", m_ScreenWidth, m_ScreenHeight);
        textWidth = strnlen(resText, sizeof(resText)) * DEBUG_CHAR_WIDTH;
        m_Renderer->DrawText(resText,
                             glm::vec2(rightMargin - textWidth, 32.0f + lineHeight),
                             1.0f,
                             glm::vec3(1.0f, 0.3f, 0.3f),
                             2.0f,
                             DEBUG_HUD_ALPHA);

        char frameTimeText[32];
        float frameTimeMs = (m_Fps.currentFps > 0) ? (1000.0f / m_Fps.currentFps) : 0.0f;
        snprintf(frameTimeText, sizeof(frameTimeText), "%.2fms", frameTimeMs);
        textWidth = strnlen(frameTimeText, sizeof(frameTimeText)) * DEBUG_CHAR_WIDTH;
        m_Renderer->DrawText(frameTimeText,
                             glm::vec2(rightMargin - textWidth, 32.0f + lineHeight * 2),
                             1.0f,
                             glm::vec3(1.0f, 0.3f, 0.3f),
                             2.0f,
                             DEBUG_HUD_ALPHA);

        char zoomText[32];
        snprintf(zoomText, sizeof(zoomText), "Zoom: %.1fx", m_Camera.GetState().zoom);
        textWidth = strnlen(zoomText, sizeof(zoomText)) * DEBUG_CHAR_WIDTH;
        m_Renderer->DrawText(zoomText,
                             glm::vec2(rightMargin - textWidth, 32.0f + lineHeight * 3),
                             1.0f,
                             glm::vec3(1.0f, 0.3f, 0.3f),
                             2.0f,
                             DEBUG_HUD_ALPHA);

        // Averaged over last second.
        char drawCallText[32];
        snprintf(drawCallText, sizeof(drawCallText), "Draws: %d", m_Fps.currentDrawCalls);
        textWidth = m_Renderer->GetTextWidth(drawCallText, 1.0f);
        m_Renderer->DrawText(drawCallText,
                             glm::vec2(rightMargin - textWidth, 32.0f + lineHeight * 4),
                             1.0f,
                             glm::vec3(1.0f, 0.3f, 0.3f),
                             2.0f,
                             DEBUG_HUD_ALPHA);

        // Restore world projection in case EndFrame flushes batches.
        m_Renderer->SetProjection(projection);
    }

    // Build-version footer (bottom-right), mirroring the title screen. Shown
    // during normal gameplay; suppressed in the editor (its dense UI owns the
    // screen edges) and outside Playing (Title draws its own footer; the pause
    // menu omits it). RenderVersionFooter switches to a UI projection, so
    // restore the world projection afterward for EndFrame's batch flush.
    if (m_GameMode == GameMode::Playing && !m_Editor.IsActive())
    {
        RenderVersionFooter();
        m_Renderer->SetProjection(projection);
    }

    // No-projection anchors only outside the editor - the editor draws its own
    // structure visuals and adding markers on top just clutters the edit view.
    if (m_Editor.IsShowNoProjectionAnchors() && !m_Editor.IsActive())
    {
        IRenderer::PerspectiveSuspendGuard guard(*m_Renderer);
        m_Editor.RenderNoProjectionAnchors(MakeEditorContext());
    }

    // Pause overlay above gameplay UI but below the console (keep REPL accessible).
    if (m_GameMode == GameMode::Paused)
    {
        RenderPauseOverlay();
    }

    if (m_GameMode == GameMode::Title)
    {
        RenderTitleContent();
        if (m_ConfirmOverwriteShown)
        {
            RenderConfirmOverwritePrompt();
        }
    }

    // Console renders last so it sits on top of every layer.
    m_Console.Render(*m_Renderer, m_ScreenWidth, m_ScreenHeight);

    m_Renderer->EndFrame();

    // Restore the unsnapped camera for game-state updates.
    m_Camera.GetState().position = originalCamera;

    m_Fps.drawCallAccumulator += m_Renderer->GetDrawCallCount();

    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        if (IsDebugDrawSleepEnabled())
        {
            Logger::Debug(LOG_SUBSYSTEM, "===== FRAME END =====");
        }
        glfwSwapBuffers(m_Window);
    }
    // Vulkan presents in EndFrame(). m_IsRendering reset by RenderGuard.
}

void Game::Shutdown()
{
    // Windows timer period (timeBeginPeriod/timeEndPeriod) is owned by
    // TimerPeriodGuard in Run(), not here.

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
    // Hot-swap OpenGL <-> Vulkan at runtime. The GLFW window must be recreated
    // because OpenGL needs GLFW_OPENGL_CORE_PROFILE and Vulkan needs GLFW_NO_API.
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

    if (m_Renderer)
    {
        m_Renderer->Shutdown();
        m_Renderer.reset();
    }

    // Preserve window position across the swap.
    int windowX = 0, windowY = 0;
    glfwGetWindowPos(m_Window, &windowX, &windowY);

    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    // Create window + renderer + (OpenGL: GLAD) for `targetAPI`. Returns true on
    // success; every failure path tears down its own partial state (m_Window and
    // m_Renderer left null), so the rollback caller just retries.
    auto setupRendererForAPI = [&](RendererAPI targetAPI) -> bool
    {
        m_RendererAPI = targetAPI;

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

        glfwSetWindowUserPointer(m_Window, this);
        glfwSetScrollCallback(m_Window, ScrollCallback);
        glfwSetCharCallback(m_Window, CharCallback);
        glfwSetFramebufferSizeCallback(m_Window, FramebufferSizeCallback);
        glfwSetWindowRefreshCallback(m_Window, WindowRefreshCallback);

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

        if (m_RendererAPI == RendererAPI::OpenGL)
        {
            glfwMakeContextCurrent(m_Window);
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
            glViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glfwSwapInterval(0);  // Uncapped FPS; may tear.
        }

        if (!m_Renderer->Init())
        {
            Logger::Error(LOG_SUBSYSTEM, "Renderer->Init() failed during SwitchRenderer");
            m_Renderer->Shutdown();
            m_Renderer.reset();
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
            return false;
        }

        // Re-apply no-vsync (Init can reset it).
        if (m_RendererAPI == RendererAPI::OpenGL)
        {
            glfwSwapInterval(0);
        }

        m_Renderer->SetViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
        const glm::vec2 world = VisibleWorldSizeZoomed();
        float worldWidth = world.x;
        float worldHeight = world.y;
        m_Camera.ConfigurePerspective(*m_Renderer, worldWidth, worldHeight);
        glm::mat4 projection = CameraController::GetOrthoProjection(worldWidth, worldHeight);
        m_Renderer->SetProjection(projection);

        // Re-upload textures to the new renderer. The tileset is a Tilemap
        // texture; every other texture (player + NPC sprite sheets, plus the
        // particle + sky procedural textures) now lives in m_TextureStore and
        // re-uploads in a single pass.
        m_Renderer->UploadTexture(m_Tilemap.GetTilesetTexture());
        m_TextureStore.UploadAll(*m_Renderer);

        // Re-pack characters into the atlas. PackCharactersIntoAtlas rebuilds and
        // re-uploads the atlas from each sheet's retained CPU pixels (not the GL
        // resources uploaded above), overwriting the tileset upload. Run it after
        // the standalone uploads so they remain the fallback if packing fails.
        PackCharactersIntoAtlas();

        return true;
    };

    if (setupRendererForAPI(api))
    {
        Logger::InfoF(LOG_SUBSYSTEM,
                      "Renderer switch complete! Now using {}",
                      m_RendererAPI == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");
        // Swap cost (texture re-upload, window recreate) is ~100-500ms.
        // Re-stamp so the first post-swap frame doesn't see that gap as dt.
        m_LastFrameTime = static_cast<float>(glfwGetTime());
        return true;
    }

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

    // Both renderers failed - fatal, shut down before the next frame.
    Logger::Fatal(LOG_SUBSYSTEM, "Rollback also failed, shutting down");
    Shutdown();
    return false;
}

void Game::OnFramebufferResized(int width, int height)
{
    // Updates dimensions immediately but defers window snapping to avoid
    // fighting with an active resize drag. 150ms after the last resize event,
    // SnapWindowToTileBoundaries() aligns to tile boundaries for pixel-perfect
    // rendering.

    if (!m_Window || width <= 0 || height <= 0)
        return;

    m_ScreenWidth = width;
    m_ScreenHeight = height;

    // Each tile occupies TILE_PIXEL_SIZE * PIXEL_SCALE screen pixels (16*5 = 80).
    const int tileScreenSize = TILE_PIXEL_SIZE * PIXEL_SCALE;

    m_TilesVisibleWidth = std::max(1, m_ScreenWidth / tileScreenSize);
    m_TilesVisibleHeight = std::max(1, m_ScreenHeight / tileScreenSize);

    if (m_Renderer)
    {
        m_Renderer->SetViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
    }

    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        glViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
    }

    // Title mode camera follows the window: re-anchor onto the map center so
    // the backdrop stays centered behind the menu on resize. Gameplay uses
    // follow-the-player and doesn't need a resize hook.
    if (m_GameMode == GameMode::Title && m_Tilemap.GetMapWidth() > 0 &&
        m_Tilemap.GetMapHeight() > 0)
    {
        // Grow the title world (and its particle zones) so grass keeps covering
        // the window, then re-center. Repaints only when the required map size
        // (in tiles) changes.
        RefreshTitleWorldForViewport(/*forceRepaint=*/false);
    }

    // Schedule a snap once the resize settles.
    m_ResizeSnapTimer = 0.15f;
    m_PendingWindowSnap = true;
}

void Game::SnapWindowToTileBoundaries()
{
    // Round window to an exact multiple of tile size for pixel-perfect tile
    // rendering. Minimum is 5x4 tiles (400x320 at 5x scale).
    if (!m_Window)
        return;

    const int tileScreenSize = TILE_PIXEL_SIZE * PIXEL_SCALE;

    int snappedWidth =
        std::max(5 * tileScreenSize, (m_ScreenWidth / tileScreenSize) * tileScreenSize);
    int snappedHeight =
        std::max(4 * tileScreenSize, (m_ScreenHeight / tileScreenSize) * tileScreenSize);

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
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (game)
    {
        game->OnFramebufferResized(width, height);
    }
}

void Game::WindowRefreshCallback(GLFWwindow* window)
{
    // Fired by the OS during resize-drag repaints. Re-render so the user sees
    // game content instead of white fill.
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
                         m_Camera.GetState(),
                         m_Tilemap,
                         m_PlayerEntity,
                         m_World,
                         *m_Renderer,
                         m_Particles,
                         m_SaveMapPath};
}
