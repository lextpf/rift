#include "Game.h"
#include "PlayerCharacter.h"
#include "NonPlayerCharacter.h"
#include "RendererFactory.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cctype>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>

// Defined in main.cpp - enables 2-second sleep after each draw call for debugging render order
extern void SetDebugDrawSleep(GLFWwindow *window, bool enabled);

Game::Game()
    : m_Window(nullptr)
    , m_ScreenWidth(1360)
    , m_ScreenHeight(960)
    , m_TilesVisibleWidth(17)
    , m_TilesVisibleHeight(12)
    , m_ResizeSnapTimer(0.0f)
    , m_PendingWindowSnap(false)
    , m_CameraPosition(0.0f)
    , m_CameraFollowTarget(0.0f)
    , m_HasCameraFollowTarget(false)
    , m_CameraZoom(1.0f)
    , m_CameraTilt(0.2f)
    , m_Enable3DEffect(false)
    , m_GlobeSphereRadius(200.0f)
    , m_FreeCameraMode(false)
    , m_LastFrameTime(0.0f)
    , m_PlayerPreviousPosition(0.0f)
    , m_InDialogue(false)
    , m_DialogueNPC(nullptr)
    , m_DialogueText("")
    , m_Renderer(nullptr)
    , m_FpsUpdateTimer(0.0f)
    , m_FpsConsoleTimer(0.0f)
    , m_FrameCount(0)
    , m_CurrentFps(0.0f)
    , m_TargetFps(0.0f)
    , m_DrawCallAccumulator(0)
    , m_CurrentDrawCalls(0)
    , m_RendererAPI(RendererAPI::OpenGL)
{
}

Game::~Game()
{
    Shutdown();
}

bool Game::Initialize()
{
    std::cout << "Initialize() step 1: Initializing GLFW..." << std::endl;

    // Initialize GLFW
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    std::cout << "Initialize() step 2: Selecting Renderer API..." << std::endl;

    // Default to OpenGL
    m_RendererAPI = RendererAPI::OpenGL;
    std::cout << "Renderer API: OpenGL (press F1 to switch)" << std::endl;
    std::cout << "Available renderers: OpenGL, Vulkan" << std::endl;

    std::cout << "Initialize() step 3: Setting window hints..." << std::endl;

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

    std::cout << "Initialize() step 4: Creating GLFW window..." << std::endl;

    m_Window = glfwCreateWindow(m_ScreenWidth, m_ScreenHeight, "rift", nullptr, nullptr);
    if (!m_Window)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    std::cout << "Initialize() step 5: Setting window callbacks..." << std::endl;

    // Store Game instance pointer in window for callbacks
    glfwSetWindowUserPointer(m_Window, this);

    // Set callbacks
    glfwSetScrollCallback(m_Window, ScrollCallback);
    glfwSetFramebufferSizeCallback(m_Window, FramebufferSizeCallback);
    glfwSetWindowRefreshCallback(m_Window, WindowRefreshCallback);

    // Sleep 2 seconds after each draw call, set to true to enable
    SetDebugDrawSleep(m_Window, false);

    std::cout << "Initialize() step 6: Creating Renderer..." << std::endl;

    // Create renderer based on selected API
    m_Renderer.reset(CreateRenderer(m_RendererAPI, m_Window));
    if (!m_Renderer)
    {
        std::cerr << "Failed to create Renderer" << std::endl;
        glfwTerminate();
        return false;
    }

    std::cout << "Initialize() step 7: Renderer created successfully" << std::endl;

    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        // Make OpenGL context current and initialize GLAD
        glfwMakeContextCurrent(m_Window);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cerr << "Failed to initialize GLAD" << std::endl;
            return false;
        }
        Texture::AdvanceOpenGLContextGeneration();

        // OpenGL settings
        glViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glfwSwapInterval(0); // VSync: 0 = disabled, 1 = enabled
    }

    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        std::cout << "OpenGL: " << glGetString(GL_VERSION) << std::endl;
        std::cout << "GLSL: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    }
    else
    {
        // TODO: report Vulkan device/driver info via renderer instead of calling glGetString().
    }

    // Initialize renderer
    std::cout << "About to call Renderer->Init()..." << std::endl;
    try
    {
        m_Renderer->Init();
        std::cout << "Renderer->Init() completed successfully" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception during Renderer initialization: " << e.what() << std::endl;
        return false;
    }
    catch (...)
    {
        std::cerr << "Unknown exception during Renderer initialization" << std::endl;
        return false;
    }

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
    ConfigureRendererPerspective(initWorldWidth, initWorldHeight);
    glm::mat4 projection = GetOrthoProjection(initWorldWidth, initWorldHeight);
    m_Renderer->SetProjection(projection);

    // Load combined tilemap from a list of tileset files
    std::vector<std::string> tilesetPaths =
    {
        "assets/overworld/cb5fa6a6-f88d-47ca-95d6-c73cc79f879d.png",
        "assets/overworld/5ee53950-ea54-41c5-93d3-991e1407cb8b.png",
        "assets/overworld/fd3ff88b-f533-4d40-947c-2c7e5e90839c.png",
        "assets/overworld/11941f71-5703-4a5b-b167-9cd53f88e10e.png",
        "assets/overworld/2b0922a6-66f8-4137-89af-45aaabc5434f.png",
        "assets/overworld/40954708-5e64-4179-8faa-3bd3068de66c.png",
        "assets/overworld/1bc8e647-5e22-4456-839a-845991ba4255.png",
        "assets/overworld/145bb27c-c01d-44fd-b820-2f36f37673f2.png",
        "assets/overworld/6a913092-f773-4d2f-a5d7-09a8d9fbb401.png",
    };

    // Load tilesets from current directory first, then try parent directory.
    // This handles both running from build/ subdirectory and project root.
    bool loaded = m_Tilemap.LoadCombinedTilesets(tilesetPaths, m_Tilemap.GetTileWidth(), m_Tilemap.GetTileHeight());
    if (!loaded)
    {
        std::vector<std::string> parentPaths = tilesetPaths;
        for (auto &path : parentPaths)
        {
            path = "../" + path;
        }
        loaded = m_Tilemap.LoadCombinedTilesets(parentPaths, m_Tilemap.GetTileWidth(), m_Tilemap.GetTileHeight());

        if (!loaded)
        {
            std::cerr << "Failed to load combined tileset. Tried:" << std::endl;
            std::cerr << "  Current directory:" << std::endl;
            for (const auto &path : tilesetPaths)
            {
                std::cerr << "    " << path << std::endl;
            }
            std::cerr << "  Parent directory:" << std::endl;
            for (const auto &path : parentPaths)
            {
                std::cerr << "    " << path << std::endl;
            }
            return false;
        }
    }

    // Initialize editor with available NPC types
    m_Editor.Initialize({
        "assets/non-player/f8cb6fd1-b8a5-44df-b017-c6cc9834353f.png",
        "assets/non-player/ccdc6c30-ecf8-4d08-b5ef-1307d84eecf0.png",
        "assets/non-player/8eb301d1-1dd4-4044-8718-72de1e7b981b.png",
        "assets/non-player/5a5f49f1-32be-4645-b5ca-6c0817461253.png",
        "assets/non-player/d06a4775-e373-4c7a-acfb-6b8fe5f01ca1.png",
        "assets/non-player/908fc99d-b456-45a2-937c-074413e8f664.png",
        "assets/non-player/f7e4604c-a458-4096-bbba-59149419c650.png",
        "assets/non-player/94c6b5b9-99fa-4f3d-bab5-b93684c934e5.png"
    });

    // Try to load save from JSON first, if it exists
    // If loading fails, generate a default map
    int loadedPlayerTileX = -1;
    int loadedPlayerTileY = -1;
    int loadedCharacterType = -1;
    bool mapLoaded = m_Tilemap.LoadMapFromJSON("save.json", &m_NPCs, &loadedPlayerTileX, &loadedPlayerTileY, &loadedCharacterType);
    if (!mapLoaded)
    {
        std::cout << "No existing save found, generating default map" << std::endl;
        m_Tilemap.SetTilemapSize(125, 125);  // This will generate the default map
    }

    // Upload tileset texture to Vulkan renderer
    if (m_RendererAPI == RendererAPI::Vulkan)
    {
        m_Renderer->UploadTexture(m_Tilemap.GetTilesetTexture());
        std::cout << "Tileset texture uploaded to Vulkan" << std::endl;
    }

    // Configure player asset paths
    PlayerCharacter::SetCharacterAsset(CharacterType::BW1_MALE, "Walking", "assets/player/1135c14b-d3cb-414e-8b87-8dca516ba610.png");
    PlayerCharacter::SetCharacterAsset(CharacterType::BW1_MALE, "Running", "assets/player/2444a0be-9d2a-4b12-9921-4ca1956e7107.png");
    PlayerCharacter::SetCharacterAsset(CharacterType::BW1_MALE, "Bicycle", "assets/player/e6b68c46-ab34-4dbb-bca0-93710e3a433c.png");

    PlayerCharacter::SetCharacterAsset(CharacterType::BW1_FEMALE, "Walking", "assets/player/5f3431e3-4835-4266-af9c-505b771122ee.png");
    PlayerCharacter::SetCharacterAsset(CharacterType::BW1_FEMALE, "Running", "assets/player/e2216c65-fef8-41c9-a5b8-911a962d7ae2.png");
    PlayerCharacter::SetCharacterAsset(CharacterType::BW1_FEMALE, "Bicycle", "assets/player/9ba37d2a-fe59-4fee-86d5-ca1e17bca11f.png");

    PlayerCharacter::SetCharacterAsset(CharacterType::BW2_MALE, "Walking", "assets/player/f3a3f051-382e-4653-8449-131d2a75548e.png");
    PlayerCharacter::SetCharacterAsset(CharacterType::BW2_MALE, "Running", "assets/player/b67d0c3e-b2d1-48bc-b0a9-2ea5a42037c8.png");
    PlayerCharacter::SetCharacterAsset(CharacterType::BW2_MALE, "Bicycle", "assets/player/1023c322-8f93-4f73-8772-7543bf832569.png");

    PlayerCharacter::SetCharacterAsset(CharacterType::BW2_FEMALE, "Walking", "assets/player/1ce93276-4959-476f-adeb-508c86802567.png");
    PlayerCharacter::SetCharacterAsset(CharacterType::BW2_FEMALE, "Running", "assets/player/2f1d4723-c682-4d21-9991-af4f3513bdc1.png");
    PlayerCharacter::SetCharacterAsset(CharacterType::BW2_FEMALE, "Bicycle", "assets/player/980d60d7-3bbc-4c1f-9681-5b7f371d4605.png");

    PlayerCharacter::SetCharacterAsset(CharacterType::CC_FEMALE, "Walking", "assets/player/17d3da80-9b85-42e5-adf8-fd5823962f20.png");
    PlayerCharacter::SetCharacterAsset(CharacterType::CC_FEMALE, "Running", "assets/player/2f079f34-3ea2-4c6a-a054-de5ba9c44e2f.png");
    PlayerCharacter::SetCharacterAsset(CharacterType::CC_FEMALE, "Bicycle", "assets/player/e23ea083-b992-42dd-8dd2-690f246bc164.png");

    // After tilemap is loaded, instead of manual sprite loads:
    // Use saved character type or default to BW1_MALE
    CharacterType initialCharacter = (loadedCharacterType >= 0)
                                         ? static_cast<CharacterType>(loadedCharacterType)
                                         : CharacterType::BW1_MALE;
    if (!m_Player.SwitchCharacter(initialCharacter))
    {
        std::cerr << "Failed to initialize player sprites!" << std::endl;
        return false;
    }

    // Upload player sprite textures to Vulkan renderer
    if (m_RendererAPI == RendererAPI::Vulkan)
    {
        // For now, the textures will be uploaded when first used in DrawSpriteRegion
        // But we can add a public method to PlayerCharacter to upload textures if needed
        std::cout << "PlayerCharacter sprites loaded, textures will be uploaded on first use" << std::endl;
    }

    // Camera viewport size
    float camWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float camWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());

    // Place player at saved position or default (9, 5)
    // Player takes up 2 tiles in height
    int playerTileX = (loadedPlayerTileX >= 0) ? loadedPlayerTileX : 9;
    int playerTileY = (loadedPlayerTileY >= 0) ? loadedPlayerTileY : 5;

    m_Player.SetTilePosition(playerTileX, playerTileY);
    glm::vec2 playerPos = m_Player.GetPosition();

    // Center camera on player's visual center
    // Player's visual center is at playerPos.y - HITBOX_HEIGHT (middle of 32px sprite)
    glm::vec2 playerVisualCenter = glm::vec2(playerPos.x, playerPos.y - PlayerCharacter::HITBOX_HEIGHT);
    m_CameraPosition = playerVisualCenter - glm::vec2(camWorldWidth / 2.0f, camWorldHeight / 2.0f);
    // Initialize follow target to current camera position
    m_CameraFollowTarget = m_CameraPosition;
    m_HasCameraFollowTarget = false;

    // Clamp camera to map bounds
    float mapWidth = static_cast<float>(m_Tilemap.GetMapWidth() * m_Tilemap.GetTileWidth());
    float mapHeight = static_cast<float>(m_Tilemap.GetMapHeight() * m_Tilemap.GetTileHeight());
    m_CameraPosition.x = std::max(0.0f, std::min(m_CameraPosition.x, mapWidth - camWorldWidth));
    m_CameraPosition.y = std::max(0.0f, std::min(m_CameraPosition.y, mapHeight - camWorldHeight));

    std::cout << "Map size: " << m_Tilemap.GetMapWidth() << "x" << m_Tilemap.GetMapHeight()
              << " tiles = " << mapWidth << "x" << mapHeight << " pixels" << std::endl;
    std::cout << "Camera view: " << camWorldWidth << "x" << camWorldHeight << " pixels (" << m_TilesVisibleWidth << " tiles wide, " << m_TilesVisibleHeight << " tiles tall)" << std::endl;
    std::cout << "Player position: (" << playerPos.x << ", " << playerPos.y << ") - Tile (" << playerTileX << ", " << playerTileY << ")" << std::endl;
    std::cout << "Camera position: (" << m_CameraPosition.x << ", " << m_CameraPosition.y << ")" << std::endl;
    std::cout << "PlayerCharacter size: " << PlayerCharacter::RENDER_WIDTH << "x" << PlayerCharacter::RENDER_HEIGHT << " pixels (ONE TILE)" << std::endl;

    m_LastFrameTime = static_cast<float>(glfwGetTime());

    // Initialize particle system
    m_Particles.LoadTextures();
    m_Particles.SetZones(m_Tilemap.GetParticleZones());
    m_Particles.SetTileSize(m_Tilemap.GetTileWidth(), m_Tilemap.GetTileHeight());
    m_Particles.SetTilemap(&m_Tilemap);
    m_Particles.SetMaxParticlesPerZone(50);

    // Initialize day & night cycle
    m_TimeManager.Initialize();
    m_TimeManager.SetDayDuration(240.0f); // 240 seconds = 1 Game day
    m_SkyRenderer.Initialize();

    // Initialize dialogue system
    m_DialogueManager.Initialize(this, &m_GameState);

    return true;
}

void Game::Run()
{
    // Main game loop. Processes input, updates game state, and renders each frame.
    // Delta time is computed from wall-clock time for frame-rate independent movement.
    try
    {
        while (!glfwWindowShouldClose(m_Window))
        {
            double frameStartTime = glfwGetTime();
            float deltaTime = static_cast<float>(frameStartTime) - m_LastFrameTime;
            m_LastFrameTime = static_cast<float>(frameStartTime);

            // Clamp deltaTime to prevent physics explosions after debugger pauses or window drag stalls
            constexpr float MAX_DELTA_TIME = 0.1f;
            deltaTime = std::min(deltaTime, MAX_DELTA_TIME);

            try
            {
                ProcessInput(deltaTime);
                Update(deltaTime);
                Render();
            }
            catch (const std::exception &e)
            {
                std::cerr << "Exception in game loop: " << e.what() << std::endl;
                std::cerr.flush();
                break; // Exit loop on error
            }
            catch (...)
            {
                std::cerr << "Unknown exception in game loop" << std::endl;
                std::cerr.flush();
                break;
            }

            glfwPollEvents();

            // FPS limiter: busy-wait until target frame time is reached.
            // Busy-waiting is used instead of sleep() for sub-millisecond accuracy,
            // but this does consume CPU cycles. When m_TargetFps is 0, no limiting.
            if (m_TargetFps > 0.0f)
            {
                double targetFrameTime = 1.0 / static_cast<double>(m_TargetFps);
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
    catch (const std::exception &e)
    {
        std::cerr << "Exception in Run(): " << e.what() << std::endl;
        std::cerr.flush();
    }
    catch (...)
    {
        std::cerr << "Unknown exception in Run()" << std::endl;
        std::cerr.flush();
    }
}

void Game::Update(float deltaTime)
{
    // Compute blend factor for frame-rate independent exponential smoothing.
    // Unlike fixed lerp (e.g., lerp 10% per frame), this produces consistent
    // motion regardless of frame rate.
    //
    // Parameters:
    //   dt - delta time this frame (seconds)
    //   st - settle time: roughly how long to reach the target (seconds)
    //   e  - epsilon: how close to target counts as "arrived" (default 1%)
    //
    // Returns alpha in [0,1] for use with: current = lerp(current, target, alpha)
    auto expApproachAlpha = [](float dt, float st, float e = 0.01f) -> float
    {
        dt = std::max(0.0f, dt);
        st = std::max(1e-5f, st);
        return std::clamp(1.0f - std::pow(e, dt / st), 0.0f, 1.0f);
    };

    // Update FPS counter
    m_FrameCount++;
    m_FpsUpdateTimer += deltaTime;
    if (m_FpsUpdateTimer >= 1.0f) // Update FPS display every second
    {
        m_CurrentFps = m_FrameCount / m_FpsUpdateTimer;
        m_CurrentDrawCalls = (m_FrameCount > 0) ? m_DrawCallAccumulator / m_FrameCount : 0;
        m_FrameCount = 0;
        m_FpsUpdateTimer = 0.0f;
        m_DrawCallAccumulator = 0;
    }

    // Output stats to console every second [deprecated]
    m_FpsConsoleTimer += deltaTime;
    if (m_FpsConsoleTimer >= 1.0f)
    {
        const char *renderer = (m_RendererAPI == RendererAPI::OpenGL) ? "OpenGL" : "Vulkan";
        float frameTimeMs = (m_CurrentFps > 0) ? (1000.0f / m_CurrentFps) : 0.0f;
        /*std::cout << "[" << renderer << "] "
                  << static_cast<int>(m_CurrentFps) << " FPS | "
                  << std::fixed << std::setprecision(4) << frameTimeMs << "ms | "
                  << m_ScreenWidth << "x" << m_ScreenHeight << " | "
                  << std::setprecision(2) << m_CameraZoom << "x zoom"
                  << std::endl;*/
        m_FpsConsoleTimer = 0.0f;
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

    m_Player.Update(deltaTime);

    // Update day & night cycle
    m_TimeManager.Update(deltaTime);
    m_SkyRenderer.Update(deltaTime, m_TimeManager);

    // Update particle system
    float pWorldW = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float pWorldH = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    glm::vec2 viewSize(pWorldW / m_CameraZoom, pWorldH / m_CameraZoom);
    // Set night factor for lantern glows and rays
    m_Particles.SetNightFactor(m_TimeManager.GetStarVisibility());
    m_Particles.Update(deltaTime, m_CameraPosition, viewSize);

    // Update animated tiles
    m_Tilemap.UpdateAnimations(deltaTime);

    // Get player position, needed for NPC updates and collision
    glm::vec2 playerPos = m_Player.GetPosition();

    if (m_DialogueSnapActive)
    {
        if (!m_DialogueNPC)
        {
            m_DialogueSnapActive = false;
        }
        else
        {
            m_DialogueSnapTimer += deltaTime;
            float duration = std::max(0.05f, m_DialogueSnapDuration);
            float t = std::clamp(m_DialogueSnapTimer / duration, 0.0f, 1.0f);
            float smoothT = t * t * (3.0f - 2.0f * t); // Smoothstep easing

            glm::vec2 blendedPlayer = m_DialogueSnapPlayerStart + (m_DialogueSnapPlayerTarget - m_DialogueSnapPlayerStart) * smoothT;
            glm::vec2 blendedNPC = m_DialogueSnapNPCStart + (m_DialogueSnapNPCTarget - m_DialogueSnapNPCStart) * smoothT;

            m_Player.SetPositionRaw(blendedPlayer);
            m_DialogueNPC->SetPosition(blendedNPC);
            m_DialogueNPC->SetStopped(true);
            m_DialogueNPC->ResetAnimationToIdle();
            playerPos = blendedPlayer;

            if (t >= 1.0f)
            {
                if (m_DialogueSnapHasPlayerTile)
                {
                    m_Player.SetTilePosition(m_DialogueSnapPlayerTileX, m_DialogueSnapPlayerTileY);
                }
                else
                {
                    m_Player.SetPositionRaw(m_DialogueSnapPlayerTarget);
                }
                m_DialogueNPC->SetTilePosition(m_DialogueSnapNPCTileX, m_DialogueSnapNPCTileY, 16, true);

                m_Player.Stop();
                m_Player.SetDirection(m_DialogueSnapPlayerFacing);
                m_DialogueNPC->SetDirection(m_DialogueSnapNPCFacing);
                m_DialogueNPC->SetStopped(true);
                m_DialogueNPC->ResetAnimationToIdle();

                bool startedTree = false;
                if (m_DialogueSnapPrefersTree)
                {
                    startedTree = m_DialogueManager.StartDialogue(m_DialogueNPC);
                    if (startedTree)
                    {
                        m_DialoguePage = 0;
                    }
                }
                if (!startedTree)
                {
                    m_InDialogue = true;
                    m_DialogueText = m_DialogueSnapFallbackText;
                }

                m_DialogueSnapActive = false;
                playerPos = m_Player.GetPosition();
            }
        }
    }

    // Update player elevation based on tilemap
    float elevation = m_Tilemap.GetElevationAtWorldPos(playerPos.x, playerPos.y);
    m_Player.SetElevationOffset(elevation);

    // Update NPCs
    // During dialogue, freeze the NPC being talked to
    bool inAnyDialogue = m_InDialogue || m_DialogueManager.IsActive() || m_DialogueSnapActive;
    for (auto &npc : m_NPCs)
    {
        // Skip updating the NPC in dialogue
        if (inAnyDialogue && m_DialogueNPC == &npc)
        {
            continue;
        }
        npc.Update(deltaTime, &m_Tilemap, &playerPos);

        // Update NPC elevation based on tilemap
        glm::vec2 npcPos = npc.GetPosition();
        float npcElevation = m_Tilemap.GetElevationAtWorldPos(npcPos.x, npcPos.y);
        npc.SetElevationOffset(npcElevation);
    }

    // Update editor (tile picker smooth panning, etc.)
    m_Editor.Update(deltaTime, MakeEditorContext());

    // Calculate world space dimensions with camera zoom applied
    float baseWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float baseWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    float worldWidth = baseWorldWidth / m_CameraZoom;
    float worldHeight = baseWorldHeight / m_CameraZoom;

    // Check if arrow keys are pressed for manual camera control
    bool arrowUp = glfwGetKey(m_Window, GLFW_KEY_UP) == GLFW_PRESS;
    bool arrowDown = glfwGetKey(m_Window, GLFW_KEY_DOWN) == GLFW_PRESS;
    bool arrowLeft = glfwGetKey(m_Window, GLFW_KEY_LEFT) == GLFW_PRESS;
    bool arrowRight = glfwGetKey(m_Window, GLFW_KEY_RIGHT) == GLFW_PRESS;

    // When the tile picker is open, arrow keys are repurposed for tilepicker panning
    if (m_Editor.IsActive() && m_Editor.ShowTilePicker())
    {
        arrowUp = arrowDown = arrowLeft = arrowRight = false;
    }

    // When in dialogue, arrow keys are used for navigating dialogue options
    if (m_DialogueManager.IsActive() || m_InDialogue || m_DialogueSnapActive)
    {
        arrowUp = arrowDown = arrowLeft = arrowRight = false;
    }

    // Check if WASD keys are pressed for player movement
    bool wasdPressed = (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS ||
                        glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS ||
                        glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS ||
                        glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS);

    bool arrowKeysPressed = arrowUp || arrowDown || arrowLeft || arrowRight;

    // Camera follow target: use actual player position while moving for smooth tracking,
    // and tile center when idle so the camera settles on the grid.
    glm::vec2 playerCamPos = m_Player.GetPosition();
    glm::vec2 playerVisualCenter = glm::vec2(playerCamPos.x, playerCamPos.y - PlayerCharacter::HITBOX_HEIGHT);
    glm::vec2 smoothTarget = playerVisualCenter - glm::vec2(worldWidth / 2.0f, worldHeight / 2.0f);

    glm::vec2 playerBottomTileCenter = m_Player.GetCurrentTileCenter();
    glm::vec2 tileVisualCenter = glm::vec2(playerBottomTileCenter.x, playerBottomTileCenter.y - PlayerCharacter::HITBOX_HEIGHT);
    glm::vec2 gridTarget = tileVisualCenter - glm::vec2(worldWidth / 2.0f, worldHeight / 2.0f);

    // While WASD is held, follow the player directly; otherwise settle onto the grid
    glm::vec2 snappedTarget = wasdPressed ? smoothTarget : gridTarget;

    // Camera movement modes:
    // - Free camera (Space toggle): Arrow keys pan freely, camera ignores player
    // - Manual pan: Arrow keys override player follow temporarily
    // - Auto follow: Camera smoothly tracks player's tile center position
    if (m_FreeCameraMode)
    {
        if (arrowKeysPressed)
        {
            // Base speed scales with zoom (faster when zoomed out for easier map navigation)
            float cameraSpeed = 600.0f / m_CameraZoom; // Pixels per second

            // Shift modifier for faster panning (2.5x)
            if (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                glfwGetKey(m_Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
            {
                cameraSpeed *= 2.5f;
            }

            glm::vec2 cameraMove(0.0f);

            // Arrow keys pan camera
            if (arrowUp)
                cameraMove.y -= cameraSpeed * deltaTime;
            if (arrowDown)
                cameraMove.y += cameraSpeed * deltaTime;
            if (arrowLeft)
                cameraMove.x -= cameraSpeed * deltaTime;
            if (arrowRight)
                cameraMove.x += cameraSpeed * deltaTime;

            m_CameraPosition += cameraMove;
        }
        else
        {
            // Smoothly snap to tile grid when not moving
            float tileW = static_cast<float>(m_Tilemap.GetTileWidth());
            float tileH = static_cast<float>(m_Tilemap.GetTileHeight());
            glm::vec2 snappedPos;
            snappedPos.x = std::round(m_CameraPosition.x / tileW) * tileW;
            snappedPos.y = std::round(m_CameraPosition.y / tileH) * tileH;

            float alpha = expApproachAlpha(deltaTime, 0.5f); // Faster snap than player follow
            glm::vec2 newPos = m_CameraPosition + (snappedPos - m_CameraPosition) * alpha;

            // Snap exactly when very close to avoid jitter
            if (glm::length(snappedPos - newPos) < 0.1f)
            {
                m_CameraPosition = snappedPos;
            }
            else
            {
                m_CameraPosition = newPos;
            }
        }
        m_HasCameraFollowTarget = false;
    }
    else if (arrowKeysPressed)
    {
        // Manual camera control with arrow keys
        // Speed scales with zoom (faster when zoomed out)
        float cameraSpeed = 600.0f / m_CameraZoom;

        // Shift modifier for faster panning (2.5x)
        if (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(m_Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
        {
            cameraSpeed *= 2.5f;
        }

        glm::vec2 cameraMove(0.0f);

        if (arrowUp)
        {
            // Both renderers use the same coordinate system
            cameraMove.y -= cameraSpeed * deltaTime;
        }
        if (arrowDown)
        {
            cameraMove.y += cameraSpeed * deltaTime;
        }
        if (arrowLeft)
        {
            cameraMove.x -= cameraSpeed * deltaTime;
        }
        if (arrowRight)
        {
            cameraMove.x += cameraSpeed * deltaTime;
        }

        m_CameraPosition += cameraMove;

        // When user manually pans, cancel any automatic follow smoothing
        m_HasCameraFollowTarget = false;
    }
    else
    {
        // No manual camera input.
        // If player is moving with WASD, establish a follow target.
        if (wasdPressed || m_HasCameraFollowTarget)
        {
            m_CameraFollowTarget = snappedTarget;
            m_HasCameraFollowTarget = true;
        }

        // Smoothly move camera towards follow target if we have one
        if (m_HasCameraFollowTarget)
        {
            // Smooth camera follow
            float alpha = expApproachAlpha(deltaTime, 0.6f);

            glm::vec2 newPos = m_CameraPosition + (m_CameraFollowTarget - m_CameraPosition) * alpha;
            // Lerp          = |------ a -----| + |--------------- (b - a) ---------------| *   t

            // If very close to target, snap and stop smoothing to avoid jitter
            if (glm::length(m_CameraFollowTarget - newPos) < 0.1f)
            {
                m_CameraPosition = m_CameraFollowTarget;
                m_HasCameraFollowTarget = false;
            }
            else
            {
                m_CameraPosition = newPos;
            }
        }
        // If no follow target and no Arrows or WASD, camera simply stays where it is.
    }

    // Clamp camera to map bounds after snapping (skip in editor free-camera mode to allow panning beyond map)
    if (!(m_Editor.IsActive() && m_FreeCameraMode))
    {
        float mapWidth = static_cast<float>(m_Tilemap.GetMapWidth() * m_Tilemap.GetTileWidth());
        float mapHeight = static_cast<float>(m_Tilemap.GetMapHeight() * m_Tilemap.GetTileHeight());

        m_CameraPosition.x = std::max(0.0f, std::min(m_CameraPosition.x, mapWidth - worldWidth));
        m_CameraPosition.y = std::max(0.0f, std::min(m_CameraPosition.y, mapHeight - worldHeight));
    }

    // Resolve player vs NPC collisions using axis-aligned bounding boxes.
    // Both player and NPCs use bottom-center anchored hitboxes (16x16 pixels).
    // When collision is detected, the NPC is stopped to prevent overlap.
    const float PLAYER_HALF_W = PlayerCharacter::HITBOX_WIDTH * 0.5f;
    const float PLAYER_BOX_H = PlayerCharacter::HITBOX_HEIGHT;

    // Build an AABB from a bottom-center anchor point.
    // The anchor is at the character's feet; the box extends upward and outward.
    auto makePlayerAABB = [&](const glm::vec2 &anchorPos) -> auto
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
    auto overlaps = [](const auto &a, const auto &b)
    {
        return (a.minX < b.maxX && a.maxX > b.minX &&
                a.minY < b.maxY && a.maxY > b.minY);
    };

    // Check for player-NPC collisions and stop NPCs when colliding
    for (auto &npc : m_NPCs)
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

void Game::ConfigureRendererPerspective(float width, float height)
{
    // Configure the renderer's perspective distortion based on current settings.
    // When 3D effect is enabled, applies a fisheye/globe projection that curves
    // the world and creates a vanishing point effect at the horizon.
    if (m_Enable3DEffect)
    {
        // horizonY: vertical position of the vanishing point (negative = above center)
        // The 0.20 factor controls how much tilt affects horizon placement.
        float horizonY = -height * m_CameraTilt * 0.20f;

        // horizonScale: how much objects shrink at the horizon (0.75 = 75% size minimum)
        // Less tilt means less shrinking (closer to 0.85 at tilt=0).
        float horizonScale = 0.75f + (1.0f - m_CameraTilt) * 0.10f;

        // Scale sphere radius with zoom and viewport, but allow globe to be visible
        float viewportDiagonal = std::sqrt(width * width + height * height);
        float baseRadius = m_GlobeSphereRadius / m_CameraZoom;
        // Minimum radius to prevent extreme distortion, but set lower to allow globe visibility
        float minRadius = viewportDiagonal / static_cast<float>(M_PI * 2.0);  // Quarter of full coverage
        float effectiveSphereRadius = std::max(baseRadius, minRadius);

        m_Renderer->SetFisheyePerspective(true, effectiveSphereRadius, horizonY, horizonScale, width, height);
    }
    else
    {
        m_Renderer->SetVanishingPointPerspective(false, 0.0f, 1.0f, width, height);
    }
}

glm::mat4 Game::GetOrthoProjection(float width, float height)
{
    // Create orthographic projection with origin at top-left, Y increasing downward.
    // This matches screen coordinates where (0,0) is top-left corner.
    return glm::ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
}

void Game::Toggle3DEffect()
{
    m_Enable3DEffect = !m_Enable3DEffect;
    std::cout << "3D Effect: " << (m_Enable3DEffect ? "ON" : "OFF")
              << " (Radius: " << m_GlobeSphereRadius << ")" << std::endl;
}

void Game::Render()
{
    // Render order (back to front):
    // 1. Sky color (clear)
    // 2. Background tilemap layers (ground, ground detail, objects)
    // 3. No-projection background tiles (buildings that stay upright)
    // 4. Y-sorted pass: tiles + NPCs + player interleaved by Y coordinate
    // 5. No-projection foreground tiles
    // 6. No-projection particles (e.g., fireflies in buildings)
    // 7. Foreground tilemap layers (overlay tiles)
    // 8. Regular particles
    // 9. Sky effects (sun rays, stars)
    // 10. UI overlays (editor, debug info, dialogue)

    // Debug draw sleep: pauses after each draw call for visual debugging
    extern bool g_DebugDrawSleep;
    extern void ResetDebugDrawCallIndex();
    if (g_DebugDrawSleep)
    {
        ResetDebugDrawCallIndex();
        std::cout << "===== FRAME START =====" << std::endl;
    }

    m_Renderer->BeginFrame();

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
    float zoomedWidth = worldWidth / m_CameraZoom;
    float zoomedHeight = worldHeight / m_CameraZoom;
    ConfigureRendererPerspective(zoomedWidth, zoomedHeight);
    glm::mat4 projection = GetOrthoProjection(zoomedWidth, zoomedHeight);
    m_Renderer->SetProjection(projection);

    // Snap camera to pixel grid for rendering to avoid per-frame jitter seams (OpenGL only)
    const glm::vec2 originalCamera = m_CameraPosition;
    glm::vec2 renderCam = originalCamera;
    glm::vec2 renderSize(zoomedWidth, zoomedHeight);
    glm::vec2 cullCam = originalCamera; // use unsnapped camera for visibility tests
    glm::vec2 cullSize(zoomedWidth, zoomedHeight);
    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        const float pixelStepX = zoomedWidth / static_cast<float>(m_ScreenWidth);
        const float pixelStepY = zoomedHeight / static_cast<float>(m_ScreenHeight);
        auto snapToPixel = [](float value, float step)
        {
            return (step > 0.0f) ? std::round(value / step) * step : value;
        };
        renderCam.x = snapToPixel(originalCamera.x, pixelStepX);
        renderCam.y = snapToPixel(originalCamera.y, pixelStepY);
    }

    // Calculate cull rectangle for tile visibility testing
    // When 3D effect is enabled, we need to load more tiles because the perspective widens the view.
    // renderCam may be pixel-snapped (OpenGL) for drawing; cullCam/cullSize use the unsnapped
    // camera to keep conservative tile visibility when the snap shifts by sub-pixels.
    if (m_Enable3DEffect)
    {
        // With perspective enabled, the horizon shows more world area than the
        // camera viewport suggests (things shrink toward the horizon). We must
        // expand the culling rectangle to load tiles that would otherwise be
        // culled but become visible due to the perspective warping.
        float horizonScale = 0.6f + (1.0f - m_CameraTilt) * 0.15f;
        float expansion = 1.0f / horizonScale;
        float expandedWidth = zoomedWidth * expansion * 1.5f;
        float expandedHeight = zoomedHeight * expansion;

        // Center the expanded cull rect on the camera position
        float widthDiff = (expandedWidth - zoomedWidth) * 0.5f;
        float heightDiff = (expandedHeight - zoomedHeight) * 0.5f;
        cullCam.x = originalCamera.x - widthDiff;
        cullCam.y = originalCamera.y - heightDiff;
        cullSize = glm::vec2(expandedWidth, expandedHeight);
    }

    // Use snapped camera for rendering when OpenGL (restore at end of function)
    m_CameraPosition = renderCam;

    // Render layers in order with Y-sorted tiles:
    // 1. Background layers (Ground, Ground Detail, Objects, Objects2)
    // 2. Y-sorted pass: Y-sorted tiles from ALL layers + NPCs + player
    // 3. Foreground layers (Foreground, Foreground2, Overlay, Overlay2)

    // Render background layers - Y-sorted and no-projection tiles are skipped
    m_Tilemap.RenderBackgroundLayers(*m_Renderer, renderCam, renderSize, cullCam, cullSize);

    // Suspend perspective for character rendering
    m_Renderer->SuspendPerspective(true);

    // Render no-projection tiles from background layers (buildings & entities that should appear upright)
    m_Tilemap.RenderBackgroundLayersNoProjection(*m_Renderer, renderCam, renderSize, cullCam, cullSize);

    // Collect Y-sorted tiles from all layers
    auto ySortPlusTiles = m_Tilemap.GetVisibleYSortPlusTiles(cullCam, cullSize);

    // Build unified render list for Y-sorted tiles and entities.
    // Items are sorted by Y coordinate so objects lower on screen (higher Y)
    // render on top of objects higher on screen (lower Y), creating depth.
    // Characters are split into top/bottom halves for proper occlusion with tiles.
    struct RenderItem
    {
        // Type ordering matters for stable sort tiebreaker:
        // Higher values render later (in front) when Y coordinates match.
        enum Type
        {
            PLAYER_TOP = 0,    // Player top half (renders first/behind at same Y)
            PLAYER_BOTTOM = 1,
            NPC_TOP = 2,
            NPC_BOTTOM = 3,
            TILE = 4           // Tiles render last/in front at same Y
        } type;
        float sortY;                       // Y coordinate for depth sorting
        Tilemap::YSortPlusTile tile;         // Valid when type == TILE
        const NonPlayerCharacter *npc;     // Valid when type == NPC_*
    };
    // Reuse static vector to avoid allocation every frame
    static std::vector<RenderItem> renderList;
    renderList.clear();
    size_t estimatedSize = ySortPlusTiles.size() + m_NPCs.size() * 2 + 2;
    if (renderList.capacity() < estimatedSize)
        renderList.reserve(estimatedSize);

    // Add Y-sorted tiles (sort by bottom edge of tile)
    // Skip tiles behind the sphere when full globe is visible
    int tileW = m_Tilemap.GetTileWidth();
    int tileH = m_Tilemap.GetTileHeight();
    for (const auto &tile : ySortPlusTiles)
    {
        // Check if tile center is behind the sphere
        float screenX = static_cast<float>(tile.x * tileW) - renderCam.x + tileW * 0.5f;
        float screenY = static_cast<float>(tile.y * tileH) - renderCam.y + tileH * 0.5f;
        if (m_Renderer->IsPointBehindSphere(glm::vec2(screenX, screenY)))
            continue;

        RenderItem item;
        item.type = RenderItem::TILE;
        item.sortY = tile.anchorY;
        item.tile = tile;
        item.npc = nullptr;
        renderList.push_back(item);
    }

    // Add NPCs split into bottom/top halves for proper tile occlusion.
    // The bottom half sorts at the character's anchor (feet) position.
    // The top half sorts slightly higher so it can appear behind tiles
    // that the character is walking past.
    // Skip NPCs behind the sphere when full globe is visible.
    for (const auto &npc : m_NPCs)
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
        renderList.push_back(bottomItem);
        // Top half renders slightly above
        RenderItem topItem;
        topItem.type = RenderItem::NPC_TOP;
        topItem.sortY = anchorY - PlayerCharacter::HALF_HITBOX_HEIGHT;
        topItem.tile = {};
        topItem.npc = &npc;
        renderList.push_back(topItem);
    }

    // Add player.
    // Both halves use anchor position for sorting.
    // Skip player if behind the sphere (edge case when zoomed way out).
    if (!m_Editor.IsActive())
    {
        glm::vec2 playerPos = m_Player.GetPosition();
        float playerScreenX = playerPos.x - renderCam.x;
        float playerScreenY = playerPos.y - renderCam.y;
        if (!m_Renderer->IsPointBehindSphere(glm::vec2(playerScreenX, playerScreenY)))
        {
            float playerAnchorY = playerPos.y; // Bottom-center point
            RenderItem playerBottomItem;
            playerBottomItem.type = RenderItem::PLAYER_BOTTOM;
            playerBottomItem.sortY = playerAnchorY;
            playerBottomItem.tile = {};
            playerBottomItem.npc = nullptr;
            renderList.push_back(playerBottomItem);
            RenderItem playerTopItem;
            playerTopItem.type = RenderItem::PLAYER_TOP;
            playerTopItem.sortY = playerAnchorY;
            playerTopItem.tile = {};
            playerTopItem.npc = nullptr;
            renderList.push_back(playerTopItem);
        }
    }

    // Sort by Y coordinate ascending (lower Y = further from camera = render first).
    // - Normal tiles (Y-sort+1): use epsilon tiebreaker (tile behind, player in front at same Y)
    // - Y-sort-1 tiles: use Y offset so tile renders in front at same Y, no tiebreaker
    // Use stable_sort to maintain consistent ordering for equal elements
    std::stable_sort(renderList.begin(), renderList.end(),
              [](const RenderItem &a, const RenderItem &b)
              {
                  bool aIsYSortMinusTile = (a.type == RenderItem::TILE && a.tile.ySortMinus);
                  bool bIsYSortMinusTile = (b.type == RenderItem::TILE && b.tile.ySortMinus);

                  // If comparing a Y-sort-1 tile with an entity, use offset-based comparison
                  // The offset makes the tile sort as if slightly lower, so it renders in front
                  // when at the same actual Y, but behind when player has walked past
                  bool aIsEntity = (a.type <= RenderItem::NPC_BOTTOM);
                  bool bIsEntity = (b.type <= RenderItem::NPC_BOTTOM);

                  if ((aIsYSortMinusTile && bIsEntity) || (bIsYSortMinusTile && aIsEntity))
                  {
                      // Half-tile offset: player must be at least 8px in front to render in front
                      float aSortY = a.sortY + (aIsYSortMinusTile ? 8.0f : 0.0f);
                      float bSortY = b.sortY + (bIsYSortMinusTile ? 8.0f : 0.0f);
                      // Use epsilon for float comparison to avoid flickering
                      if (std::abs(aSortY - bSortY) > 0.1f)
                          return aSortY < bSortY;
                      // Within epsilon: entity first (behind), tile second (in front)
                      return a.type < b.type;
                  }

                  // Normal comparison with epsilon tiebreaker
                  const float epsilon = 1.0f;
                  if (std::abs(a.sortY - b.sortY) > epsilon)
                      return a.sortY < b.sortY;

                  // Tiebreaker: higher type comes first (renders behind)
                  // TILE(4) before PLAYER(0/1) = TILE renders first = TILE behind, PLAYER in front
                  return a.type > b.type;
              });

    // Render sorted list
    for (const auto &item : renderList)
    {
        switch (item.type)
        {
        case RenderItem::TILE:
            // No-projection tiles render with perspective suspended (upright)
            // Normal tiles render with perspective enabled
            // Pass explicit flag to avoid RenderSingleTile re-reading from layer
            if (item.tile.noProjection)
            {
                // Keep perspective suspended for no-projection tiles
                // RenderSingleTile handles the upright rendering algorithm
                m_Tilemap.RenderSingleTile(*m_Renderer, item.tile.x, item.tile.y,
                                           item.tile.layer, m_CameraPosition, 1);
            }
            else
            {
                // Resume perspective for normal tile rendering
                m_Renderer->SuspendPerspective(false);
                m_Tilemap.RenderSingleTile(*m_Renderer, item.tile.x, item.tile.y,
                                           item.tile.layer, m_CameraPosition, 0);
                // Suspend perspective again for subsequent entities
                m_Renderer->SuspendPerspective(true);
            }
            break;
        case RenderItem::NPC_BOTTOM:
            item.npc->RenderBottomHalf(*m_Renderer, m_CameraPosition);
            break;
        case RenderItem::NPC_TOP:
            item.npc->RenderTopHalf(*m_Renderer, m_CameraPosition);
            break;
        case RenderItem::PLAYER_BOTTOM:
            m_Player.RenderBottomHalf(*m_Renderer, m_CameraPosition);
            break;
        case RenderItem::PLAYER_TOP:
            m_Player.RenderTopHalf(*m_Renderer, m_CameraPosition);
            break;
        }
    }

    // Render no-projection tiles from foreground layers
    m_Tilemap.RenderForegroundLayersNoProjection(*m_Renderer, renderCam, renderSize, cullCam, cullSize);

    // Render noProjection particles, particle system handles suspend internally
    m_Particles.Render(*m_Renderer, m_CameraPosition, true, false);

    // Resume perspective for normal foreground rendering
    // (perspective may still be suspended from Y-sorted loop or RenderForegroundLayersNoProjection
    // if no noProjection structures were processed)
    m_Renderer->SuspendPerspective(false);

    // Render foreground layers, Y-sorted and no-projection tiles are skipped
    m_Tilemap.RenderForegroundLayers(*m_Renderer, renderCam, renderSize, cullCam, cullSize);

    // Render regular particles on top of world
    m_Particles.Render(*m_Renderer, m_CameraPosition, false, false);

    // Render ambient light overlay
    m_Renderer->SuspendPerspective(true);
    glm::mat4 screenProjection = glm::ortho(0.0f, worldWidth, worldHeight, 0.0f);
    m_Renderer->SetProjection(screenProjection);
    m_SkyRenderer.Render(*m_Renderer, m_TimeManager,
                         static_cast<int>(worldWidth), static_cast<int>(worldHeight));
    m_Renderer->SetProjection(projection); // Restore world projection
    m_Renderer->SuspendPerspective(false);

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
        m_Renderer->SuspendPerspective(true);
        RenderNPCHeadText();
        m_Renderer->SuspendPerspective(false);
    }

    // Render branching dialogue tree UI
    if (m_DialogueManager.IsActive())
    {
        m_Renderer->SuspendPerspective(true);
        RenderDialogueTreeBox();
        m_Renderer->SuspendPerspective(false);
    }

    // Render debug info in top left corner (F4 toggle)
    if (m_Editor.IsShowDebugInfo())
    {
        // Set up UI projection
        glm::mat4 uiProjection = glm::ortho(0.0f, static_cast<float>(m_ScreenWidth),
                                            static_cast<float>(m_ScreenHeight), 0.0f, -1.0f, 1.0f);
        m_Renderer->SetProjection(uiProjection);

        // Format FPS text
        char fpsText[32];
        snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", m_CurrentFps);

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
        m_Renderer->DrawText(fpsText, glm::vec2(12.0f, 32.0f + lineHeight * currentLine++), 1.0f, glm::vec3(1.0f, 1.0f, 0.0f), 2.0f, 0.85f);
        m_Renderer->DrawText(posText, glm::vec2(12.0f, 32.0f + lineHeight * currentLine++), 1.0f, glm::vec3(1.0f, 1.0f, 0.0f), 2.0f, 0.85f);
        m_Renderer->DrawText(tileText, glm::vec2(12.0f, 32.0f + lineHeight * currentLine++), 1.0f, glm::vec3(1.0f, 1.0f, 0.0f), 2.0f, 0.85f);

        // Draw active quests section (with spacing and descriptions)
        auto activeQuests = m_GameState.GetActiveQuests();
        if (!activeQuests.empty())
        {
            currentLine += 0.5f; // Add spacing before quests section
            glm::vec3 questGold(1.0f, 0.85f, 0.2f);
            glm::vec3 descColor(0.9f, 0.75f, 0.5f);

            for (const auto &quest : activeQuests)
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
                            displayName[i + 1] = static_cast<char>(std::toupper(displayName[i + 1]));
                        }
                    }
                }
                if (!displayName.empty())
                {
                    displayName[0] = static_cast<char>(std::toupper(displayName[0]));
                }

                // Draw quest title with exclamation mark
                float questTextX = 52.0f; // X position where quest name starts
                glm::vec3 exclamYellow(1.0f, 1.0f, 0.0f);
                m_Renderer->DrawText(">!<", glm::vec2(12.0f, 32.0f + lineHeight * currentLine), 1.0f, exclamYellow, 2.0f, 0.85f);
                m_Renderer->DrawText(displayName, glm::vec2(questTextX, 32.0f + lineHeight * currentLine++), 1.0f, questGold, 2.0f, 0.85f);

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
                    m_Renderer->DrawText(description, glm::vec2(questTextX, 32.0f + lineHeight * currentLine++), 0.8f, descColor, 2.0f, 0.7f);
                }
            }
        }

        // Draw renderer info on right side
        const char *rendererName = (m_RendererAPI == RendererAPI::OpenGL) ? "OpenGL" : "Vulkan";
        float rightMargin = static_cast<float>(m_ScreenWidth) - 12.0f;

        // Renderer name
        char rendererText[32];
        snprintf(rendererText, sizeof(rendererText), "%s", rendererName);
        float textWidth = strnlen(rendererText, sizeof(rendererText)) * 12.0f;
        m_Renderer->DrawText(rendererText, glm::vec2(rightMargin - textWidth, 32.0f), 1.0f, glm::vec3(1.0f, 0.3f, 0.3f), 2.0f, 0.85f);

        // Resolution
        char resText[32];
        snprintf(resText, sizeof(resText), "%dx%d", m_ScreenWidth, m_ScreenHeight);
        textWidth = strnlen(resText, sizeof(resText)) * 12.0f;
        m_Renderer->DrawText(resText, glm::vec2(rightMargin - textWidth, 32.0f + lineHeight), 1.0f, glm::vec3(1.0f, 0.3f, 0.3f), 2.0f, 0.85f);

        // Frame time
        char frameTimeText[32];
        float frameTimeMs = (m_CurrentFps > 0) ? (1000.0f / m_CurrentFps) : 0.0f;
        snprintf(frameTimeText, sizeof(frameTimeText), "%.2fms", frameTimeMs);
        textWidth = strnlen(frameTimeText, sizeof(frameTimeText)) * 12.0f;
        m_Renderer->DrawText(frameTimeText, glm::vec2(rightMargin - textWidth, 32.0f + lineHeight * 2), 1.0f, glm::vec3(1.0f, 0.3f, 0.3f), 2.0f, 0.85f);

        // Zoom level
        char zoomText[32];
        snprintf(zoomText, sizeof(zoomText), "Zoom: %.1fx", m_CameraZoom);
        textWidth = strnlen(zoomText, sizeof(zoomText)) * 12.0f;
        m_Renderer->DrawText(zoomText, glm::vec2(rightMargin - textWidth, 32.0f + lineHeight * 3), 1.0f, glm::vec3(1.0f, 0.3f, 0.3f), 2.0f, 0.85f);

        // Draw calls (averaged over last second)
        char drawCallText[32];
        snprintf(drawCallText, sizeof(drawCallText), "Draws: %d", m_CurrentDrawCalls);
        textWidth = m_Renderer->GetTextWidth(drawCallText, 1.0f);
        m_Renderer->DrawText(drawCallText, glm::vec2(rightMargin - textWidth, 32.0f + lineHeight * 4), 1.0f, glm::vec3(1.0f, 0.3f, 0.3f), 2.0f, 0.85f);

        // Restore world projection (in case EndFrame flushes any batches)
        m_Renderer->SetProjection(projection);
    }

    // Render no-projection anchors on top of everything
    if (m_Editor.IsShowNoProjectionAnchors())
    {
        m_Renderer->SuspendPerspective(true);
        m_Editor.RenderNoProjectionAnchors(MakeEditorContext());
        m_Renderer->SuspendPerspective(false);
    }

    m_Renderer->EndFrame();

    // Restore unsnapped camera for game state updates
    m_CameraPosition = originalCamera;

    // Accumulate draw calls for averaging (calculated in Update())
    m_DrawCallAccumulator += m_Renderer->GetDrawCallCount();

    // Swap buffers
    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        // DEBUG: Print frame end marker
        extern bool g_DebugDrawSleep;
        if (g_DebugDrawSleep)
        {
            std::cout << "===== FRAME END =====" << std::endl;
        }
        glfwSwapBuffers(m_Window);
    }
    // Vulkan handles its own presentation in EndFrame()
}

void Game::Shutdown()
{
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
    glfwTerminate();
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
        std::cout << "Already using " << (api == RendererAPI::OpenGL ? "OpenGL" : "Vulkan") << std::endl;
        return true;
    }

    if (!IsRendererAvailable(api))
    {
        std::cerr << "Renderer API not available: " << (api == RendererAPI::OpenGL ? "OpenGL" : "Vulkan") << std::endl;
        return false;
    }

    std::cout << "Switching renderer from "
              << (m_RendererAPI == RendererAPI::OpenGL ? "OpenGL" : "Vulkan")
              << " to "
              << (api == RendererAPI::OpenGL ? "OpenGL" : "Vulkan")
              << "..." << std::endl;

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

    // Update renderer API
    m_RendererAPI = api;

    // Reset and set window hints for new API
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
    m_Window = glfwCreateWindow(m_ScreenWidth, m_ScreenHeight, "rift", nullptr, nullptr);
    if (!m_Window)
    {
        std::cerr << "Failed to create GLFW window during renderer switch" << std::endl;
        return false;
    }
    glfwSetWindowPos(m_Window, windowX, windowY);

    // Restore window callbacks
    glfwSetWindowUserPointer(m_Window, this);
    glfwSetScrollCallback(m_Window, ScrollCallback);
    glfwSetFramebufferSizeCallback(m_Window, FramebufferSizeCallback);
    glfwSetWindowRefreshCallback(m_Window, WindowRefreshCallback);

    // Create new renderer
    m_Renderer.reset(CreateRenderer(m_RendererAPI, m_Window));
    if (!m_Renderer)
    {
        std::cerr << "Failed to create renderer during switch" << std::endl;
        return false;
    }

    // Initialize OpenGL-specific stuff
    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        // Bind this window's OpenGL context to the current thread.
        glfwMakeContextCurrent(m_Window);
        // Load OpenGL function pointers via GLAD.
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cerr << "Failed to initialize GLAD during renderer switch" << std::endl;
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
    m_Renderer->Init();

    // Re-apply no-vsync after renderer initialization.
    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        glfwSwapInterval(0);
    }

    // Set viewport and projection
    m_Renderer->SetViewport(0, 0, m_ScreenWidth, m_ScreenHeight);
    float worldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth()) / m_CameraZoom;
    float worldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight()) / m_CameraZoom;
    ConfigureRendererPerspective(worldWidth, worldHeight);
    glm::mat4 projection = GetOrthoProjection(worldWidth, worldHeight);
    m_Renderer->SetProjection(projection);

    // Re-upload textures to new renderer
    m_Renderer->UploadTexture(m_Tilemap.GetTilesetTexture());
    m_Player.UploadTextures(*m_Renderer);
    for (auto &npc : m_NPCs)
    {
        npc.UploadTextures(*m_Renderer);
    }
    m_Particles.UploadTextures(*m_Renderer);
    m_SkyRenderer.UploadTextures(*m_Renderer);

    std::cout << "Renderer switch complete! Now using "
              << (m_RendererAPI == RendererAPI::OpenGL ? "OpenGL" : "Vulkan")
              << std::endl;

    return true;
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
    int snappedWidth = std::max(5 * tileScreenSize, (m_ScreenWidth / tileScreenSize) * tileScreenSize);
    int snappedHeight = std::max(4 * tileScreenSize, (m_ScreenHeight / tileScreenSize) * tileScreenSize);

    // Only resize if not already snapped
    if (snappedWidth != m_ScreenWidth || snappedHeight != m_ScreenHeight)
    {
        glfwSetWindowSize(m_Window, snappedWidth, snappedHeight);
        std::cout << "Window snapped to " << snappedWidth << "x" << snappedHeight
                  << " (" << (snappedWidth / tileScreenSize) << "x" << (snappedHeight / tileScreenSize) << " tiles)" << std::endl;
    }

    m_PendingWindowSnap = false;
}

void Game::FramebufferSizeCallback(GLFWwindow *window, int width, int height)
{
    // Get "this" back from the window's user data
    Game *game = static_cast<Game *>(glfwGetWindowUserPointer(window));
    if (game)
    {
        // Now we can call member functions
        game->OnFramebufferResized(width, height);
    }
}

void Game::WindowRefreshCallback(GLFWwindow *window)
{
    // Called by the OS when the window needs repainting (e.g. during resize drag).
    // Re-renders the scene so the user sees game content instead of white fill.
    Game *game = static_cast<Game *>(glfwGetWindowUserPointer(window));
    if (game)
    {
        game->Render();
    }
}

EditorContext Game::MakeEditorContext()
{
    return EditorContext{
        m_Window,
        m_ScreenWidth,
        m_ScreenHeight,
        m_TilesVisibleWidth,
        m_TilesVisibleHeight,
        m_CameraPosition,
        m_CameraFollowTarget,
        m_HasCameraFollowTarget,
        m_CameraZoom,
        m_FreeCameraMode,
        m_Enable3DEffect,
        m_CameraTilt,
        m_GlobeSphereRadius,
        m_Tilemap,
        m_Player,
        m_NPCs,
        *m_Renderer,
        m_Particles
    };
}
