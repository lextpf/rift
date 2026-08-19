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
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Core
 *
 * Built fresh for each command invocation by Console::RegisterDefaultCommands
 * (which knows the full Game), but the individual command implementations only
 * touch the members they need. This makes them unit-testable in isolation: a
 * test fills in just the references the command requires.
 *
 * Every pointer member is nullable; a command must check the ones it needs and
 * emit an error to @ref out when a required service is missing. The two
 * non-pointer members follow different rules: @ref out is a reference and is
 * always valid, while @ref playerEntity is a value that may still be a default
 * or stale handle, so resolve it through @ref npcs and check the result.
 *
 * @warning **Do not store.** Single-frame contract, the same one EditorContext
 * carries: every member aliases Game-owned state and the struct is rebuilt per
 * invocation. @ref renderer is re-read from Game::m_Renderer, which
 * Game::SwitchRenderer destroys and recreates; @ref tilemap contents are
 * replaced wholesale by a map load. Caching a CommandContext - or any pointer
 * taken out of one - past the handler that received it dangles.
 */
struct CommandContext
{
    ConsoleBuffer& out;          ///< Scrollback sink for all command output; always valid.
    ecs::entity playerEntity{};  ///< Player entity; resolve via @ref npcs (the world).
    GameStateManager* gameState = nullptr;  ///< Save-game flag/quest store for flag.* and quest.*.
    TimeManager* time = nullptr;            ///< Clock and weather state for time.* and weather.*.
    Tilemap* tilemap = nullptr;             ///< Active map; replaced wholesale by a map load.
    /// ECS world registry: the player entity and every NPC. Required by player.*, character.*,
    /// appearance.*, move.* and npc.* alike. Resolve @ref playerEntity through it.
    ecs::registry* npcs = nullptr;
    /// Command table used by `help` and by tab completion. Const because a handler may only
    /// read the catalog, never register into it mid-dispatch.
    const ConsoleCommandRegistry* registry = nullptr;
    Editor* editor = nullptr;            ///< Level editor state and toggles.
    CameraController* camera = nullptr;  ///< Camera (position, zoom, follow, free mode).
    /// Active renderer: sprite/texture uploads (character.*, npc.spawn) and backend identity
    /// queries (renderer.info). Recreated by renderer.set, so never cache it.
    IRenderer* renderer = nullptr;
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
 *
 * Each brief quotes the canonical name and argument grammar exactly as
 * registered there - `&lt;x&gt;` is required, `[x]` optional. Aliases are not
 * repeated here; `help` prints them and the registry resolves them.
 * @{
 */

/**
 * @name Core, player, time, weather, and rendering
 * @{
 */
/// @brief `help` - list every registered command; extra arguments are ignored.
bool Cmd_Help(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `clear` - drop the scrollback (input line and history survive).
bool Cmd_Clear(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `teleport &lt;tx&gt; &lt;ty&gt;` - move the player to a tile coord.
bool Cmd_Teleport(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `flag.set &lt;name&gt; &lt;value&gt;` - set a game-state flag.
bool Cmd_FlagSet(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `flag.get &lt;name&gt;` - print one game-state flag.
bool Cmd_FlagGet(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `time.set &lt;hours&gt;` - set in-game time (0.0-24.0).
bool Cmd_TimeSet(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `time.add &lt;hours&gt;` - offset time of day (signed, wraps 0..24).
bool Cmd_TimeAdd(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `time.freeze [on|off|toggle]` - pause/resume the day-night cycle.
bool Cmd_TimeFreeze(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `map.load &lt;filename&gt;` - switch maps.
bool Cmd_MapLoad(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `state.dump` - print player tile, time, NPC count, and quests.
bool Cmd_StateDump(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `noclip [on|off]` - toggle player tile/NPC collision.
bool Cmd_NoClip(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `editor [on|off|toggle]` - toggle level editor mode.
bool Cmd_Editor(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `appearance.copy` - copy the appearance of the nearest NPC within 32px.
bool Cmd_AppearanceCopy(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `appearance.restore` - restore the original character appearance.
bool Cmd_AppearanceRestore(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `character.set &lt;type&gt;` - switch player character (a CharacterType name).
bool Cmd_CharacterSet(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `character.next` - cycle to the next player character.
bool Cmd_CharacterNext(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `renderer.set &lt;opengl|vulkan&gt;` - switch backend at runtime.
bool Cmd_RendererSet(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `debug.info [on|off|toggle]` - toggle the FPS/coords HUD.
bool Cmd_DebugInfo(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `debug.overlays [on|off|toggle]` - collision/nav/anchor overlays.
bool Cmd_DebugOverlays(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `particles [on|off|toggle]` - all particle rendering (weather + zones).
bool Cmd_ParticlesToggle(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `world3d [on|off|toggle]` - render gameplay through the 3D camera path.
bool Cmd_World3D(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `cam.preset &lt;classic|ds|free&gt;` - select the 3D camera preset.
bool Cmd_CamPreset(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `cam.yaw &lt;degrees&gt;` - orbit the 3D camera horizontally (implies Free).
bool Cmd_CamYaw(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `cam.pitch &lt;degrees&gt;` - 3D camera elevation above the horizon (implies Free).
bool Cmd_CamPitch(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `time.next` - advance to the next time-of-day preset.
bool Cmd_TimeNext(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `postfx [on|off|toggle]` - bloom/grading/vignette/grain master switch.
bool Cmd_PostFX(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `player.speed [multiplier]` - scale movement speed (1.0 = normal).
bool Cmd_PlayerSpeed(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `player.pos` - print the player's tile, world pixel, and facing.
bool Cmd_PlayerPos(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `player.bicycle [on|off|toggle]` - toggle bicycle mode.
bool Cmd_PlayerBicycle(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `player.run [on|off|toggle]` - toggle running mode.
bool Cmd_PlayerRun(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `move.accel [px/s^2]` - acceleration rate (momentum ramp-up).
bool Cmd_MoveAccel(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `move.decel [px/s^2]` - deceleration rate (momentum ramp-down).
bool Cmd_MoveDecel(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `move.lookahead [px]` - camera look-ahead along the travel direction.
bool Cmd_MoveLookahead(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `move.dump` - print the current accel/decel/look-ahead values.
bool Cmd_MoveDump(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `npc.list` - list NPCs (idx, name, type, tile, AI state).
bool Cmd_NpcList(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `npc.tp &lt;idx&gt; &lt;tx&gt; &lt;ty&gt;` - teleport an NPC by index.
bool Cmd_NpcTp(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `npc.spawn &lt;type&gt; &lt;tx&gt; &lt;ty&gt;` - spawn an NPC at a tile.
bool Cmd_NpcSpawn(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `npc.despawn &lt;idx&gt;` - remove an NPC (refused while it is in dialogue).
bool Cmd_NpcDespawn(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `npc.freeze &lt;idx|all&gt; [on|off|toggle]` - halt NPC AI.
bool Cmd_NpcFreeze(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `npc.dialog &lt;idx&gt; &lt;text...&gt;` - set an NPC's simple dialogue text.
bool Cmd_NpcDialog(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `dialogue.active` - print the current dialogue node and visible options.
bool Cmd_DialogueActive(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `dialogue.end` - force-close any active dialogue (simple or tree).
bool Cmd_DialogueEnd(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `dialogue.skip` - advance tree dialogue (confirm the current selection).
bool Cmd_DialogueSkip(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `flag.list` - dump every game-state flag.
bool Cmd_FlagList(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `flag.unset &lt;name&gt;` - remove a flag.
bool Cmd_FlagUnset(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `time.scale &lt;multiplier&gt;` - time progression rate (1.0 = normal).
bool Cmd_TimeScale(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `time.weather &lt;name&gt; [seconds]` - set weather, blended (0 = instant).
bool Cmd_TimeWeather(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `weather.overlay &lt;name|off&gt;` - set/clear the manual sky overlay.
bool Cmd_WeatherOverlay(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `weather.intensity &lt;0.0-1.0&gt;` - density / effect strength.
bool Cmd_WeatherIntensity(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `weather.next [seconds]` - cycle to the next weather (0 = instant).
bool Cmd_WeatherNext(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `weather.random [seconds]` - pick a random weather (0 = instant).
bool Cmd_WeatherRandom(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `weather.forecast [days]` - upcoming fronts/night events (default 3, cap 7).
bool Cmd_WeatherForecast(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `weather.auto [on|off]` - forecast autonomy; no arg prints the state.
bool Cmd_WeatherAuto(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `weather.status` - current weather, transition, auto/hold flags, wind.
bool Cmd_WeatherStatus(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `weather.wind` - gusted wind direction/strength readout.
bool Cmd_WeatherWind(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `light.add &lt;x&gt; &lt;y&gt; [r g b] [radius] [schedule]` - place a light.
bool Cmd_LightAdd(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `light.clear` - remove every WorldLight.
bool Cmd_LightClear(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `light.list` - lights with index, position, color, radius, schedule.
bool Cmd_LightList(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `light.remove &lt;index&gt;` - remove one WorldLight by index.
bool Cmd_LightRemove(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `time.status` - print time, period, weather, day count, moon phase.
bool Cmd_TimeStatus(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `particle.spawn &lt;type&gt; &lt;wx&gt; &lt;wy&gt;` - spawn at world pixels.
bool Cmd_ParticleSpawn(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `particle.list` - count active particles by type and list zones.
bool Cmd_ParticleList(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `particle.kill_all` - remove every active particle.
bool Cmd_ParticleKillAll(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `camera.freecam [on|off|toggle]` - decouple the camera from the player.
bool Cmd_CameraFreecam(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `camera.zoom &lt;factor&gt;` - set camera zoom (0.1-10.0).
bool Cmd_CameraZoom(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `camera.follow [on|off|toggle]` - re-attach the camera to the player.
bool Cmd_CameraFollow(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `camera.info` - dump pos, zoom, freecam, follow, and tilt.
bool Cmd_CameraInfo(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `map.save [path]` - save the current map to JSON.
bool Cmd_MapSave(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `map.size` - print map dimensions in tiles and pixels.
bool Cmd_MapSize(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `map.collision &lt;tx&gt; &lt;ty&gt;` - query the collision flag at a tile.
bool Cmd_MapCollision(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `perf` - print FPS, frame time, and draw-call count.
bool Cmd_Perf(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `renderer.trace [on|off|dump|clear]` - per-frame draw-call trace.
bool Cmd_RendererTrace(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `console.copy` - copy the whole scrollback to the OS clipboard.
bool Cmd_ConsoleCopy(std::span<const std::string_view> args, CommandContext& ctx);
/// @}

/**
 * @name World inspection
 * @{
 */
/// @brief `layers.list` - every tilemap layer (name, order, fill, animations).
bool Cmd_LayersList(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `tile.info &lt;tx&gt; &lt;ty&gt;` - inspect one tile across all layers.
bool Cmd_TileInfo(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `tile.find &lt;tileID&gt; [layer]` - find tiles by ID.
bool Cmd_TileFind(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `map.stats` - cells, collision %, nav %, layer/struct/light counts.
bool Cmd_MapStats(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `tileset.info` - tileset texture and tile dimensions.
bool Cmd_TilesetInfo(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `anim.list` - animated tile definitions and their use counts.
bool Cmd_AnimList(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `struct.list` - list all no-projection structures.
bool Cmd_StructList(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `struct.info &lt;id&gt;` - detail for one structure.
bool Cmd_StructInfo(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `struct.goto &lt;id&gt;` - snap the camera to a structure.
bool Cmd_StructGoto(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `zone.list` - list particle zones.
bool Cmd_ZoneList(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `zone.goto &lt;idx&gt;` - snap the camera to a zone center.
bool Cmd_ZoneGoto(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `light.goto &lt;idx&gt;` - snap the camera to a world light.
bool Cmd_LightGoto(std::span<const std::string_view> args, CommandContext& ctx);
/// @}

/**
 * @name Navigation
 * @{
 */
/// @brief `nav.path &lt;fx&gt; &lt;fy&gt; &lt;tx&gt; &lt;ty&gt;` - BFS path on the nav grid.
bool Cmd_NavPath(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `nav.reachable &lt;tx&gt; &lt;ty&gt;` - reachable tile count plus bounds.
bool Cmd_NavReachable(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `npc.path &lt;idx&gt;` - print one NPC's patrol waypoints.
bool Cmd_NpcPath(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `npc.goto &lt;idx&gt;` - snap the camera to an NPC.
bool Cmd_NpcGoto(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `npc.nearest` - the NPC nearest the player by tile distance.
bool Cmd_NpcNearest(std::span<const std::string_view> args, CommandContext& ctx);
/// @}

/**
 * @name Quests
 * @{
 */
/// @brief `quest.list` - list active and completed quests.
bool Cmd_QuestList(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `quest.give &lt;name&gt; [description...]` - accept a quest.
bool Cmd_QuestGive(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `quest.complete &lt;name&gt;` - mark a quest complete.
bool Cmd_QuestComplete(std::span<const std::string_view> args, CommandContext& ctx);
/// @}

/**
 * @name Engine info
 * @{
 */
/// @brief `version` - engine version, build config, and build date.
bool Cmd_Version(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `renderer.info` - active backend plus GPU vendor/device/driver.
bool Cmd_RendererInfo(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `mem.stats` - approximate memory usage.
bool Cmd_MemStats(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `ecs.validate` - check registry integrity (table/pools/globals/links).
bool Cmd_EcsValidate(std::span<const std::string_view> args, CommandContext& ctx);
/// @brief `config.dump` - emit the current toggles as a replayable command list.
bool Cmd_ConfigDump(std::span<const std::string_view> args, CommandContext& ctx);
/// @}

/**
 * @name Bookmarks
 * @brief Session-scoped player-position marks.
 *
 * The bookmark map is owned by Console, not carried in CommandContext, so it is
 * passed as an extra parameter to keep these free functions trivially testable
 * without a Console. It is never persisted to disk.
 * @{
 */
/// @brief `bookmark.set &lt;name&gt;` - save the player's current tile.
bool Cmd_BookmarkSet(std::span<const std::string_view> args,
                     CommandContext& ctx,
                     std::unordered_map<std::string, glm::ivec2>& bookmarks);
/// @brief `bookmark.tp &lt;name&gt;` - teleport to a saved bookmark.
bool Cmd_BookmarkTp(std::span<const std::string_view> args,
                    CommandContext& ctx,
                    std::unordered_map<std::string, glm::ivec2>& bookmarks);
/// @brief `bookmark.list` - list saved bookmarks (alphabetical).
bool Cmd_BookmarkList(std::span<const std::string_view> args,
                      CommandContext& ctx,
                      const std::unordered_map<std::string, glm::ivec2>& bookmarks);
/// @}

/// @}
