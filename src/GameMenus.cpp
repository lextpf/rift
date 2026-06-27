#ifdef _WIN32
#define NOMINMAX
#endif

#include "Game.hpp"

#include "AmbienceConfig.hpp"
#include "CharacterConstants.hpp"
#include "Dialogue.hpp"
#include "DrawTracer.hpp"
#include "Logger.hpp"
#include "NpcSprite.hpp"
#include "NpcTag.hpp"
#include "ParticleSystem.hpp"
#include "PlayerSprite.hpp"
#include "PlayerSystem.hpp"
#include "PostFXParams.hpp"
#include "Transform.hpp"
#include "Version.hpp"
#include "ViewScaling.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <unordered_set>

#ifdef _WIN32
#include <Windows.h>
#undef DrawText
#endif

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Game";

/// Atlas region keys for the player's three sprite sheets (walk, run,
/// bicycle). Distinct from any NPC type string so collisions are impossible.
constexpr const char* kPlayerWalkAtlasKey = "__player_walk__";
constexpr const char* kPlayerRunAtlasKey = "__player_run__";
constexpr const char* kPlayerBicycleAtlasKey = "__player_bicycle__";

constexpr const char* kSkyRayAtlasKey = "__sky_ray__";
constexpr const char* kSkyStarAtlasKey = "__sky_star__";
constexpr const char* kSkyStarGlowAtlasKey = "__sky_star_glow__";
constexpr const char* kSkyShootingStarAtlasKey = "__sky_shooting_star__";
constexpr const char* kSkyGlowAtlasKey = "__sky_glow__";
constexpr const char* kSkyLightPoolAtlasKey = "__sky_light_pool__";
constexpr const char* kSkyAuroraCurtainAtlasKey = "__sky_aurora_curtain__";
constexpr const char* kSkyAuroraSmallAtlasKey = "__sky_aurora_small__";

constexpr glm::vec3 TITLE_TEXT_COLOR{1.0f, 1.0f, 1.0f};
constexpr glm::vec3 TITLE_DIM_COLOR{0.55f, 0.55f, 0.62f};
constexpr glm::vec3 TITLE_DISABLED_COLOR{0.30f, 0.30f, 0.34f};
constexpr glm::vec3 TITLE_HIGHLIGHT_COLOR{1.00f, 0.84f, 0.40f};
constexpr glm::vec4 TITLE_BG_COLOR{0.04f, 0.05f, 0.09f, 1.0f};
constexpr glm::vec4 PAUSE_DIM_COLOR{0.0f, 0.0f, 0.0f, 0.55f};
constexpr glm::vec4 MODAL_BACKDROP_COLOR{0.0f, 0.0f, 0.0f, 0.65f};
constexpr glm::vec4 MODAL_BOX_COLOR{0.10f, 0.11f, 0.16f, 0.95f};

// Title logo uses the renderer's *headline* atlas (~96 px native in OpenGL;
// falls back to a scaled body atlas in Vulkan). Scale 1.0 keeps it crisp
// instead of blurring like an upscaled body-atlas glyph.
constexpr float TITLE_LOGO_SCALE = 1.0f;
// Outline scaled to match the menu items' outline-to-glyph ratio. Outline
// offset = 2 * scale * outlineSize px; on a 96-px headline glyph that's
// ~12.5% of glyph height.
constexpr float TITLE_LOGO_OUTLINE = 6.0f;
constexpr float MENU_ITEM_SCALE = 1.4f;
constexpr float MENU_LINE_HEIGHT = 48.0f;
// Design resolution the menu's pixel/scale constants were authored against (the
// default window size). uiScale = 1.0 here, so the menu is unchanged at the
// default size and scales proportionally as the window grows/shrinks.
constexpr float MENU_REFERENCE_WIDTH = 1520.0f;
constexpr float MENU_REFERENCE_HEIGHT = 800.0f;
constexpr float VERSION_TEXT_SCALE = 0.7f;
constexpr float PAUSE_HEADER_SCALE = 1.8f;
constexpr float MODAL_TEXT_SCALE = 1.0f;

// Body-atlas (24px native) menu/button outline. ~21% outline-to-glyph ratio
// (heavier than the title's 12.5%) so strokes read against the busy backdrop.
// Selection only changes color, never outline weight.
constexpr float MENU_ITEM_OUTLINE = 2.5f;

// Title world: a small grass-only tilemap with firefly particles, frozen at
// night. Tweak these to change the title-screen aesthetic.
constexpr int TITLE_WORLD_MAP_WIDTH = 32;
constexpr int TITLE_WORLD_MAP_HEIGHT = 24;
constexpr float TITLE_WORLD_TIME_OF_DAY = 23.0f;  // 23:00 = deep night.
// Painted across layer 0. The base overworld tileset's first non-empty entry
// is grass; change this if your tileset orders things differently.
constexpr int TITLE_WORLD_GRASS_TILE_ID = 1;
// Per-zone particle cap on the title screen. Bumped above the gameplay
// default (50, set in Game::Initialize) because the title world has one
// whole-map zone per type, so a denser pool reads as populated backdrop.
// Lifted further (was 160) so Snow / Fog / CherryBlossom - whose long
// lifetimes saturate the cap quickly - actually look populated instead
// of "barely there".
constexpr size_t TITLE_PARTICLES_PER_ZONE = 240;
// Restored when LoadGameWorld runs so the title bump doesn't leak in.
constexpr size_t GAMEPLAY_PARTICLES_PER_ZONE = 50;

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

/// Ortho projection mapping (0,0)=top-left to (screenW, screenH)=bottom-right.
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

// Hit-testing matches the renderer layout exactly. IRenderer::DrawText takes
// y as the glyph baseline despite the "top-left" wording in the header.

/// Item index under the cursor for the title menu, or -1.
int TitleMenuHitTest(
    IRenderer& renderer, int screenWidth, int screenHeight, double mouseX, double mouseY)
{
    const float screenW = static_cast<float>(screenWidth);
    const float screenH = static_cast<float>(screenHeight);
    // Mirror RenderTitleContent term-for-term: same uiScale, same un-scaled
    // anchor, same per-item centering and line spacing.
    const float uiScale = viewScaling::MenuUiScale(
        screenWidth, screenHeight, MENU_REFERENCE_WIDTH, MENU_REFERENCE_HEIGHT);
    const float menuTopY = std::floor(screenH * 0.50f);
    const float lineHeight = MENU_LINE_HEIGHT * uiScale;
    const float ascent = renderer.GetTextAscent(MENU_ITEM_SCALE * uiScale);
    constexpr float HIT_PAD_X = 24.0f;

    for (int i = 0; i < TITLE_ITEM_COUNT; ++i)
    {
        // Use unselected width: "> " vs "  " prefix shift is small relative to
        // HIT_PAD_X, so the hit box stays comfortable across items.
        const std::string display = std::string("  ") + TITLE_LABELS[i];
        const float w = renderer.GetTextWidth(display, MENU_ITEM_SCALE * uiScale);
        const float x = std::floor((screenW - w) * 0.5f);
        const float baselineY = menuTopY + i * lineHeight;
        const float topY = baselineY - ascent;
        if (mouseX >= x - HIT_PAD_X && mouseX <= x + w + HIT_PAD_X && mouseY >= topY &&
            mouseY <= topY + lineHeight)
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
    // Mirror RenderPauseOverlay term-for-term: same uiScale, same un-scaled
    // anchor, same per-item centering and line spacing.
    const float uiScale = viewScaling::MenuUiScale(
        screenWidth, screenHeight, MENU_REFERENCE_WIDTH, MENU_REFERENCE_HEIGHT);
    const float menuTopY = std::floor(screenH * 0.52f);
    const float lineHeight = MENU_LINE_HEIGHT * uiScale;
    const float ascent = renderer.GetTextAscent(MENU_ITEM_SCALE * uiScale);
    constexpr float HIT_PAD_X = 24.0f;

    for (int i = 0; i < PAUSE_ITEM_COUNT; ++i)
    {
        const std::string display = std::string("  ") + PAUSE_LABELS[i];
        const float w = renderer.GetTextWidth(display, MENU_ITEM_SCALE * uiScale);
        const float x = std::floor((screenW - w) * 0.5f);
        const float baselineY = menuTopY + i * lineHeight;
        const float topY = baselineY - ascent;
        if (mouseX >= x - HIT_PAD_X && mouseX <= x + w + HIT_PAD_X && mouseY >= topY &&
            mouseY <= topY + lineHeight)
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
            m_SaveMapPath, &m_World, &loadedPlayerTileX, &loadedPlayerTileY, &loadedCharacterType);
    }

    if (!mapLoaded)
    {
        Logger::InfoF(LOG_SUBSYSTEM,
                      "{}",
                      loadSave ? "No existing save found, generating default map"
                               : "New Game: regenerating default map");
        EntityStore::Clear(m_World);
        m_Tilemap.SetTilemapSize(m_DefaultMapWidth, m_DefaultMapHeight);
    }

    // Vulkan re-uploads the tileset after a map change; OpenGL uploads lazily on first use.
    if (m_RendererAPI == RendererAPI::Vulkan && m_Renderer)
    {
        m_Renderer->UploadTexture(m_Tilemap.GetTilesetTexture());
    }

    // Pick player character: saved value, then first manifest entry, then hard default.
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
    if (!PlayerSystem::SwitchCharacter(m_World, m_PlayerEntity, initialCharacter))
    {
        Logger::Error(LOG_SUBSYSTEM, "Failed to switch player character in LoadGameWorld");
    }

    int playerTileX = (loadedPlayerTileX >= 0) ? loadedPlayerTileX : 9;
    int playerTileY = (loadedPlayerTileY >= 0) ? loadedPlayerTileY : 5;
    PlayerSystem::SetTilePosition(m_World, m_PlayerEntity, playerTileX, playerTileY);

    // Pack every loaded NPC and player sprite sheet into the tile atlas so
    // the Y-sorted pass batches into one or two draws instead of one draw
    // per texture switch. Bindings are reset on each load so a save load or
    // character switch re-binds with the freshly-packed offsets.
    PackCharactersIntoAtlas();

    float camWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
    float camWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
    glm::vec2 playerPos = m_World.get<Transform>(m_PlayerEntity).position;
    glm::vec2 playerVisualCenter =
        glm::vec2(playerPos.x, playerPos.y - CharacterConstants::HITBOX_HEIGHT);
    m_Camera.Initialize(playerVisualCenter, camWorldWidth, camWorldHeight);

    // Particle zones are tied to the live tilemap; refresh after a (re)load.
    m_Particles.SetZones(m_Tilemap.GetParticleZones());
    m_Particles.SetTilemap(&m_Tilemap);

    // Restore the gameplay cap (title screen bumps it for a denser backdrop).
    m_Particles.SetMaxParticlesPerZone(GAMEPLAY_PARTICLES_PER_ZONE);
}

void Game::PackCharactersIntoAtlas()
{
    // Build the list of sheet entries to pack. PackAdditionalSheets flips
    // every source on copy so the atlas sub-region preserves each source's
    // m_ImageData layout - that means standalone-equivalent UV math works
    // for both stbi-flipped (LoadFromFile) and image-space (LoadFromData)
    // sources without any per-source distinction here.
    std::vector<Tilemap::AtlasPackEntry> sheets;
    sheets.reserve(EntityStore::Count(m_World) + 3 + 8);

    // De-duplicate NPC types - multiple NPCs of the same type share one
    // sheet, so we only need one atlas region per type. First-seen wins.
    std::unordered_set<std::string> seenTypes;
    m_World.each<const Dialogue, const NpcSprite, const NpcTag>(
        [&](const Dialogue& dial, const NpcSprite& sprite)
        {
            const std::string& type = dial.type;
            if (type.empty())
            {
                return;
            }
            if (seenTypes.insert(type).second)
            {
                sheets.push_back({type, &m_TextureStore.Get(sprite.sheet)});
            }
        });

    // Player sheets (walk / run / bicycle) under fixed keys.
    const PlayerSprite& playerSprite = m_World.get<PlayerSprite>(m_PlayerEntity);
    sheets.push_back({kPlayerWalkAtlasKey, &PlayerSystem::GetSpriteSheet(m_World, playerSprite)});
    sheets.push_back(
        {kPlayerRunAtlasKey, &PlayerSystem::GetRunningSpriteSheet(m_World, playerSprite)});
    sheets.push_back(
        {kPlayerBicycleAtlasKey, &PlayerSystem::GetBicycleSpriteSheet(m_World, playerSprite)});

    // Sky textures (procedurally generated + AuroraSmall from file).
    sheets.push_back({kSkyRayAtlasKey, &m_SkyRenderer.GetRayTexture()});
    sheets.push_back({kSkyStarAtlasKey, &m_SkyRenderer.GetStarTexture()});
    sheets.push_back({kSkyStarGlowAtlasKey, &m_SkyRenderer.GetStarGlowTexture()});
    sheets.push_back({kSkyShootingStarAtlasKey, &m_SkyRenderer.GetShootingStarTexture()});
    sheets.push_back({kSkyGlowAtlasKey, &m_SkyRenderer.GetGlowTexture()});
    sheets.push_back({kSkyLightPoolAtlasKey, &m_SkyRenderer.GetLightPoolTexture()});
    sheets.push_back({kSkyAuroraCurtainAtlasKey, &m_SkyRenderer.GetAuroraCurtainTexture()});
    sheets.push_back({kSkyAuroraSmallAtlasKey, &m_SkyRenderer.GetAuroraSmallTexture()});

    if (!m_Tilemap.PackAdditionalSheets(sheets))
    {
        Logger::Error(
            LOG_SUBSYSTEM,
            "PackCharactersIntoAtlas: atlas pack failed; characters keep per-sheet textures");
        // Make sure no stale bindings linger.
        m_World.each<NpcSprite, NpcTag>(
            [](NpcSprite& sprite)
            {
                sprite.atlas = nullptr;
                sprite.atlasOffset = glm::vec2(0.0f);
            });
        PlayerSystem::SetAtlasBinding(
            m_World, m_PlayerEntity, nullptr, glm::vec2(0.0f), glm::vec2(0.0f), glm::vec2(0.0f));
        m_SkyRenderer.SetAtlasBinding(nullptr,
                                      glm::vec2(0.0f),
                                      glm::vec2(0.0f),
                                      glm::vec2(0.0f),
                                      glm::vec2(0.0f),
                                      glm::vec2(0.0f),
                                      glm::vec2(0.0f),
                                      glm::vec2(0.0f),
                                      glm::vec2(0.0f));
        return;
    }

    // Bind each character to the atlas region keyed by its sheet identifier.
    const Texture* atlasTex = &m_Tilemap.GetTilesetTexture();
    m_World.each<Dialogue, NpcSprite, NpcTag>(
        [&](const Dialogue& dial, NpcSprite& sprite)
        {
            auto offset = m_Tilemap.GetCharacterAtlasOffset(dial.type);
            sprite.atlas = atlasTex;
            sprite.atlasOffset = offset.value_or(glm::vec2(0.0f));
        });

    glm::vec2 walkOff =
        m_Tilemap.GetCharacterAtlasOffset(kPlayerWalkAtlasKey).value_or(glm::vec2(0.0f));
    glm::vec2 runOff =
        m_Tilemap.GetCharacterAtlasOffset(kPlayerRunAtlasKey).value_or(glm::vec2(0.0f));
    glm::vec2 bikeOff =
        m_Tilemap.GetCharacterAtlasOffset(kPlayerBicycleAtlasKey).value_or(glm::vec2(0.0f));
    PlayerSystem::SetAtlasBinding(m_World, m_PlayerEntity, atlasTex, walkOff, runOff, bikeOff);

    auto skyOff = [this](const char* key)
    { return m_Tilemap.GetCharacterAtlasOffset(key).value_or(glm::vec2(0.0f)); };
    m_SkyRenderer.SetAtlasBinding(atlasTex,
                                  skyOff(kSkyRayAtlasKey),
                                  skyOff(kSkyStarAtlasKey),
                                  skyOff(kSkyStarGlowAtlasKey),
                                  skyOff(kSkyShootingStarAtlasKey),
                                  skyOff(kSkyGlowAtlasKey),
                                  skyOff(kSkyLightPoolAtlasKey),
                                  skyOff(kSkyAuroraCurtainAtlasKey),
                                  skyOff(kSkyAuroraSmallAtlasKey));
}

void Game::PaintTitleWorld(int tilesWide, int tilesTall)
{
    tilesWide = std::max(1, tilesWide);
    tilesTall = std::max(1, tilesTall);

    m_Tilemap.SetTilemapSize(tilesWide, tilesTall, /*generateMap=*/false);

    // Paint layer 0 (Ground) with grass across the whole (possibly grown) map.
    for (int y = 0; y < tilesTall; ++y)
    {
        for (int x = 0; x < tilesWide; ++x)
        {
            m_Tilemap.SetLayerTile(x, y, /*layer=*/0, TITLE_WORLD_GRASS_TILE_ID);
        }
    }

    // One whole-map zone per atmospheric particle type, sized to the map so
    // particles fill the viewport. (Lantern excluded - needs a lit zone.)
    if (auto* zones = m_Tilemap.GetParticleZonesMutable())
    {
        zones->clear();
        const glm::vec2 zonePos(0.0f, 0.0f);
        const glm::vec2 zoneSize(static_cast<float>(tilesWide * m_Tilemap.GetTileWidth()),
                                 static_cast<float>(tilesTall * m_Tilemap.GetTileHeight()));
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

    // ParticleSystem holds the zones vector by reference; refresh after rebuild.
    m_Particles.SetZones(m_Tilemap.GetParticleZones());
    m_Particles.SetTilemap(&m_Tilemap);
}

void Game::RefreshTitleWorldForViewport(bool forceRepaint)
{
    const glm::ivec2 titleTiles = viewScaling::RequiredTitleWorldTiles(m_ScreenWidth,
                                                                       m_ScreenHeight,
                                                                       PIXEL_SCALE,
                                                                       m_Tilemap.GetTileWidth(),
                                                                       m_Tilemap.GetTileHeight(),
                                                                       m_Camera.GetState().zoom,
                                                                       /*marginTiles=*/2,
                                                                       TITLE_WORLD_MAP_WIDTH,
                                                                       TITLE_WORLD_MAP_HEIGHT);

    if (forceRepaint || titleTiles.x != m_Tilemap.GetMapWidth() ||
        titleTiles.y != m_Tilemap.GetMapHeight())
    {
        PaintTitleWorld(titleTiles.x, titleTiles.y);
    }

    // Center the camera on the (possibly resized) map using the accurate extent.
    const glm::vec2 mapCenterPx(
        static_cast<float>(m_Tilemap.GetMapWidth() * m_Tilemap.GetTileWidth()) * 0.5f,
        static_cast<float>(m_Tilemap.GetMapHeight() * m_Tilemap.GetTileHeight()) * 0.5f);
    const glm::vec2 view = VisibleWorldSizeZoomed();
    m_Camera.Initialize(mapCenterPx, view.x, view.y);
}

void Game::LoadTitleScreenWorld()
{
    // Title is purely cosmetic - no save, NPCs, player, or editor. Strip
    // session state to a clean slate before painting the grass.
    // Re-arm the title-ambient latch: a fresh title session starts with
    // the scripted ambient zones + initial weather, until the user opens
    // the console (which strips them for the rest of the session).
    m_TitleAmbientCleared = false;
    EntityStore::Clear(m_World);
    m_Editor.SetActive(false);
    m_DialogueManager.EndDialogue();
    m_DialogueUi.inDialogue = false;
    m_DialogueUi.text.clear();
    m_DialogueUi.npcId = 0;
    m_DialogueUi.page = 0;
    m_DialogueUi.charReveal = -1.0f;
    m_DialogueUi.boxFadeTimer = 0.0f;
    m_DialogueUi.snap.active = false;

    // Size the title world to the current viewport, paint grass, build zones,
    // and center the camera. Re-runnable on resize via the same helper.
    RefreshTitleWorldForViewport(/*forceRepaint=*/true);

    // Park player at (0,0) to keep its tile coords valid - it isn't rendered
    // in Title (Y-sort skips it).
    PlayerSystem::SetTilePosition(m_World, m_PlayerEntity, 0, 0);

    // Freeze time at night. TimeManager.Update is gated in Title mode so
    // this value holds until Continue / New Game.
    m_TimeManager.Initialize();
    m_TimeManager.SetTime(TITLE_WORLD_TIME_OF_DAY);
    // AuroraNight enables aurora curtains + floating wisps (SkyRenderer reads
    // weather each frame). Cherry blossoms still drift via the title zone.
    m_TimeManager.SetWeather(WeatherState::AuroraNight);

    // Bump per-zone cap for a denser backdrop (one zone per type, whole-map).
    // Reset to gameplay value when a real game world loads.
    m_Particles.SetMaxParticlesPerZone(TITLE_PARTICLES_PER_ZONE);

    // Pre-warm the particle pool so the menu doesn't open empty. Stepping a
    // few seconds reaches steady state for fast types (rain, sparkles) and a
    // near-cap count for slow ones (firefly, fog). Time-of-day and night
    // factor must be set first - some zone spawn rules read them (e.g.
    // lantern day-skip).
    m_Particles.SetTimeOfDay(m_TimeManager.GetTimeOfDay());
    m_Particles.SetNightFactor(m_TimeManager.GetStarVisibility());
    m_Particles.Clear();
    const glm::vec2 prewarmCam = m_Camera.GetState().position;
    const glm::vec2 prewarmView = VisibleWorldSizeZoomed();
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

    // Close leftover dialogue/snap state so the next gameplay frame starts clean.
    m_DialogueManager.EndDialogue();
    m_DialogueUi.inDialogue = false;
    m_DialogueUi.text.clear();
    m_DialogueUi.npcId = 0;
    m_DialogueUi.page = 0;
    m_DialogueUi.charReveal = -1.0f;
    m_DialogueUi.boxFadeTimer = 0.0f;
    m_DialogueUi.snap.active = false;
}

void Game::RebuildTitleMenu()
{
    const bool hasSave = CheckSaveExists();

    m_TitleMenu.enabled.assign(TITLE_ITEM_COUNT, true);
    m_TitleMenu.enabled[TITLE_CONTINUE] = hasSave;
    m_TitleMenu.enabled[TITLE_SETTINGS] = false;  // MVP stub.

    // Default-highlight Continue when a save exists (likely intent); otherwise
    // first enabled item (New Game).
    m_TitleMenu.selected =
        hasSave ? static_cast<int>(TITLE_CONTINUE) : MenuLogic::FirstEnabledIndex(m_TitleMenu);

    m_ConfirmOverwriteShown = false;
    m_ConfirmPrompt.selected = MenuLogic::ConfirmChoice::Cancel;

    // Sentinel skips move-detection on the first title frame so a stale
    // cursor doesn't yank selection. Also suppresses any in-flight click so
    // entering with the mouse held doesn't fire a phantom confirm.
    m_MenuLastMouseX = -1.0;
    m_MenuLastMouseY = -1.0;
    m_MenuMouseLeftPrev = true;
}

void Game::ProcessTitleInput()
{
    // Lazy first-time init so we don't have to reach into Initialize.
    // Cheap to repeat (size never grows).
    if (m_TitleMenu.enabled.size() != static_cast<size_t>(TITLE_ITEM_COUNT))
    {
        RebuildTitleMenu();
    }

    // Drive every KeyToggle to advance edge state and avoid carry-over.
    bool up = m_KeyMenuUp.JustPressed(m_Window);
    bool down = m_KeyMenuDown.JustPressed(m_Window);
    bool left = m_KeyMenuLeft.JustPressed(m_Window);
    bool right = m_KeyMenuRight.JustPressed(m_Window);
    bool confirm = m_KeyMenuConfirm.JustPressed(m_Window);
    bool esc = m_KeyEscape.JustPressed(m_Window);

    // Mouse: hover updates selection only when the cursor actually moved (so
    // a parked cursor doesn't override keyboard nav); left-click edge confirms.
    // The negative sentinel on m_MenuLastMouseX skips move-detection on the
    // first menu frame so a stale cursor doesn't yank the default selection.
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

    // Mouse hover/click goes first so the cursor can override the keyboard
    // selection on the same frame and the confirm path sees the click.
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
        // No-op on title - user must explicitly select Quit.
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
            // Reset time so Continue doesn't inherit the title's 23:00 night
            // setting (LoadGameWorld doesn't touch TimeManager).
            m_TimeManager.Initialize();
            LoadGameWorld(/*loadSave=*/true);
            m_GameMode = GameMode::Playing;
            break;
        }
        case TITLE_SETTINGS:
        {
            // Stub (disabled; NavigateDown skips it).
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

    // Mouse hover/click - same pattern as title menu (see ProcessTitleInput).
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
            // Restore the cosmetic title world so the menu sits over grass +
            // fireflies again instead of the abandoned session.
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

    // Clear to the title world's sky color (deep blue at night).
    glm::vec3 skyColor = m_TimeManager.GetSkyColor();
    m_Renderer->Clear(skyColor.r, skyColor.g, skyColor.b, 1.0f);
    m_Renderer->SetAmbientColor(m_TimeManager.GetAmbientColor());

    // Title world has no entities, so the Y-sort assembly is skipped here.
    // Foreground layers still render in case future tweaks add upper-layer overlays.
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

    // Ambient particles (fireflies, etc.) drawn on top of the world.
    DrawTracer::Mark("section: Particles", m_Renderer->GetDrawCallCount());
    m_Particles.Render(*m_Renderer, renderCam, /*noProjection=*/false, /*additive=*/false);

    // Sky overlay (stars, moon, dawn glow). World projection with parallax
    // driven by the menu's renderCam, matching the gameplay path.
    DrawTracer::Mark("section: Sky", m_Renderer->GetDrawCallCount());
    m_Renderer->SuspendPerspective(true);
    m_SkyRenderer.Render(*m_Renderer,
                         m_TimeManager,
                         renderCam,
                         static_cast<int>(worldWidth),
                         static_cast<int>(worldHeight));
    m_Renderer->SuspendPerspective(false);

    // Composite through PostFX. Modest bloom so fireflies glow without
    // blowing the screen out.
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
        // gradingParams default-constructs to identity. postFXEnabled=false
        // is the real off-switch; these zeroes are a defensive fallback in
        // case the uniform fails to bind.
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

    // Perf overlay (F4): FPS + right-column renderer/res/zoom. Player position
    // and quests are intentionally omitted on title.
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

        // Left column: FPS only (matches gameplay overlay format).
        char fpsText[32];
        std::snprintf(
            fpsText, sizeof(fpsText), "FPS: %d", static_cast<int>(m_Fps.currentFps + 0.5f));
        m_Renderer->DrawText(fpsText, glm::vec2(kMargin, 32.0f), 1.0f, kFpsColor, 2.0f, 0.85f);

        // Right column: renderer, resolution, frame time, zoom, draws.
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

    glm::mat4 uiProjection = MakeUIProjection(m_ScreenWidth, m_ScreenHeight);
    m_Renderer->SetProjection(uiProjection);

    const float screenW = static_cast<float>(m_ScreenWidth);
    const float screenH = static_cast<float>(m_ScreenHeight);
    const float uiScale = viewScaling::MenuUiScale(
        m_ScreenWidth, m_ScreenHeight, MENU_REFERENCE_WIDTH, MENU_REFERENCE_HEIGHT);

    // "RIFT" logo (headline atlas), scaled and anchored proportionally.
    const std::string logoText = "RIFT";
    const float logoWidth = m_Renderer->GetTextWidthLarge(logoText, TITLE_LOGO_SCALE * uiScale);
    const float logoX = std::floor((screenW - logoWidth) * 0.5f);
    const float logoY = std::floor(screenH * 0.22f);
    m_Renderer->DrawTextLarge(logoText,
                              glm::vec2(logoX, logoY),
                              TITLE_LOGO_SCALE * uiScale,
                              TITLE_TEXT_COLOR,
                              TITLE_LOGO_OUTLINE,
                              1.0f);

    const float menuTopY = std::floor(screenH * 0.50f);
    const float lineHeight = MENU_LINE_HEIGHT * uiScale;
    for (int i = 0; i < TITLE_ITEM_COUNT; ++i)
    {
        const bool enabled = m_TitleMenu.enabled[i];
        const bool selected = (m_TitleMenu.selected == i);
        const std::string& label = TITLE_LABELS[i];
        const std::string display =
            selected ? std::string("> ") + label : std::string("  ") + label;

        const float w = m_Renderer->GetTextWidth(display, MENU_ITEM_SCALE * uiScale);
        const float x = std::floor((screenW - w) * 0.5f);
        const float y = menuTopY + i * lineHeight;
        m_Renderer->DrawText(display,
                             glm::vec2(x, y),
                             MENU_ITEM_SCALE * uiScale,
                             MenuItemColor(selected, enabled),
                             MENU_ITEM_OUTLINE,
                             1.0f);
    }

    // Version footer (bottom-right).
    const std::string versionText = std::string("rift ") + RIFT_VERSION;
    const float versionWidth = m_Renderer->GetTextWidth(versionText, VERSION_TEXT_SCALE * uiScale);
    m_Renderer->DrawText(
        versionText,
        glm::vec2(screenW - versionWidth - 16.0f * uiScale, screenH - 28.0f * uiScale),
        VERSION_TEXT_SCALE * uiScale,
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

    // Semi-opaque backdrop over the title.
    m_Renderer->DrawColoredRect(
        glm::vec2(0.0f, 0.0f), glm::vec2(screenW, screenH), MODAL_BACKDROP_COLOR);

    const float boxW = std::floor(screenW * 0.55f);
    const float boxH = std::floor(screenH * 0.30f);
    const float boxX = std::floor((screenW - boxW) * 0.5f);
    const float boxY = std::floor((screenH - boxH) * 0.5f);
    m_Renderer->DrawColoredRect(glm::vec2(boxX, boxY), glm::vec2(boxW, boxH), MODAL_BOX_COLOR);

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

    // Cancel | New Game buttons, side by side.
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
    const float uiScale = viewScaling::MenuUiScale(
        m_ScreenWidth, m_ScreenHeight, MENU_REFERENCE_WIDTH, MENU_REFERENCE_HEIGHT);

    // Dim the frozen world.
    m_Renderer->DrawColoredRect(
        glm::vec2(0.0f, 0.0f), glm::vec2(screenW, screenH), PAUSE_DIM_COLOR);

    const std::string headerText = "-- PAUSED --";
    const float headerW = m_Renderer->GetTextWidth(headerText, PAUSE_HEADER_SCALE * uiScale);
    const float headerX = std::floor((screenW - headerW) * 0.5f);
    const float headerY = std::floor(screenH * 0.32f);
    m_Renderer->DrawText(headerText,
                         glm::vec2(headerX, headerY),
                         PAUSE_HEADER_SCALE * uiScale,
                         TITLE_TEXT_COLOR,
                         2.5f,
                         1.0f);

    const float menuTopY = std::floor(screenH * 0.52f);
    const float lineHeight = MENU_LINE_HEIGHT * uiScale;
    for (int i = 0; i < PAUSE_ITEM_COUNT; ++i)
    {
        const bool selected = (m_PauseMenu.selected == i);
        const std::string& label = PAUSE_LABELS[i];
        const std::string display =
            selected ? std::string("> ") + label : std::string("  ") + label;

        const float w = m_Renderer->GetTextWidth(display, MENU_ITEM_SCALE * uiScale);
        const float x = std::floor((screenW - w) * 0.5f);
        const float y = menuTopY + i * lineHeight;
        m_Renderer->DrawText(display,
                             glm::vec2(x, y),
                             MENU_ITEM_SCALE * uiScale,
                             MenuItemColor(selected, /*enabled=*/true),
                             MENU_ITEM_OUTLINE,
                             1.0f);
    }
}
