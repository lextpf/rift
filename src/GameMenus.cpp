#ifdef _WIN32
#define NOMINMAX
#endif

#include "Game.h"

#include "AmbienceConfig.h"
#include "Logger.h"
#include "ParticleSystem.h"
#include "PostFXParams.h"
#include "Version.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <ranges>

#ifdef _WIN32
#include <Windows.h>
#undef DrawText
#endif

// =============================================================================
// File-local constants and helpers
// =============================================================================

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Game";

// --- Colors -----------------------------------------------------------------
constexpr glm::vec3 TITLE_TEXT_COLOR{1.0f, 1.0f, 1.0f};
constexpr glm::vec3 TITLE_DIM_COLOR{0.55f, 0.55f, 0.62f};
constexpr glm::vec3 TITLE_DISABLED_COLOR{0.30f, 0.30f, 0.34f};
constexpr glm::vec3 TITLE_HIGHLIGHT_COLOR{1.00f, 0.84f, 0.40f};
constexpr glm::vec4 TITLE_BG_COLOR{0.04f, 0.05f, 0.09f, 1.0f};
constexpr glm::vec4 PAUSE_DIM_COLOR{0.0f, 0.0f, 0.0f, 0.55f};
constexpr glm::vec4 MODAL_BACKDROP_COLOR{0.0f, 0.0f, 0.0f, 0.65f};
constexpr glm::vec4 MODAL_BOX_COLOR{0.10f, 0.11f, 0.16f, 0.95f};

// --- Layout (in screen-space pixels) ----------------------------------------
// Title logo uses the renderer's *headline* atlas (rasterized at ~96 px in
// OpenGLRenderer; falls back to scaled body atlas in Vulkan). At scale 1.0
// the logo renders at the headline atlas's native size, so it stays crisp
// instead of blurring like an upscaled body-atlas glyph.
constexpr float TITLE_LOGO_SCALE = 1.0f;
// Outline scaled to match the menu items' outline-to-glyph ratio. Outline
// offset is 2 * scale * outlineSize px; on a 96-px headline glyph that puts
// the stroke at ~12.5 % of glyph height (between unselected and selected
// menu items at scale 1.4 with outline 1.0 / 2.0).
constexpr float TITLE_LOGO_OUTLINE = 6.0f;
constexpr float MENU_ITEM_SCALE = 1.4f;
constexpr float MENU_LINE_HEIGHT = 48.0f;
constexpr float VERSION_TEXT_SCALE = 0.7f;
constexpr float PAUSE_HEADER_SCALE = 1.8f;
constexpr float MODAL_TEXT_SCALE = 1.0f;

// Outline thickness for body-atlas (24-px native) menu / button text.
// Outline-to-glyph ratio ~21 % (heavier than the title's 12.5 %) so the
// menu strokes read clearly against the busy grass-and-fireflies backdrop.
// Selection only changes color, never outline weight.
constexpr float MENU_ITEM_OUTLINE = 2.5f;

// --- Title-screen world ----------------------------------------------------
// The title screen renders a small grass-only tilemap with firefly particles,
// frozen at night. Tweak these to change the title-screen aesthetic.
constexpr int TITLE_WORLD_MAP_WIDTH = 32;
constexpr int TITLE_WORLD_MAP_HEIGHT = 24;
constexpr float TITLE_WORLD_TIME_OF_DAY = 23.0f;  // 23:00 = deep night.
// Tile ID painted across layer 0. The base overworld tileset's first
// non-empty entry is grass; adjust here if your tileset orders things
// differently.
constexpr int TITLE_WORLD_GRASS_TILE_ID = 1;

// --- Title menu items -------------------------------------------------------
enum TitleItem : int
{
    TITLE_NEW_GAME = 0,
    TITLE_CONTINUE = 1,
    TITLE_SETTINGS = 2,
    TITLE_QUIT = 3,
    TITLE_ITEM_COUNT = 4
};

constexpr const char* TITLE_LABELS[TITLE_ITEM_COUNT] = {
    "New Game",
    "Continue",
    "Settings",
    "Quit",
};

// --- Pause menu items -------------------------------------------------------
enum PauseItem : int
{
    PAUSE_RESUME = 0,
    PAUSE_QUIT_TO_TITLE = 1,
    PAUSE_ITEM_COUNT = 2
};

constexpr const char* PAUSE_LABELS[PAUSE_ITEM_COUNT] = {
    "Resume",
    "Quit to Title",
};

// --- Helpers ----------------------------------------------------------------

/// Build an orthographic projection mapping (0,0)=top-left to
/// (screenWidth, screenHeight)=bottom-right, suitable for screen-space UI.
glm::mat4 MakeUIProjection(int screenWidth, int screenHeight)
{
    return glm::ortho(
        0.0f, static_cast<float>(screenWidth), static_cast<float>(screenHeight), 0.0f, -1.0f, 1.0f);
}

/// Pick the text color for a menu item given its state.
glm::vec3 MenuItemColor(bool selected, bool enabled)
{
    if (!enabled)
    {
        return TITLE_DISABLED_COLOR;
    }
    return selected ? TITLE_HIGHLIGHT_COLOR : TITLE_DIM_COLOR;
}

}  // namespace

// =============================================================================
// Save existence query
// =============================================================================

bool Game::CheckSaveExists() const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(m_SaveMapPath, ec) && fs::is_regular_file(m_SaveMapPath, ec);
}

// =============================================================================
// Load / reset world
// =============================================================================

void Game::LoadGameWorld(bool loadSave)
{
    int loadedPlayerTileX = -1;
    int loadedPlayerTileY = -1;
    int loadedCharacterType = -1;
    bool mapLoaded = false;

    if (loadSave)
    {
        mapLoaded = m_Tilemap.LoadMapFromJSON(
            m_SaveMapPath, &m_NPCs, &loadedPlayerTileX, &loadedPlayerTileY, &loadedCharacterType);
    }

    if (!mapLoaded)
    {
        Logger::InfoF(LOG_SUBSYSTEM,
                      "{}",
                      loadSave ? "No existing save found, generating default map"
                               : "New Game: regenerating default map");
        m_NPCs.clear();
        m_Tilemap.SetTilemapSize(m_DefaultMapWidth, m_DefaultMapHeight);
    }

    // Vulkan needs the tileset re-uploaded after a map change. OpenGL uploads
    // textures lazily on first use and does not need this.
    if (m_RendererAPI == RendererAPI::Vulkan && m_Renderer)
    {
        m_Renderer->UploadTexture(m_Tilemap.GetTilesetTexture());
    }

    // Pick the player character: saved value first, then the first manifest
    // entry, falling back to a hard default if the manifest is empty.
    CharacterType initialCharacter =
        (loadedCharacterType >= 0 &&
         loadedCharacterType < static_cast<int>(EnumTraits<CharacterType>::Count))
            ? static_cast<CharacterType>(loadedCharacterType)
            : (m_ConfiguredCharacters.empty() ? CharacterType::BW1_MALE
                                              : m_ConfiguredCharacters.front());
    if (!m_ConfiguredCharacters.empty() &&
        std::ranges::find(m_ConfiguredCharacters, initialCharacter) == m_ConfiguredCharacters.end())
    {
        initialCharacter = m_ConfiguredCharacters.front();
    }
    if (!m_Player.SwitchCharacter(initialCharacter))
    {
        Logger::Error(LOG_SUBSYSTEM, "Failed to switch player character in LoadGameWorld");
    }

    // Place player and re-anchor the camera.
    int playerTileX = (loadedPlayerTileX >= 0) ? loadedPlayerTileX : 9;
    int playerTileY = (loadedPlayerTileY >= 0) ? loadedPlayerTileY : 5;
    m_Player.SetTilePosition(playerTileX, playerTileY);

    float camWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float camWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    glm::vec2 playerPos = m_Player.GetPosition();
    glm::vec2 playerVisualCenter =
        glm::vec2(playerPos.x, playerPos.y - PlayerCharacter::HITBOX_HEIGHT);
    m_Camera.Initialize(playerVisualCenter, camWorldWidth, camWorldHeight);

    // Particle zones are tied to the live tilemap; refresh after a (re)load.
    m_Particles.SetZones(m_Tilemap.GetParticleZones());
    m_Particles.SetTilemap(&m_Tilemap);
}

void Game::LoadTitleScreenWorld()
{
    // Wipe any session state. The title screen is purely cosmetic - no save,
    // no NPCs, no player, no editor - so we strip everything to a clean slate
    // before painting the grass.
    m_NPCs.clear();
    m_Editor.SetActive(false);
    m_DialogueManager.EndDialogue();
    m_InDialogue = false;
    m_DialogueText.clear();
    m_DialogueNPCIndex = -1;
    m_DialoguePage = 0;
    m_DialogueCharReveal = -1.0f;
    m_DialogueBoxFadeTimer = 0.0f;
    m_DialogueSnap.active = false;

    // Allocate a small map sized for the visible viewport, no random
    // generation (we paint every tile by hand below).
    m_Tilemap.SetTilemapSize(TITLE_WORLD_MAP_WIDTH, TITLE_WORLD_MAP_HEIGHT, /*generateMap=*/false);

    // Paint layer 0 (Ground) with the grass tile. All other layers stay
    // empty (default tile id -1) so the scene reads as a flat field.
    for (int y = 0; y < TITLE_WORLD_MAP_HEIGHT; ++y)
    {
        for (int x = 0; x < TITLE_WORLD_MAP_WIDTH; ++x)
        {
            m_Tilemap.SetLayerTile(x, y, /*layer=*/0, TITLE_WORLD_GRASS_TILE_ID);
        }
    }

    // Replace the particle zones with one zone per atmospheric particle
    // type, each spanning the whole map. The ParticleSystem caps each zone's
    // active count (configured in Initialize), so total density stays bounded.
    // Excluded: Lantern (needs explicit lit-zone placement near a light source).
    if (auto* zones = m_Tilemap.GetParticleZonesMutable())
    {
        zones->clear();

        const glm::vec2 zonePos(0.0f, 0.0f);
        const glm::vec2 zoneSize(TITLE_WORLD_MAP_WIDTH * m_Tilemap.GetTileWidth(),
                                 TITLE_WORLD_MAP_HEIGHT * m_Tilemap.GetTileHeight());

        constexpr ParticleType TITLE_PARTICLE_TYPES[] = {
            ParticleType::Firefly,
            ParticleType::Rain,
            ParticleType::Snow,
            ParticleType::Fog,
            ParticleType::Sparkles,
            ParticleType::Wisp,
            ParticleType::Sunshine,
            ParticleType::DriftingLeaf,
            ParticleType::DustMote,
            ParticleType::Pollen,
        };

        for (ParticleType type : TITLE_PARTICLE_TYPES)
        {
            ParticleZone zone;
            zone.position = zonePos;
            zone.size = zoneSize;
            zone.type = type;
            zone.enabled = true;
            zone.noProjection = false;
            zones->push_back(zone);
        }
    }

    // Vulkan needs the tileset re-uploaded after a tilemap recreate.
    if (m_RendererAPI == RendererAPI::Vulkan && m_Renderer)
    {
        m_Renderer->UploadTexture(m_Tilemap.GetTilesetTexture());
    }

    // Stick the player at (0, 0) just to keep its tile coordinates valid;
    // it isn't rendered in Title mode (Y-sort assembly skips it). Camera
    // sits at the map center so the menu has a clean grass backdrop.
    m_Player.SetTilePosition(0, 0);
    glm::vec2 mapCenterPx(TITLE_WORLD_MAP_WIDTH * 0.5f * m_Tilemap.GetTileWidth(),
                          TITLE_WORLD_MAP_HEIGHT * 0.5f * m_Tilemap.GetTileHeight());
    float camWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float camWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    m_Camera.Initialize(mapCenterPx, camWorldWidth, camWorldHeight);

    // Freeze time at night. m_TimeManager.Update is gated in Title mode so
    // this value holds until the user picks Continue / New Game.
    m_TimeManager.Initialize();
    m_TimeManager.SetTime(TITLE_WORLD_TIME_OF_DAY);

    // Refresh the particle system's zone pointer (zones vector reference is
    // stored across the system) and tilemap pointer.
    m_Particles.SetZones(m_Tilemap.GetParticleZones());
    m_Particles.SetTilemap(&m_Tilemap);
}

void Game::ResetWorldToDefaults()
{
    LoadGameWorld(/*loadSave=*/false);
    m_TimeManager.Initialize();
    m_GameState.Clear();

    // Close any leftover dialogue/snap state from the prior session so the
    // next gameplay frame starts clean.
    m_DialogueManager.EndDialogue();
    m_InDialogue = false;
    m_DialogueText.clear();
    m_DialogueNPCIndex = -1;
    m_DialoguePage = 0;
    m_DialogueCharReveal = -1.0f;
    m_DialogueBoxFadeTimer = 0.0f;
    m_DialogueSnap.active = false;
}

// =============================================================================
// Title menu state (re)build
// =============================================================================

void Game::RebuildTitleMenu()
{
    const bool hasSave = CheckSaveExists();

    m_TitleMenu.enabled.assign(TITLE_ITEM_COUNT, true);
    m_TitleMenu.enabled[TITLE_CONTINUE] = hasSave;
    m_TitleMenu.enabled[TITLE_SETTINGS] = false;  // stub in this MVP

    // If a save exists, default-highlight Continue (the user's most likely
    // intent on a returning launch); otherwise fall back to the first
    // enabled item (New Game).
    m_TitleMenu.selected =
        hasSave ? static_cast<int>(TITLE_CONTINUE) : MenuLogic::FirstEnabledIndex(m_TitleMenu);

    m_ConfirmOverwriteShown = false;
    m_ConfirmPrompt.selected = MenuLogic::ConfirmChoice::Cancel;
}

// =============================================================================
// Title input dispatch
// =============================================================================

void Game::ProcessTitleInput()
{
    // Lazy first-time init for the menu so we don't have to reach into
    // Initialize. Also cheap to call repeatedly because the size never grows.
    if (m_TitleMenu.enabled.size() != static_cast<size_t>(TITLE_ITEM_COUNT))
    {
        RebuildTitleMenu();
    }

    // Drive all KeyToggles unconditionally to advance their edge state and
    // avoid spurious carry-over into other modes.
    bool up = m_KeyMenuUp.JustPressed(m_Window);
    bool down = m_KeyMenuDown.JustPressed(m_Window);
    bool left = m_KeyMenuLeft.JustPressed(m_Window);
    bool right = m_KeyMenuRight.JustPressed(m_Window);
    bool confirm = m_KeyMenuConfirm.JustPressed(m_Window);
    bool esc = m_KeyEscape.JustPressed(m_Window);

    // Confirm-overwrite modal owns input while shown.
    if (m_ConfirmOverwriteShown)
    {
        if (esc)
        {
            m_ConfirmOverwriteShown = false;
            return;
        }
        if (left)
        {
            MenuLogic::ConfirmLeft(m_ConfirmPrompt);
        }
        if (right)
        {
            MenuLogic::ConfirmRight(m_ConfirmPrompt);
        }
        if (confirm)
        {
            const bool proceed = (m_ConfirmPrompt.selected == MenuLogic::ConfirmChoice::Confirm);
            m_ConfirmOverwriteShown = false;
            if (proceed)
            {
                Logger::Info(LOG_SUBSYSTEM, "New Game (overwrite confirmed)");
                ResetWorldToDefaults();
                m_GameMode = GameMode::Playing;
            }
        }
        return;
    }

    // Top-level title menu navigation.
    if (up)
    {
        MenuLogic::NavigateUp(m_TitleMenu);
    }
    if (down)
    {
        MenuLogic::NavigateDown(m_TitleMenu);
    }
    if (esc)
    {
        // Esc on the title is a no-op: the user must explicitly select Quit.
    }
    if (!confirm)
    {
        return;
    }

    switch (m_TitleMenu.selected)
    {
        case TITLE_NEW_GAME:
        {
            if (CheckSaveExists())
            {
                m_ConfirmOverwriteShown = true;
                m_ConfirmPrompt.selected = MenuLogic::ConfirmChoice::Cancel;
            }
            else
            {
                Logger::Info(LOG_SUBSYSTEM, "New Game (no existing save)");
                ResetWorldToDefaults();
                m_GameMode = GameMode::Playing;
            }
            break;
        }
        case TITLE_CONTINUE:
        {
            if (!CheckSaveExists())
            {
                break;  // shouldn't be reachable (item is disabled)
            }
            Logger::Info(LOG_SUBSYSTEM, "Continue: reloading save from disk");
            // Reset time so Continue doesn't inherit the title screen's
            // 23:00 night setting; LoadGameWorld doesn't touch TimeManager.
            m_TimeManager.Initialize();
            LoadGameWorld(/*loadSave=*/true);
            m_GameMode = GameMode::Playing;
            break;
        }
        case TITLE_SETTINGS:
        {
            // Stub: greyed out, NavigateDown skips it. Reaching here would mean
            // the user activated a disabled item, which our nav prevents.
            break;
        }
        case TITLE_QUIT:
        {
            Logger::Info(LOG_SUBSYSTEM, "Quit from title");
            glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
            break;
        }
        default:
            break;
    }
}

// =============================================================================
// Pause input dispatch
// =============================================================================

void Game::ProcessPauseInput()
{
    if (m_PauseMenu.enabled.size() != static_cast<size_t>(PAUSE_ITEM_COUNT))
    {
        m_PauseMenu.enabled.assign(PAUSE_ITEM_COUNT, true);
        m_PauseMenu.selected = 0;
    }

    bool up = m_KeyMenuUp.JustPressed(m_Window);
    bool down = m_KeyMenuDown.JustPressed(m_Window);
    bool confirm = m_KeyMenuConfirm.JustPressed(m_Window);
    bool esc = m_KeyEscape.JustPressed(m_Window);

    if (esc)
    {
        m_GameMode = GameMode::Playing;
        return;
    }

    if (up)
    {
        MenuLogic::NavigateUp(m_PauseMenu);
    }
    if (down)
    {
        MenuLogic::NavigateDown(m_PauseMenu);
    }

    if (!confirm)
    {
        return;
    }

    switch (m_PauseMenu.selected)
    {
        case PAUSE_RESUME:
        {
            m_GameMode = GameMode::Playing;
            break;
        }
        case PAUSE_QUIT_TO_TITLE:
        {
            Logger::Info(LOG_SUBSYSTEM, "Quit to Title (no save)");
            // Restore the cosmetic title-screen world so the menu sits over
            // grass and fireflies again instead of the abandoned session.
            LoadTitleScreenWorld();
            m_GameMode = GameMode::Title;
            RebuildTitleMenu();
            break;
        }
        default:
            break;
    }
}

// =============================================================================
// Title rendering
// =============================================================================

void Game::RenderTitleFrame()
{
    if (m_TitleMenu.enabled.size() != static_cast<size_t>(TITLE_ITEM_COUNT))
    {
        RebuildTitleMenu();
    }

    m_Renderer->BeginFrame();
    m_Renderer->BeginScene();

    // Clear with the title world's sky color (deep blue at night).
    glm::vec3 skyColor = m_TimeManager.GetSkyColor();
    m_Renderer->Clear(skyColor.r, skyColor.g, skyColor.b, 1.0f);
    m_Renderer->SetAmbientColor(m_TimeManager.GetAmbientColor());

    // Render the title-world tilemap (grass on layer 0, other layers empty).
    // We skip the Y-sort assembly entirely since the title world has no
    // entities. Foreground layers are rendered too so any future tweaks to
    // TITLE_WORLD_GRASS_TILE_ID with an upper-layer overlay still show.
    const float worldWidth = static_cast<float>(m_ScreenWidth) / static_cast<float>(PIXEL_SCALE);
    const float worldHeight = static_cast<float>(m_ScreenHeight) / static_cast<float>(PIXEL_SCALE);
    const float zoomedWidth = worldWidth / m_Camera.GetState().zoom;
    const float zoomedHeight = worldHeight / m_Camera.GetState().zoom;
    m_Camera.ConfigurePerspective(*m_Renderer, zoomedWidth, zoomedHeight);
    glm::mat4 projection = CameraController::GetOrthoProjection(zoomedWidth, zoomedHeight);
    m_Renderer->SetProjection(projection);

    const glm::vec2 renderCam = m_Camera.GetState().position;
    const glm::vec2 renderSize(zoomedWidth, zoomedHeight);
    m_Tilemap.RenderBackgroundLayers(*m_Renderer, renderCam, renderSize, renderCam, renderSize);
    m_Tilemap.RenderForegroundLayers(*m_Renderer, renderCam, renderSize, renderCam, renderSize);

    // Fireflies + ambient particles, drawn on top of the world.
    m_Particles.Render(*m_Renderer, renderCam, /*noProjection=*/false, /*additive=*/false);

    // Atmospheric sky overlay: stars at night, moon, dawn glow, etc. Drawn
    // in a screen-space orthographic projection (mirrors the gameplay path).
    m_Renderer->SuspendPerspective(true);
    glm::mat4 screenProjection = glm::ortho(0.0f, worldWidth, worldHeight, 0.0f);
    m_Renderer->SetProjection(screenProjection);
    m_SkyRenderer.Render(
        *m_Renderer, m_TimeManager, static_cast<int>(worldWidth), static_cast<int>(worldHeight));
    m_Renderer->SetProjection(projection);
    m_Renderer->SuspendPerspective(false);

    // Composite the scene through PostFX. Keep bloom modest so fireflies
    // glow without blowing the whole screen out.
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
        // gradingParams default-constructs to identity. The shader-side gate
        // (postFXEnabled=false) is the real off-switch; these zeroes are a
        // defensive fallback in case the uniform fails to bind.
    }
    m_Renderer->EndSceneApplyPostFX(postFX);

    // UI overlays draw straight to swapchain after PostFX.
    RenderTitleContent();
    if (m_ConfirmOverwriteShown)
    {
        RenderConfirmOverwritePrompt();
    }

    m_Console.Render(*m_Renderer, m_ScreenWidth, m_ScreenHeight);

    m_Renderer->EndFrame();

    if (m_RendererAPI == RendererAPI::OpenGL)
    {
        glfwSwapBuffers(m_Window);
    }

    m_Fps.drawCallAccumulator += m_Renderer->GetDrawCallCount();
}

void Game::RenderTitleContent()
{
    IRenderer::PerspectiveSuspendGuard guard(*m_Renderer);

    // Switch to a screen-space pixel projection for UI text.
    glm::mat4 uiProjection = MakeUIProjection(m_ScreenWidth, m_ScreenHeight);
    m_Renderer->SetProjection(uiProjection);

    const float screenW = static_cast<float>(m_ScreenWidth);
    const float screenH = static_cast<float>(m_ScreenHeight);

    // --- "RIFT" logo --------------------------------------------------------
    // Drawn from the renderer's headline atlas (~96 px native) so the logo
    // is sampled near 1:1 instead of upscaled from the 24-px body atlas.
    const std::string logoText = "RIFT";
    const float logoWidth = m_Renderer->GetTextWidthLarge(logoText, TITLE_LOGO_SCALE);
    const float logoX = std::floor((screenW - logoWidth) * 0.5f);
    const float logoY = std::floor(screenH * 0.22f);
    m_Renderer->DrawTextLarge(logoText,
                              glm::vec2(logoX, logoY),
                              TITLE_LOGO_SCALE,
                              TITLE_TEXT_COLOR,
                              TITLE_LOGO_OUTLINE,
                              1.0f);

    // --- Menu items ---------------------------------------------------------
    const float menuTopY = std::floor(screenH * 0.50f);
    for (int i = 0; i < TITLE_ITEM_COUNT; ++i)
    {
        const bool enabled = m_TitleMenu.enabled[i];
        const bool selected = (m_TitleMenu.selected == i);
        const std::string& label = TITLE_LABELS[i];
        const std::string display =
            selected ? std::string("> ") + label : std::string("  ") + label;

        const float w = m_Renderer->GetTextWidth(display, MENU_ITEM_SCALE);
        const float x = std::floor((screenW - w) * 0.5f);
        const float y = menuTopY + i * MENU_LINE_HEIGHT;
        m_Renderer->DrawText(display,
                             glm::vec2(x, y),
                             MENU_ITEM_SCALE,
                             MenuItemColor(selected, enabled),
                             MENU_ITEM_OUTLINE,
                             1.0f);
    }

    // --- Version footer -----------------------------------------------------
    const std::string versionText = std::string("rift ") + RIFT_VERSION;
    const float versionWidth = m_Renderer->GetTextWidth(versionText, VERSION_TEXT_SCALE);
    m_Renderer->DrawText(versionText,
                         glm::vec2(screenW - versionWidth - 16.0f, screenH - 28.0f),
                         VERSION_TEXT_SCALE,
                         TITLE_DISABLED_COLOR,
                         1.0f,
                         0.85f);
}

void Game::RenderConfirmOverwritePrompt()
{
    IRenderer::PerspectiveSuspendGuard guard(*m_Renderer);
    glm::mat4 uiProjection = MakeUIProjection(m_ScreenWidth, m_ScreenHeight);
    m_Renderer->SetProjection(uiProjection);

    const float screenW = static_cast<float>(m_ScreenWidth);
    const float screenH = static_cast<float>(m_ScreenHeight);

    // Semi-opaque backdrop covers the title.
    m_Renderer->DrawColoredRect(
        glm::vec2(0.0f, 0.0f), glm::vec2(screenW, screenH), MODAL_BACKDROP_COLOR);

    // Modal box sized as a fraction of screen.
    const float boxW = std::floor(screenW * 0.55f);
    const float boxH = std::floor(screenH * 0.30f);
    const float boxX = std::floor((screenW - boxW) * 0.5f);
    const float boxY = std::floor((screenH - boxH) * 0.5f);
    m_Renderer->DrawColoredRect(glm::vec2(boxX, boxY), glm::vec2(boxW, boxH), MODAL_BOX_COLOR);

    // Two-line message.
    const std::string line1 = "Starting a new game will overwrite";
    const std::string line2 = "your existing save. Continue?";
    const float l1w = m_Renderer->GetTextWidth(line1, MODAL_TEXT_SCALE);
    const float l2w = m_Renderer->GetTextWidth(line2, MODAL_TEXT_SCALE);
    const float lineH = 32.0f;
    const float textY = boxY + boxH * 0.30f;
    m_Renderer->DrawText(line1,
                         glm::vec2(std::floor(boxX + (boxW - l1w) * 0.5f), textY),
                         MODAL_TEXT_SCALE,
                         TITLE_TEXT_COLOR,
                         1.0f,
                         1.0f);
    m_Renderer->DrawText(line2,
                         glm::vec2(std::floor(boxX + (boxW - l2w) * 0.5f), textY + lineH),
                         MODAL_TEXT_SCALE,
                         TITLE_TEXT_COLOR,
                         1.0f,
                         1.0f);

    // Two buttons side by side: Cancel | New Game.
    const std::string cancelLabel = "Cancel";
    const std::string confirmLabel = "New Game";
    const bool confirmSelected = (m_ConfirmPrompt.selected == MenuLogic::ConfirmChoice::Confirm);

    const std::string cancelDisplay =
        !confirmSelected ? std::string("> ") + cancelLabel : std::string("  ") + cancelLabel;
    const std::string confirmDisplay =
        confirmSelected ? std::string("> ") + confirmLabel : std::string("  ") + confirmLabel;

    const float buttonScale = 1.1f;
    const float cancelW = m_Renderer->GetTextWidth(cancelDisplay, buttonScale);
    const float confirmW = m_Renderer->GetTextWidth(confirmDisplay, buttonScale);
    const float buttonY = boxY + boxH * 0.72f;
    const float cancelX = std::floor(boxX + boxW * 0.30f - cancelW * 0.5f);
    const float confirmX = std::floor(boxX + boxW * 0.70f - confirmW * 0.5f);

    m_Renderer->DrawText(cancelDisplay,
                         glm::vec2(cancelX, buttonY),
                         buttonScale,
                         confirmSelected ? TITLE_DIM_COLOR : TITLE_HIGHLIGHT_COLOR,
                         MENU_ITEM_OUTLINE,
                         1.0f);
    m_Renderer->DrawText(confirmDisplay,
                         glm::vec2(confirmX, buttonY),
                         buttonScale,
                         confirmSelected ? TITLE_HIGHLIGHT_COLOR : TITLE_DIM_COLOR,
                         MENU_ITEM_OUTLINE,
                         1.0f);
}

// =============================================================================
// Pause overlay rendering
// =============================================================================

void Game::RenderPauseOverlay()
{
    if (m_PauseMenu.enabled.size() != static_cast<size_t>(PAUSE_ITEM_COUNT))
    {
        m_PauseMenu.enabled.assign(PAUSE_ITEM_COUNT, true);
        m_PauseMenu.selected = 0;
    }

    IRenderer::PerspectiveSuspendGuard guard(*m_Renderer);
    glm::mat4 uiProjection = MakeUIProjection(m_ScreenWidth, m_ScreenHeight);
    m_Renderer->SetProjection(uiProjection);

    const float screenW = static_cast<float>(m_ScreenWidth);
    const float screenH = static_cast<float>(m_ScreenHeight);

    // Dim the frozen world.
    m_Renderer->DrawColoredRect(
        glm::vec2(0.0f, 0.0f), glm::vec2(screenW, screenH), PAUSE_DIM_COLOR);

    // "-- PAUSED --" header.
    const std::string headerText = "-- PAUSED --";
    const float headerW = m_Renderer->GetTextWidth(headerText, PAUSE_HEADER_SCALE);
    const float headerX = std::floor((screenW - headerW) * 0.5f);
    const float headerY = std::floor(screenH * 0.32f);
    m_Renderer->DrawText(
        headerText, glm::vec2(headerX, headerY), PAUSE_HEADER_SCALE, TITLE_TEXT_COLOR, 2.5f, 1.0f);

    const float menuTopY = std::floor(screenH * 0.52f);
    for (int i = 0; i < PAUSE_ITEM_COUNT; ++i)
    {
        const bool selected = (m_PauseMenu.selected == i);
        const std::string& label = PAUSE_LABELS[i];
        const std::string display =
            selected ? std::string("> ") + label : std::string("  ") + label;

        const float w = m_Renderer->GetTextWidth(display, MENU_ITEM_SCALE);
        const float x = std::floor((screenW - w) * 0.5f);
        const float y = menuTopY + i * MENU_LINE_HEIGHT;
        m_Renderer->DrawText(display,
                             glm::vec2(x, y),
                             MENU_ITEM_SCALE,
                             MenuItemColor(selected, /*enabled=*/true),
                             MENU_ITEM_OUTLINE,
                             1.0f);
    }
}
