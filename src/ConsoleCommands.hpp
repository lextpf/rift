#pragma once

#include <ecs.hpp>

#include <glm/glm.hpp>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class CameraController;
class ConsoleBuffer;
class ConsoleCommandRegistry;
class DialogueManager;
class Editor;
class Game;
class GameStateManager;
class IRenderer;
class ParticleSystem;
class TimeManager;
class Tilemap;
class WeatherDirector;

/**
 * @struct CommandContext
 * @brief Aggregates the state references a console command may operate on.
 * @ingroup Core
 *
 * Built fresh for each command invocation by the Console (which knows the
 * full Game), but the individual command implementations only touch the
 * pointers they need. This makes them unit-testable in isolation: a test
 * fills in just the references the command requires.
 *
 * All pointers are nullable. A command must check the pointers it needs
 * and emit an error to @ref out if a required reference is missing.
 */
struct CommandContext
{
    ConsoleBuffer& out;
    ecs::entity playerEntity{};  ///< Player entity; resolve via @ref npcs (the world).
    GameStateManager* gameState = nullptr;
    TimeManager* time = nullptr;
    Tilemap* tilemap = nullptr;
    ecs::registry* npcs = nullptr;  ///< Live NPC store (ECS registry) for npc.* commands.
    const ConsoleCommandRegistry* registry = nullptr;
    Editor* editor = nullptr;             ///< Level editor state and toggles.
    CameraController* camera = nullptr;   ///< Camera (3D effect, globe radius/tilt).
    IRenderer* renderer = nullptr;        ///< Active renderer for texture uploads.
    Game* game = nullptr;                 ///< Game (cross-cutting ops like renderer.set).
    bool* postFXEnabled = nullptr;        ///< Master toggle for post-processing pipeline.
    DialogueManager* dialogue = nullptr;  ///< Branching dialogue manager (current node, options).
    ParticleSystem* particles = nullptr;  ///< Particle system (single-shot spawn, list, kill).
    WeatherDirector* weatherDirector = nullptr;  ///< Weather transitions (time.weather routing).
};

/**
 * @name Default console commands
 * @brief Free functions implementing each built-in command.
 *
 * All return `true` on success, `false` on error (the error message has
 * already been printed to `ctx.out`). Tests call these directly with a
 * hand-built CommandContext; the production wiring goes through
 * Console::RegisterDefaultCommands.
 * @{
 */
bool Cmd_Help(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_Clear(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_Teleport(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_FlagSet(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_FlagGet(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TimeSet(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TimeAdd(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TimeFreeze(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_MapLoad(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_StateDump(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NoClip(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_Editor(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_AppearanceCopy(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_AppearanceRestore(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_CharacterSet(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_CharacterNext(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_RendererSet(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_DebugInfo(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_DebugOverlays(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_ParticlesToggle(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_Globe(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TimeNext(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_GlobeRadius(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_GlobeTilt(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_GlobeIntensity(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_PostFX(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_PlayerSpeed(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_PlayerPos(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_PlayerBicycle(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_PlayerRun(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_MoveAccel(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_MoveDecel(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_MoveLookahead(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_MoveDump(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NpcList(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NpcTp(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NpcSpawn(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NpcDespawn(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NpcFreeze(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NpcDialog(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_DialogueActive(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_DialogueEnd(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_DialogueSkip(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_FlagList(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_FlagUnset(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TimeScale(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TimeWeather(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_WeatherIntensity(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_WeatherNext(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_WeatherRandom(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_WeatherForecast(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_WeatherAuto(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_WeatherStatus(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_WeatherWind(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_LightAdd(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_LightClear(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_LightList(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_LightRemove(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TimeStatus(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_ParticleSpawn(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_ParticleList(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_ParticleKillAll(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_CameraFreecam(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_CameraZoom(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_CameraFollow(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_CameraInfo(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_MapSave(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_MapSize(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_MapCollision(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_Perf(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_RendererTrace(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_ConsoleCopy(std::span<const std::string_view> args, CommandContext& ctx);

// World inspection
bool Cmd_LayersList(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TileInfo(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TileFind(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_MapStats(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TilesetInfo(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_AnimList(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_StructList(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_StructInfo(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_StructGoto(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_ZoneList(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_ZoneGoto(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_LightGoto(std::span<const std::string_view> args, CommandContext& ctx);

// Navigation
bool Cmd_NavPath(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NavReachable(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NpcPath(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NpcGoto(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_NpcNearest(std::span<const std::string_view> args, CommandContext& ctx);

// Quests
bool Cmd_QuestList(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_QuestGive(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_QuestComplete(std::span<const std::string_view> args, CommandContext& ctx);

// Engine info
bool Cmd_Version(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_RendererInfo(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_MemStats(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_EcsValidate(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_ConfigDump(std::span<const std::string_view> args, CommandContext& ctx);

// Bookmarks (the bookmark map is owned by Console; passed in as an extra
// parameter so the free functions remain trivially testable without a Console).
bool Cmd_BookmarkSet(std::span<const std::string_view> args,
                     CommandContext& ctx,
                     std::unordered_map<std::string, glm::ivec2>& bookmarks);
bool Cmd_BookmarkTp(std::span<const std::string_view> args,
                    CommandContext& ctx,
                    std::unordered_map<std::string, glm::ivec2>& bookmarks);
bool Cmd_BookmarkList(std::span<const std::string_view> args,
                      CommandContext& ctx,
                      const std::unordered_map<std::string, glm::ivec2>& bookmarks);
/// @}
