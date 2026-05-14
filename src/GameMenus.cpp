#ifdef _WIN32
#define NOMINMAX
#endif

#include "Game.hpp"

#include "AmbienceConfig.hpp"
#include "DrawTracer.hpp"
#include "Logger.hpp"
#include "ParticleSystem.hpp"
#include "PostFXParams.hpp"
#include "Version.hpp"

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
// Per-zone particle cap during the title screen. Bumped above the gameplay
// default (50, set in Game::Initialize) because the title world has only
// one zone per type and they all span the whole map, so a denser pool reads
// as a populated backdrop instead of a sparse trickle.
constexpr size_t TITLE_PARTICLES_PER_ZONE = 160;
// Per-zone cap used during gameplay; restored when LoadGameWorld runs so
// the title bump doesn't leak into the player's actual world.
constexpr size_t GAMEPLAY_PARTICLES_PER_ZONE = 50;

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

// Hit-testing matches the renderer layout exactly. Note: IRenderer::DrawText
// takes y as the glyph baseline (despite "top-left" wording in the header).

/// Item index under the cursor for the title menu, or -1.
int TitleMenuHitTest(
    IRenderer& renderer, int screenWidth, int screenHeight, double mouseX, double mouseY)
{
    const float screenW = static_cast<float>(screenWidth);
    const float screenH = static_cast<float>(screenHeight);
    const float menuTopY = std::floor(screenH * 0.50f);
    const float ascent = renderer.GetTextAscent(MENU_ITEM_SCALE);
    constexpr float HIT_PAD_X = 24.0f;

    for (int i = 0; i < TITLE_ITEM_COUNT; ++i)
    {
        // Hit against the unselected width: the "> " vs "  " prefix shift is
        // small relative to HIT_PAD_X, so this stays comfortable as the user
        // moves the cursor across items.
        const std::string display = std::string("  ") + TITLE_LABELS[i];
        const float w = renderer.GetTextWidth(display, MENU_ITEM_SCALE);
        const float x = std::floor((screenW - w) * 0.5f);
        const float baselineY = menuTopY + i * MENU_LINE_HEIGHT;
        const float topY = baselineY - ascent;
        if (mouseX >= x - HIT_PAD_X && mouseX <= x + w + HIT_PAD_X && mouseY >= topY &&
            mouseY <= topY + MENU_LINE_HEIGHT)
        {
            return i;
        }
    }
    return -1;
}

/// Item index under the cursor for the pause menu, or -1.
int PauseMenuHitTest(
    IRenderer& renderer, int screenWidth, int screenHeight, double mouseX, double mouseY)
{
    const float screenW = static_cast<float>(screenWidth);
    const float screenH = static_cast<float>(screenHeight);
    const float menuTopY = std::floor(screenH * 0.52f);
    const float ascent = renderer.GetTextAscent(MENU_ITEM_SCALE);
    constexpr float HIT_PAD_X = 24.0f;

    for (int i = 0; i < PAUSE_ITEM_COUNT; ++i)
    {
        const std::string display = std::string("  ") + PAUSE_LABELS[i];
        const float w = renderer.GetTextWidth(display, MENU_ITEM_SCALE);
        const float x = std::floor((screenW - w) * 0.5f);
        const float baselineY = menuTopY + i * MENU_LINE_HEIGHT;
        const float topY = baselineY - ascent;
        if (mouseX >= x - HIT_PAD_X && mouseX <= x + w + HIT_PAD_X && mouseY >= topY &&
            mouseY <= topY + MENU_LINE_HEIGHT)
        {
            return i;
        }
    }
    return -1;
}

/// Confirm-overwrite modal hit test. 0 = Cancel, 1 = New Game, -1 = neither.
int ConfirmPromptHitTest(
    IRenderer& renderer, int screenWidth, int screenHeight, double mouseX, double mouseY)
{
    const float screenW = static_cast<float>(screenWidth);
    const float screenH = static_cast<float>(screenHeight);
    const float boxW = std::floor(screenW * 0.55f);
    const float boxH = std::floor(screenH * 0.30f);
    const float boxX = std::floor((screenW - boxW) * 0.5f);
    const float boxY = std::floor((screenH - boxH) * 0.5f);

    constexpr float buttonScale = 1.1f;
    const std::string cancelDisplay = std::string("  ") + "Cancel";
    const std::string confirmDisplay = std::string("  ") + "New Game";
    const float cancelW = renderer.GetTextWidth(cancelDisplay, buttonScale);
    const float confirmW = renderer.GetTextWidth(confirmDisplay, buttonScale);
    const float ascent = renderer.GetTextAscent(buttonScale);
    const float buttonBaselineY = boxY + boxH * 0.72f;
    const float cancelX = std::floor(boxX + boxW * 0.30f - cancelW * 0.5f);
    const float confirmX = std::floor(boxX + boxW * 0.70f - confirmW * 0.5f);

    constexpr float HIT_PAD_X = 16.0f;
    constexpr float HIT_PAD_Y = 8.0f;
    const float topY = buttonBaselineY - ascent - HIT_PAD_Y;
    const float rowH = ascent + 2.0f * HIT_PAD_Y;
    if (mouseX >= cancelX - HIT_PAD_X && mouseX <= cancelX + cancelW + HIT_PAD_X &&
        mouseY >= topY && mouseY <= topY + rowH)
    {
        return 0;
    }
    if (mouseX >= confirmX - HIT_PAD_X && mouseX <= confirmX + confirmW + HIT_PAD_X &&
        mouseY >= topY && mouseY <= topY + rowH)
    {
        return 1;
    }
    return -1;
}

}  // namespace

bool Game::CheckSaveExists() const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(m_SaveMapPath, ec) && fs::is_regular_file(m_SaveMapPath, ec);
}

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

    // Restore the gameplay per-zone cap in case we're returning from the
    // title screen, which bumps it for a denser backdrop.
    m_Particles.SetMaxParticlesPerZone(GAMEPLAY_PARTICLES_PER_ZONE);
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
            // Showcase the hand-painted blossom asset on the title screen so
            // pink petals drift across the menu alongside the night ambience.
            ParticleType::CherryBlossom,
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
    // Title screen showcases AuroraNight: the SkyRenderer reads the weather
    // each frame, so this turns on the aurora curtains + floating wisps.
    // Cherry blossom particles still drift across the menu via the title
    // particle zone for ParticleType::CherryBlossom.
    m_TimeManager.SetWeather(WeatherState::AuroraNight);

    // Refresh the particle system's zone pointer (zones vector reference is
    // stored across the system) and tilemap pointer.
    m_Particles.SetZones(m_Tilemap.GetParticleZones());
    m_Particles.SetTilemap(&m_Tilemap);

    // Title-screen zones span the whole map and only have one zone per type,
    // so bump the per-zone cap above the gameplay default for a denser
    // backdrop. Reset back to the gameplay value when a real game world
    // loads.
    m_Particles.SetMaxParticlesPerZone(TITLE_PARTICLES_PER_ZONE);

    // Pre-warm the particle pool so the menu doesn't open onto an empty
    // screen. Step the simulation forward a few seconds in fixed sub-frame
    // steps; that reaches steady state for the fast types (rain, sparkles)
    // and a near-cap count for the slower ones (firefly, fog). Time of day
    // and night factor must be set before stepping or zone particles whose
    // spawn rules read those (e.g. lantern day-skip) make the wrong choice.
    m_Particles.SetTimeOfDay(m_TimeManager.GetTimeOfDay());
    m_Particles.SetNightFactor(m_TimeManager.GetStarVisibility());
    m_Particles.Clear();
    const glm::vec2 prewarmCam = m_Camera.GetState().position;
    const glm::vec2 prewarmView(camWorldWidth, camWorldHeight);
    constexpr float PREWARM_DURATION_S = 5.0f;
    constexpr float PREWARM_STEP_S = 1.0f / 60.0f;
    constexpr int PREWARM_STEPS = static_cast<int>(PREWARM_DURATION_S / PREWARM_STEP_S);
    for (int s = 0; s < PREWARM_STEPS; ++s)
    {
        m_Particles.Update(PREWARM_STEP_S, prewarmCam, prewarmView);
    }
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

    // Reset mouse-tracking sentinel so the first frame in title mode skips
    // the move-detection (a stale cursor over a menu item shouldn't yank
    // selection away from the default). Suppress any in-flight click so
    // entering with the mouse held doesn't fire a phantom confirm.
    m_MenuLastMouseX = -1.0;
    m_MenuLastMouseY = -1.0;
    m_MenuMouseLeftPrev = true;
}

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

    // Mouse: cursor hover updates selection (only when the cursor actually
    // moved, so a parked cursor doesn't override keyboard nav each frame),
    // and a left-click edge confirms the hovered item. The negative sentinel
    // on m_MenuLastMouseX skips move-detection on the first frame after
    // entering the menu, so a stale cursor over an item doesn't yank the
    // default selection from underneath the keyboard user.
    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_Window, &mouseX, &mouseY);
    const bool firstMouseFrame = (m_MenuLastMouseX < 0.0);
    const bool mouseMoved =
        !firstMouseFrame && ((mouseX != m_MenuLastMouseX) || (mouseY != m_MenuLastMouseY));
    m_MenuLastMouseX = mouseX;
    m_MenuLastMouseY = mouseY;
    const bool mouseDown = (glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    const bool mouseClicked = mouseDown && !m_MenuMouseLeftPrev;
    m_MenuMouseLeftPrev = mouseDown;

    // Confirm-overwrite modal owns input while shown.
    if (m_ConfirmOverwriteShown)
    {
        const int modalHit =
            ConfirmPromptHitTest(*m_Renderer, m_ScreenWidth, m_ScreenHeight, mouseX, mouseY);
        if (modalHit >= 0)
        {
            const auto target = (modalHit == 0) ? MenuLogic::ConfirmChoice::Cancel
                                                : MenuLogic::ConfirmChoice::Confirm;
            if (mouseMoved)
            {
                m_ConfirmPrompt.selected = target;
            }
            if (mouseClicked)
            {
                m_ConfirmPrompt.selected = target;
                confirm = true;
            }
        }

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

    // Top-level title menu navigation. Mouse hover/click goes first so the
    // cursor can override keyboard selection on the same frame, and so the
    // confirm path below sees the click as a confirm.
    const int titleHit =
        TitleMenuHitTest(*m_Renderer, m_ScreenWidth, m_ScreenHeight, mouseX, mouseY);
    if (titleHit >= 0 && m_TitleMenu.enabled[titleHit])
    {
        if (mouseMoved)
        {
            m_TitleMenu.selected = titleHit;
        }
        if (mouseClicked)
        {
            m_TitleMenu.selected = titleHit;
            confirm = true;
        }
    }

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

    // Mouse hover/click on pause menu items - same pattern as title menu.
    // See ProcessTitleInput for the sentinel-based first-frame skip.
    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_Window, &mouseX, &mouseY);
    const bool firstMouseFrame = (m_MenuLastMouseX < 0.0);
    const bool mouseMoved =
        !firstMouseFrame && ((mouseX != m_MenuLastMouseX) || (mouseY != m_MenuLastMouseY));
    m_MenuLastMouseX = mouseX;
    m_MenuLastMouseY = mouseY;
    const bool mouseDown = (glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    const bool mouseClicked = mouseDown && !m_MenuMouseLeftPrev;
    m_MenuMouseLeftPrev = mouseDown;

    if (esc)
    {
        m_GameMode = GameMode::Playing;
        return;
    }

    const int pauseHit =
        PauseMenuHitTest(*m_Renderer, m_ScreenWidth, m_ScreenHeight, mouseX, mouseY);
    if (pauseHit >= 0)
    {
        if (mouseMoved)
        {
            m_PauseMenu.selected = pauseHit;
        }
        if (mouseClicked)
        {
            m_PauseMenu.selected = pauseHit;
            confirm = true;
        }
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

void Game::RenderTitleFrame()
{
    if (m_TitleMenu.enabled.size() != static_cast<size_t>(TITLE_ITEM_COUNT))
    {
        RebuildTitleMenu();
    }

    m_Renderer->BeginFrame();
    m_Renderer->BeginScene();

    DrawTracer::Mark("== title frame ==", m_Renderer->GetDrawCallCount());

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
    DrawTracer::Mark("section: BackgroundLayers", m_Renderer->GetDrawCallCount());
    m_Tilemap.RenderBackgroundLayers(*m_Renderer, renderCam, renderSize, renderCam, renderSize);
    DrawTracer::Mark("section: ForegroundLayers", m_Renderer->GetDrawCallCount());
    m_Tilemap.RenderForegroundLayers(*m_Renderer, renderCam, renderSize, renderCam, renderSize);

    // Fireflies + ambient particles, drawn on top of the world.
    DrawTracer::Mark("section: Particles", m_Renderer->GetDrawCallCount());
    m_Particles.Render(*m_Renderer, renderCam, /*noProjection=*/false, /*additive=*/false);

    // Atmospheric sky overlay: stars at night, moon, dawn glow, etc. Renders
    // under the world projection with parallax driven by the menu's renderCam,
    // matching the gameplay path.
    DrawTracer::Mark("section: Sky", m_Renderer->GetDrawCallCount());
    m_Renderer->SuspendPerspective(true);
    m_SkyRenderer.Render(*m_Renderer,
                         m_TimeManager,
                         renderCam,
                         static_cast<int>(worldWidth),
                         static_cast<int>(worldHeight));
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
    DrawTracer::Mark("section: PostFX", m_Renderer->GetDrawCallCount());
    m_Renderer->EndSceneApplyPostFX(postFX);

    DrawTracer::Mark("section: UI overlays", m_Renderer->GetDrawCallCount());

    // UI overlays draw straight to swapchain after PostFX.
    RenderTitleContent();
    if (m_ConfirmOverwriteShown)
    {
        RenderConfirmOverwritePrompt();
    }

    // Perf overlay (F4 toggle, same as gameplay): FPS / frame time / draws +
    // renderer / resolution / zoom on the right. Player position and quests
    // are intentionally omitted - neither applies on the title screen.
    if (m_Editor.IsShowDebugInfo())
    {
        constexpr float kMargin = 12.0f;
        constexpr float kCharWidth = 12.0f;
        constexpr float kLineHeight = 28.0f;
        const glm::vec3 kFpsColor(1.0f, 1.0f, 0.0f);
        const glm::vec3 kRightColor(1.0f, 0.3f, 0.3f);

        glm::mat4 uiProjection = glm::ortho(0.0f,
                                            static_cast<float>(m_ScreenWidth),
                                            static_cast<float>(m_ScreenHeight),
                                            0.0f,
                                            -1.0f,
                                            1.0f);
        m_Renderer->SetProjection(uiProjection);

        // Left column: FPS only (no player position / tile to show). Integer
        // readout - matches the gameplay overlay.
        char fpsText[32];
        std::snprintf(
            fpsText, sizeof(fpsText), "FPS: %d", static_cast<int>(m_Fps.currentFps + 0.5f));
        m_Renderer->DrawText(fpsText, glm::vec2(kMargin, 32.0f), 1.0f, kFpsColor, 2.0f, 0.85f);

        // Right column: renderer / resolution / frame time / zoom / draws.
        const char* rendererName = (m_RendererAPI == RendererAPI::OpenGL) ? "OpenGL" : "Vulkan";
        float rightMargin = static_cast<float>(m_ScreenWidth) - kMargin;

        char rendererText[32];
        std::snprintf(rendererText, sizeof(rendererText), "%s", rendererName);
        float textWidth = strnlen(rendererText, sizeof(rendererText)) * kCharWidth;
        m_Renderer->DrawText(rendererText,
                             glm::vec2(rightMargin - textWidth, 32.0f),
                             1.0f,
                             kRightColor,
                             2.0f,
                             0.85f);

        char resText[32];
        std::snprintf(resText, sizeof(resText), "%dx%d", m_ScreenWidth, m_ScreenHeight);
        textWidth = strnlen(resText, sizeof(resText)) * kCharWidth;
        m_Renderer->DrawText(resText,
                             glm::vec2(rightMargin - textWidth, 32.0f + kLineHeight),
                             1.0f,
                             kRightColor,
                             2.0f,
                             0.85f);

        char frameTimeText[32];
        float frameTimeMs = (m_Fps.currentFps > 0) ? (1000.0f / m_Fps.currentFps) : 0.0f;
        std::snprintf(frameTimeText, sizeof(frameTimeText), "%.2fms", frameTimeMs);
        textWidth = strnlen(frameTimeText, sizeof(frameTimeText)) * kCharWidth;
        m_Renderer->DrawText(frameTimeText,
                             glm::vec2(rightMargin - textWidth, 32.0f + kLineHeight * 2),
                             1.0f,
                             kRightColor,
                             2.0f,
                             0.85f);

        char zoomText[32];
        std::snprintf(zoomText, sizeof(zoomText), "Zoom: %.1fx", m_Camera.GetState().zoom);
        textWidth = strnlen(zoomText, sizeof(zoomText)) * kCharWidth;
        m_Renderer->DrawText(zoomText,
                             glm::vec2(rightMargin - textWidth, 32.0f + kLineHeight * 3),
                             1.0f,
                             kRightColor,
                             2.0f,
                             0.85f);

        char drawCallText[32];
        std::snprintf(drawCallText, sizeof(drawCallText), "Draws: %d", m_Fps.currentDrawCalls);
        textWidth = m_Renderer->GetTextWidth(drawCallText, 1.0f);
        m_Renderer->DrawText(drawCallText,
                             glm::vec2(rightMargin - textWidth, 32.0f + kLineHeight * 4),
                             1.0f,
                             kRightColor,
                             2.0f,
                             0.85f);
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
