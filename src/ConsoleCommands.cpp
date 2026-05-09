#include "ConsoleCommands.h"

#include "CameraController.h"
#include "Console.h"
#include "DialogueManager.h"
#include "DialogueTypes.h"
#include "DrawTracer.h"
#include "Editor.h"
#include "EnumTraits.h"
#include "Game.h"
#include "GameStateManager.h"
#include "IRenderer.h"
#include "NonPlayerCharacter.h"
#include "ParticleSystem.h"
#include "PlayerCharacter.h"
#include "Tilemap.h"
#include "TimeManager.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <string>

namespace
{
constexpr int CONSOLE_TILE_SIZE = 16;

/// Parse a non-negative integer from @p text. Returns true on success.
bool ParseInt(std::string_view text, int& out)
{
    if (text.empty())
    {
        return false;
    }
    int value = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size())
    {
        return false;
    }
    out = value;
    return true;
}

/// Parse a finite float from @p text. Accepts optional sign, decimal point.
bool ParseFloat(std::string_view text, float& out)
{
    if (text.empty())
    {
        return false;
    }
    // std::from_chars for float is supported by MSVC and recent libstdc++/libc++.
    float value = 0.0f;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size())
    {
        return false;
    }
    if (!std::isfinite(value))
    {
        return false;
    }
    out = value;
    return true;
}

/// Parse `[on|off|toggle]` (also `1`/`0`/`true`/`false`) into a target boolean.
/// Empty args means "toggle". Any other arg pattern fails with a usage error.
bool ParseToggleArg(std::span<const std::string_view> args,
                    bool current,
                    const char* commandName,
                    ConsoleBuffer& out,
                    bool& outTarget)
{
    if (args.empty())
    {
        outTarget = !current;
        return true;
    }
    if (args.size() != 1)
    {
        out.PrintError(std::string(commandName) + ": usage '" + commandName + " [on|off|toggle]'");
        return false;
    }
    auto a = args[0];
    if (a == "on" || a == "1" || a == "true")
    {
        outTarget = true;
        return true;
    }
    if (a == "off" || a == "0" || a == "false")
    {
        outTarget = false;
        return true;
    }
    if (a == "toggle")
    {
        outTarget = !current;
        return true;
    }
    out.PrintError(std::string(commandName) + ": usage '" + commandName + " [on|off|toggle]'");
    return false;
}

/// Parse `<opengl|vulkan>` (case-insensitive) into a RendererAPI value.
/// Pure helper kept at namespace scope so unit tests can exercise it without
/// a live Game.
bool ParseRendererAPI(std::string_view text, RendererAPI& out)
{
    if (text == "opengl" || text == "OpenGL" || text == "OPENGL" || text == "gl")
    {
        out = RendererAPI::OpenGL;
        return true;
    }
    if (text == "vulkan" || text == "Vulkan" || text == "VULKAN" || text == "vk")
    {
        out = RendererAPI::Vulkan;
        return true;
    }
    return false;
}
}  // namespace

// ============================================================================
// help
// ============================================================================

bool Cmd_Help(std::span<const std::string_view> /*args*/, CommandContext& ctx)
{
    // Bind ctx.out to a local reference up-front so the static analyzer
    // tracks it independently of the path-sensitive null-state of
    // ctx.registry below (otherwise it pessimistically flags the PrintError
    // call as a "Called C++ object pointer is null").
    ConsoleBuffer& out = ctx.out;
    const ConsoleCommandRegistry* registry = ctx.registry;
    if (registry == nullptr)
    {
        out.PrintError("help: registry unavailable");
        return false;
    }
    out.Print("Commands:");
    for (const auto& [name, cmd] : registry->All())
    {
        std::string line = "  " + name;
        if (!cmd.aliases.empty())
        {
            line += " (";
            for (std::size_t i = 0; i < cmd.aliases.size(); ++i)
            {
                if (i > 0)
                {
                    line += ", ";
                }
                line += cmd.aliases[i];
            }
            line += ")";
        }
        line += " - " + cmd.description;
        out.Print(line);
    }
    return true;
}

// ============================================================================
// clear
// ============================================================================

bool Cmd_Clear(std::span<const std::string_view> /*args*/, CommandContext& ctx)
{
    ctx.out.Clear();
    return true;
}

// ============================================================================
// teleport <tx> <ty>
// ============================================================================

bool Cmd_Teleport(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.player == nullptr)
    {
        ctx.out.PrintError("teleport: player unavailable");
        return false;
    }
    if (args.size() != 2)
    {
        ctx.out.PrintError("teleport: usage 'teleport <tileX> <tileY>'");
        return false;
    }
    int tx = 0;
    int ty = 0;
    if (!ParseInt(args[0], tx) || !ParseInt(args[1], ty))
    {
        ctx.out.PrintError("teleport: tile coords must be non-negative integers");
        return false;
    }
    ctx.player->SetTilePosition(tx, ty);
    ctx.out.Print("teleported player to tile (" + std::to_string(tx) + ", " + std::to_string(ty) +
                  ")");
    return true;
}

// ============================================================================
// flag.set <name> <value>
// ============================================================================

bool Cmd_FlagSet(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.gameState == nullptr)
    {
        ctx.out.PrintError("flag.set: game state unavailable");
        return false;
    }
    if (args.size() != 2)
    {
        ctx.out.PrintError("flag.set: usage 'flag.set <name> <value>'");
        return false;
    }
    std::string name(args[0]);
    std::string value(args[1]);
    ctx.gameState->SetFlagValue(name, value);
    ctx.out.Print("flag.set: " + name + " = " + value);
    return true;
}

// ============================================================================
// flag.get <name>
// ============================================================================

bool Cmd_FlagGet(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.gameState == nullptr)
    {
        ctx.out.PrintError("flag.get: game state unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("flag.get: usage 'flag.get <name>'");
        return false;
    }
    std::string name(args[0]);
    if (!ctx.gameState->HasFlag(name))
    {
        ctx.out.Print(name + " = <unset>");
        return true;
    }
    ctx.out.Print(name + " = " + ctx.gameState->GetFlagValue(name));
    return true;
}

// ============================================================================
// time.set <hours>
// ============================================================================

bool Cmd_TimeSet(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.time == nullptr)
    {
        ctx.out.PrintError("time.set: time manager unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("time.set: usage 'time.set <hours 0.0-24.0>'");
        return false;
    }
    float hours = 0.0f;
    if (!ParseFloat(args[0], hours))
    {
        ctx.out.PrintError("time.set: hours must be a finite number");
        return false;
    }
    ctx.time->SetTime(hours);
    char formatted[32];
    std::snprintf(formatted, sizeof(formatted), "%.2f", static_cast<double>(hours));
    ctx.out.Print(std::string("time.set: ") + formatted + "h");
    return true;
}

// ============================================================================
// time.freeze [on|off|toggle] - pause / resume the day-night cycle
// ============================================================================

bool Cmd_TimeFreeze(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.time == nullptr)
    {
        ctx.out.PrintError("time.freeze: time manager unavailable");
        return false;
    }
    bool target = false;
    if (!ParseToggleArg(args, ctx.time->IsPaused(), "time.freeze", ctx.out, target))
    {
        return false;
    }
    ctx.time->SetPaused(target);
    ctx.out.Print(std::string("time.freeze: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// map.load <filename>
// ============================================================================

bool Cmd_MapLoad(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.tilemap == nullptr || ctx.npcs == nullptr || ctx.player == nullptr)
    {
        ctx.out.PrintError("map.load: world refs unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("map.load: usage 'map.load <filename>'");
        return false;
    }
    std::string filename(args[0]);
    int playerTileX = -1;
    int playerTileY = -1;
    int characterType = -1;
    const bool ok = ctx.tilemap->LoadMapFromJSON(
        filename, ctx.npcs, &playerTileX, &playerTileY, &characterType);
    if (!ok)
    {
        ctx.out.PrintError("map.load: failed to load '" + filename + "'");
        return false;
    }
    if (playerTileX >= 0 && playerTileY >= 0)
    {
        ctx.player->SetTilePosition(playerTileX, playerTileY);
    }
    ctx.out.Print("map.load: loaded '" + filename + "'");
    return true;
}

// ============================================================================
// state.dump
// ============================================================================

bool Cmd_StateDump(std::span<const std::string_view> /*args*/, CommandContext& ctx)
{
    if (ctx.player == nullptr || ctx.time == nullptr || ctx.gameState == nullptr ||
        ctx.npcs == nullptr)
    {
        ctx.out.PrintError("state.dump: refs unavailable");
        return false;
    }

    const glm::vec2 pos = ctx.player->GetPosition();
    const int tileX = static_cast<int>(std::floor(pos.x / CONSOLE_TILE_SIZE));
    // Player Y is at the feet (bottom of tile); subtract a small epsilon to
    // pull the floor() onto the correct tile when standing exactly on the
    // boundary, matching how dialogue/teleport code computes the tile.
    const int tileY = static_cast<int>(std::floor((pos.y - 0.1f) / CONSOLE_TILE_SIZE));

    const float hours = ctx.time->GetTimeOfDay();
    const auto activeQuests = ctx.gameState->GetActiveQuests();

    char buf[64];

    std::snprintf(buf,
                  sizeof(buf),
                  "player: tile=(%d, %d) world=(%.1f, %.1f)",
                  tileX,
                  tileY,
                  static_cast<double>(pos.x),
                  static_cast<double>(pos.y));
    ctx.out.Print(buf);

    std::snprintf(buf, sizeof(buf), "time: %.2fh", static_cast<double>(hours));
    ctx.out.Print(buf);

    std::snprintf(buf, sizeof(buf), "npcs: %zu", static_cast<std::size_t>(ctx.npcs->size()));
    ctx.out.Print(buf);

    std::snprintf(
        buf, sizeof(buf), "active quests: %zu", static_cast<std::size_t>(activeQuests.size()));
    ctx.out.Print(buf);
    for (const auto& q : activeQuests)
    {
        ctx.out.Print("  - " + q);
    }
    return true;
}

// ============================================================================
// player.speed [multiplier] - speedhack: scale player movement speed
// ============================================================================

bool Cmd_PlayerSpeed(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.player == nullptr)
    {
        ctx.out.PrintError("player.speed: player unavailable");
        return false;
    }
    if (args.empty())
    {
        char line[64];
        std::snprintf(line, sizeof(line), "player.speed: %.3f", ctx.player->GetSpeedMultiplier());
        ctx.out.Print(line);
        return true;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("player.speed: usage 'player.speed [multiplier]'");
        return false;
    }
    float m = 0.0f;
    if (!ParseFloat(args[0], m))
    {
        ctx.out.PrintError("player.speed: multiplier must be a finite number");
        return false;
    }
    if (m <= 0.0f)
    {
        ctx.out.PrintError("player.speed: multiplier must be > 0");
        return false;
    }
    ctx.player->SetSpeedMultiplier(m);
    char line[64];
    std::snprintf(line, sizeof(line), "player.speed: %.3f", m);
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// noclip [on|off] - toggle by default; explicit on/off if provided.
// ============================================================================

bool Cmd_NoClip(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.player == nullptr)
    {
        ctx.out.PrintError("noclip: player unavailable");
        return false;
    }
    bool target = !ctx.player->IsNoClip();  // default: toggle
    if (args.size() == 1)
    {
        if (args[0] == "on" || args[0] == "1" || args[0] == "true")
        {
            target = true;
        }
        else if (args[0] == "off" || args[0] == "0" || args[0] == "false")
        {
            target = false;
        }
        else
        {
            ctx.out.PrintError("noclip: usage 'noclip [on|off]'");
            return false;
        }
    }
    else if (args.size() > 1)
    {
        ctx.out.PrintError("noclip: usage 'noclip [on|off]'");
        return false;
    }
    ctx.player->SetNoClip(target);
    ctx.out.Print(std::string("noclip: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// editor [on|off|toggle]
// ============================================================================

bool Cmd_Editor(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.editor == nullptr)
    {
        ctx.out.PrintError("editor: editor unavailable");
        return false;
    }
    bool target = false;
    if (!ParseToggleArg(args, ctx.editor->IsActive(), "editor", ctx.out, target))
    {
        return false;
    }
    ctx.editor->SetActive(target);
    ctx.out.Print(std::string("editor: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// appearance.copy - copy appearance from nearest NPC within 32px
// ============================================================================

bool Cmd_AppearanceCopy(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.player == nullptr || ctx.npcs == nullptr || ctx.renderer == nullptr)
    {
        ctx.out.PrintError("appearance.copy: world refs unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("appearance.copy: usage 'appearance.copy'");
        return false;
    }
    if (ctx.npcs->empty())
    {
        ctx.out.PrintError("appearance.copy: no NPCs loaded");
        return false;
    }
    constexpr float APPEARANCE_COPY_RANGE = 32.0f;
    const glm::vec2 playerPos = ctx.player->GetPosition();
    NonPlayerCharacter* nearest = nullptr;
    float nearestDist = APPEARANCE_COPY_RANGE + 1.0f;
    for (auto& npc : *ctx.npcs)
    {
        const glm::vec2 npcPos = npc.GetPosition();
        const float dist = glm::length(npcPos - playerPos);
        if (dist < nearestDist && dist <= APPEARANCE_COPY_RANGE)
        {
            nearestDist = dist;
            nearest = &npc;
        }
    }
    if (nearest == nullptr)
    {
        ctx.out.PrintError("appearance.copy: no NPC within 32px");
        return false;
    }
    const std::string spritePath = nearest->GetSpritePath();
    if (!ctx.player->CopyAppearanceFrom(spritePath))
    {
        ctx.out.PrintError("appearance.copy: failed to load sprite '" + spritePath + "'");
        return false;
    }
    ctx.player->UploadTextures(*ctx.renderer);
    ctx.out.Print("appearance.copy: copied from '" + nearest->GetType() + "'");
    return true;
}

// ============================================================================
// appearance.restore - restore original character appearance
// ============================================================================

bool Cmd_AppearanceRestore(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.player == nullptr || ctx.renderer == nullptr)
    {
        ctx.out.PrintError("appearance.restore: refs unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("appearance.restore: usage 'appearance.restore'");
        return false;
    }
    if (!ctx.player->IsUsingCopiedAppearance())
    {
        ctx.out.Print("appearance.restore: already on original appearance");
        return true;
    }
    ctx.player->RestoreOriginalAppearance();
    ctx.player->UploadTextures(*ctx.renderer);
    ctx.out.Print("appearance.restore: restored");
    return true;
}

// ============================================================================
// character.set <name>
// ============================================================================

bool Cmd_CharacterSet(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.player == nullptr)
    {
        ctx.out.PrintError("character.set: player unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError(
            "character.set: usage 'character.set <BW1_MALE|BW1_FEMALE|BW2_MALE|BW2_FEMALE|"
            "CC_FEMALE>'");
        return false;
    }
    const auto parsed = EnumTraits<CharacterType>::FromString(args[0]);
    if (!parsed.has_value())
    {
        ctx.out.PrintError("character.set: unknown character '" + std::string(args[0]) +
                           "' (valid: BW1_MALE BW1_FEMALE BW2_MALE BW2_FEMALE CC_FEMALE)");
        return false;
    }
    if (!ctx.player->SwitchCharacter(*parsed))
    {
        ctx.out.PrintError(std::string("character.set: failed to load sprites for ") +
                           std::string(EnumTraits<CharacterType>::ToString(*parsed)));
        return false;
    }
    ctx.out.Print(std::string("character.set: ") +
                  std::string(EnumTraits<CharacterType>::ToString(*parsed)));
    return true;
}

// ============================================================================
// character.next
// ============================================================================

bool Cmd_CharacterNext(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.player == nullptr)
    {
        ctx.out.PrintError("character.next: player unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("character.next: usage 'character.next'");
        return false;
    }
    const CharacterType next = NextEnum(ctx.player->GetCharacterType());
    if (!ctx.player->SwitchCharacter(next))
    {
        ctx.out.PrintError(std::string("character.next: failed to load sprites for ") +
                           std::string(EnumTraits<CharacterType>::ToString(next)));
        return false;
    }
    ctx.out.Print(std::string("character.next: ") +
                  std::string(EnumTraits<CharacterType>::ToString(next)));
    return true;
}

// ============================================================================
// renderer.set <opengl|vulkan>
// ============================================================================

bool Cmd_RendererSet(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.game == nullptr)
    {
        ctx.out.PrintError("renderer.set: game unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("renderer.set: usage 'renderer.set <opengl|vulkan>'");
        return false;
    }
    RendererAPI api{};
    if (!ParseRendererAPI(args[0], api))
    {
        ctx.out.PrintError("renderer.set: usage 'renderer.set <opengl|vulkan>'");
        return false;
    }
    if (!ctx.game->SwitchRenderer(api))
    {
        ctx.out.PrintError("renderer.set: switch failed");
        return false;
    }
    ctx.out.Print(std::string("renderer.set: ") +
                  (api == RendererAPI::OpenGL ? "OpenGL" : "Vulkan"));
    return true;
}

// ============================================================================
// debug.info [on|off|toggle]
// ============================================================================

bool Cmd_DebugInfo(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.editor == nullptr)
    {
        ctx.out.PrintError("debug.info: editor unavailable");
        return false;
    }
    bool target = false;
    if (!ParseToggleArg(args, ctx.editor->IsShowDebugInfo(), "debug.info", ctx.out, target))
    {
        return false;
    }
    ctx.editor->SetShowDebugInfo(target);
    ctx.out.Print(std::string("debug.info: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// debug.overlays [on|off|toggle]
// ============================================================================

bool Cmd_DebugOverlays(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.editor == nullptr)
    {
        ctx.out.PrintError("debug.overlays: editor unavailable");
        return false;
    }
    bool target = false;
    if (!ParseToggleArg(args, ctx.editor->IsDebugMode(), "debug.overlays", ctx.out, target))
    {
        return false;
    }
    ctx.editor->SetDebugMode(target);
    ctx.out.Print(std::string("debug.overlays: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// globe [on|off|toggle]
// ============================================================================

bool Cmd_Globe(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.camera == nullptr)
    {
        ctx.out.PrintError("globe: camera unavailable");
        return false;
    }
    bool target = false;
    if (!ParseToggleArg(args, ctx.camera->Is3DEnabled(), "globe", ctx.out, target))
    {
        return false;
    }
    ctx.camera->SetEnable3DEffect(target);
    ctx.out.Print(std::string("globe: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// time.next - cycle through 8 time-of-day presets
// ============================================================================

bool Cmd_TimeNext(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.time == nullptr)
    {
        ctx.out.PrintError("time.next: time manager unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("time.next: usage 'time.next'");
        return false;
    }
    static constexpr float kPresetHours[8] = {6.0f, 8.5f, 13.0f, 17.0f, 19.0f, 21.0f, 1.0f, 4.5f};
    static constexpr const char* kPresetName[8] = {"Dawn (06:00)",
                                                   "Morning (08:30)",
                                                   "Midday (13:00)",
                                                   "Afternoon (17:00)",
                                                   "Dusk (19:00)",
                                                   "Evening (21:00)",
                                                   "Night (01:00)",
                                                   "Late Night (04:30)"};
    // -1 means the next call lands on index 0 (Dawn). Persists across calls.
    static int s_cycle = -1;
    s_cycle = (s_cycle + 1) % 8;
    ctx.time->SetTime(kPresetHours[s_cycle]);
    ctx.out.Print(std::string("time.next: ") + kPresetName[s_cycle]);
    return true;
}

// ============================================================================
// globe.radius <50.0-500.0>
// ============================================================================

bool Cmd_GlobeRadius(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.camera == nullptr)
    {
        ctx.out.PrintError("globe.radius: camera unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("globe.radius: usage 'globe.radius <50.0-500.0>'");
        return false;
    }
    float v = 0.0f;
    if (!ParseFloat(args[0], v))
    {
        ctx.out.PrintError("globe.radius: value must be a finite number");
        return false;
    }
    if (v < 50.0f || v > 500.0f)
    {
        ctx.out.PrintError("globe.radius: value out of range [50.0, 500.0]");
        return false;
    }
    ctx.camera->GetState().globeSphereRadius = v;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "globe.radius: %.2f", static_cast<double>(v));
    ctx.out.Print(buf);
    return true;
}

// ============================================================================
// globe.tilt <0.0-1.0>
// ============================================================================

bool Cmd_GlobeTilt(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.camera == nullptr)
    {
        ctx.out.PrintError("globe.tilt: camera unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("globe.tilt: usage 'globe.tilt <0.0-1.0>'");
        return false;
    }
    float v = 0.0f;
    if (!ParseFloat(args[0], v))
    {
        ctx.out.PrintError("globe.tilt: value must be a finite number");
        return false;
    }
    if (v < 0.0f || v > 1.0f)
    {
        ctx.out.PrintError("globe.tilt: value out of range [0.0, 1.0]");
        return false;
    }
    ctx.camera->GetState().tilt = v;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "globe.tilt: %.2f", static_cast<double>(v));
    ctx.out.Print(buf);
    return true;
}

// ============================================================================
// postfx [on|off|toggle] - master toggle for the post-processing pipeline
// ============================================================================

bool Cmd_PostFX(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.postFXEnabled == nullptr)
    {
        ctx.out.PrintError("postfx: state unavailable");
        return false;
    }
    bool target = false;
    if (!ParseToggleArg(args, *ctx.postFXEnabled, "postfx", ctx.out, target))
    {
        return false;
    }
    *ctx.postFXEnabled = target;
    ctx.out.Print(std::string("postfx: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// globe.intensity <up|down> - coupled radius+tilt step (matches PgUp/PgDn)
// ============================================================================

bool Cmd_GlobeIntensity(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.camera == nullptr)
    {
        ctx.out.PrintError("globe.intensity: camera unavailable");
        return false;
    }
    if (args.size() != 1 || (args[0] != "up" && args[0] != "down"))
    {
        ctx.out.PrintError("globe.intensity: usage 'globe.intensity <up|down>'");
        return false;
    }
    auto& s = ctx.camera->GetState();
    if (args[0] == "up")
    {
        s.globeSphereRadius = std::min(500.0f, s.globeSphereRadius + 10.0f);
        s.tilt = std::max(0.0f, s.tilt - 0.05f);
    }
    else
    {
        s.globeSphereRadius = std::max(50.0f, s.globeSphereRadius - 10.0f);
        s.tilt = std::min(1.0f, s.tilt + 0.05f);
    }
    char buf[64];
    std::snprintf(buf,
                  sizeof(buf),
                  "globe.intensity: radius=%.2f tilt=%.2f",
                  static_cast<double>(s.globeSphereRadius),
                  static_cast<double>(s.tilt));
    ctx.out.Print(buf);
    return true;
}

// ============================================================================
// player.pos - print tile coords, world pixel coords, facing direction
// ============================================================================

bool Cmd_PlayerPos(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.player == nullptr)
    {
        ctx.out.PrintError("player.pos: player unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("player.pos: usage 'player.pos'");
        return false;
    }
    const glm::vec2 p = ctx.player->GetPosition();
    const int tileX = static_cast<int>(std::floor(p.x / CONSOLE_TILE_SIZE));
    const int tileY = static_cast<int>(std::floor((p.y - 0.1f) / CONSOLE_TILE_SIZE));
    const char* facing = "DOWN";
    switch (ctx.player->GetDirection())
    {
        case CharacterDirection::DOWN:
            facing = "DOWN";
            break;
        case CharacterDirection::UP:
            facing = "UP";
            break;
        case CharacterDirection::LEFT:
            facing = "LEFT";
            break;
        case CharacterDirection::RIGHT:
            facing = "RIGHT";
            break;
    }
    char line[128];
    std::snprintf(line,
                  sizeof(line),
                  "player.pos: tile=(%d, %d) world=(%.1f, %.1f) facing=%s",
                  tileX,
                  tileY,
                  static_cast<double>(p.x),
                  static_cast<double>(p.y),
                  facing);
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// player.bicycle [on|off|toggle]
// ============================================================================

bool Cmd_PlayerBicycle(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.player == nullptr)
    {
        ctx.out.PrintError("player.bicycle: player unavailable");
        return false;
    }
    bool target = false;
    if (!ParseToggleArg(args, ctx.player->IsBicycling(), "player.bicycle", ctx.out, target))
    {
        return false;
    }
    ctx.player->SetBicycling(target);
    ctx.out.Print(std::string("player.bicycle: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// player.run [on|off|toggle]
// ============================================================================

bool Cmd_PlayerRun(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.player == nullptr)
    {
        ctx.out.PrintError("player.run: player unavailable");
        return false;
    }
    bool target = false;
    if (!ParseToggleArg(args, ctx.player->IsRunning(), "player.run", ctx.out, target))
    {
        return false;
    }
    ctx.player->SetRunning(target);
    ctx.out.Print(std::string("player.run: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// npc.list - print every NPC's index, name, type, tile, AI state
// ============================================================================

bool Cmd_NpcList(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.npcs == nullptr)
    {
        ctx.out.PrintError("npc.list: npc list unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("npc.list: usage 'npc.list'");
        return false;
    }
    char header[64];
    std::snprintf(
        header, sizeof(header), "npc.list: %zu NPC(s)", static_cast<std::size_t>(ctx.npcs->size()));
    ctx.out.Print(header);
    if (ctx.npcs->empty())
    {
        return true;
    }
    ctx.out.Print("  (idx may shift after npc.despawn)");
    for (std::size_t i = 0; i < ctx.npcs->size(); ++i)
    {
        const auto& npc = (*ctx.npcs)[i];
        char line[192];
        std::snprintf(line,
                      sizeof(line),
                      "  [%zu] %s (%s) tile=(%d, %d) %s",
                      i,
                      npc.GetName().c_str(),
                      npc.GetType().c_str(),
                      npc.GetTileX(),
                      npc.GetTileY(),
                      npc.IsStopped() ? "stopped" : "patrolling");
        ctx.out.Print(line);
    }
    return true;
}

// ============================================================================
// npc.tp <idx> <tileX> <tileY>
// ============================================================================

bool Cmd_NpcTp(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.npcs == nullptr)
    {
        ctx.out.PrintError("npc.tp: npc list unavailable");
        return false;
    }
    if (args.size() != 3)
    {
        ctx.out.PrintError("npc.tp: usage 'npc.tp <idx> <tileX> <tileY>'");
        return false;
    }
    int idx = 0;
    int tx = 0;
    int ty = 0;
    if (!ParseInt(args[0], idx) || !ParseInt(args[1], tx) || !ParseInt(args[2], ty))
    {
        ctx.out.PrintError("npc.tp: idx and tile coords must be non-negative integers");
        return false;
    }
    if (idx < 0 || static_cast<std::size_t>(idx) >= ctx.npcs->size())
    {
        ctx.out.PrintError("npc.tp: idx out of range");
        return false;
    }
    (*ctx.npcs)[static_cast<std::size_t>(idx)].SetTilePosition(tx, ty, CONSOLE_TILE_SIZE);
    char line[80];
    std::snprintf(line, sizeof(line), "npc.tp: [%d] -> tile (%d, %d)", idx, tx, ty);
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// npc.spawn <type> <tileX> <tileY> - load sprite, place at tile,
// upload textures if a renderer is bound (skipped in unit tests).
// ============================================================================

bool Cmd_NpcSpawn(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.npcs == nullptr)
    {
        ctx.out.PrintError("npc.spawn: npc list unavailable");
        return false;
    }
    if (args.size() != 3)
    {
        ctx.out.PrintError("npc.spawn: usage 'npc.spawn <type> <tileX> <tileY>'");
        return false;
    }
    int tx = 0;
    int ty = 0;
    if (!ParseInt(args[1], tx) || !ParseInt(args[2], ty))
    {
        ctx.out.PrintError("npc.spawn: tile coords must be non-negative integers");
        return false;
    }
    const std::string type(args[0]);
    NonPlayerCharacter npc;
    if (!npc.Load(NonPlayerCharacter::ResolveAssetPath(type)))
    {
        ctx.out.PrintError("npc.spawn: failed to load sprite for type '" + type + "'");
        return false;
    }
    npc.SetTilePosition(tx, ty, CONSOLE_TILE_SIZE);
    if (ctx.renderer != nullptr)
    {
        npc.UploadTextures(*ctx.renderer);
    }
    ctx.npcs->push_back(std::move(npc));
    char line[96];
    std::snprintf(line,
                  sizeof(line),
                  "npc.spawn: [%zu] '%s' at tile (%d, %d)",
                  static_cast<std::size_t>(ctx.npcs->size() - 1),
                  type.c_str(),
                  tx,
                  ty);
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// npc.despawn <idx> - refuse if NPC is the active dialogue speaker.
// ============================================================================

bool Cmd_NpcDespawn(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.npcs == nullptr)
    {
        ctx.out.PrintError("npc.despawn: npc list unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("npc.despawn: usage 'npc.despawn <idx>'");
        return false;
    }
    int idx = 0;
    if (!ParseInt(args[0], idx))
    {
        ctx.out.PrintError("npc.despawn: idx must be a non-negative integer");
        return false;
    }
    if (idx < 0 || static_cast<std::size_t>(idx) >= ctx.npcs->size())
    {
        ctx.out.PrintError("npc.despawn: idx out of range");
        return false;
    }
    if (ctx.game != nullptr && ctx.game->IsInSimpleDialogue() &&
        ctx.game->GetDialogueNPCIndex() == idx)
    {
        ctx.out.PrintError(
            "npc.despawn: cannot despawn NPC currently in dialogue (use dialogue.end first)");
        return false;
    }
    ctx.npcs->erase(ctx.npcs->begin() + idx);
    char line[64];
    std::snprintf(line, sizeof(line), "npc.despawn: removed [%d]", idx);
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// npc.freeze <idx|all> [on|off|toggle]
// ============================================================================

bool Cmd_NpcFreeze(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.npcs == nullptr)
    {
        ctx.out.PrintError("npc.freeze: npc list unavailable");
        return false;
    }
    if (args.empty())
    {
        ctx.out.PrintError("npc.freeze: usage 'npc.freeze <idx|all> [on|off|toggle]'");
        return false;
    }
    const bool isAll = (args[0] == "all");
    int idx = -1;
    if (!isAll)
    {
        if (!ParseInt(args[0], idx) || idx < 0 || static_cast<std::size_t>(idx) >= ctx.npcs->size())
        {
            ctx.out.PrintError("npc.freeze: first arg must be 'all' or a valid NPC index");
            return false;
        }
    }
    std::span<const std::string_view> toggleArgs = args.subspan(1);
    const bool current = isAll ? false : (*ctx.npcs)[static_cast<std::size_t>(idx)].IsStopped();
    bool target = false;
    if (!ParseToggleArg(toggleArgs, current, "npc.freeze", ctx.out, target))
    {
        return false;
    }
    if (isAll)
    {
        for (auto& n : *ctx.npcs)
        {
            n.SetStopped(target);
        }
        ctx.out.Print(std::string("npc.freeze all: ") + (target ? "ON" : "OFF"));
    }
    else
    {
        (*ctx.npcs)[static_cast<std::size_t>(idx)].SetStopped(target);
        char line[64];
        std::snprintf(line, sizeof(line), "npc.freeze [%d]: %s", idx, target ? "ON" : "OFF");
        ctx.out.Print(line);
    }
    return true;
}

// ============================================================================
// npc.dialog <idx> <text...> - rejoins multi-token text with single spaces
// (the console tokenizer is whitespace-only, no quoting).
// ============================================================================

bool Cmd_NpcDialog(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.npcs == nullptr)
    {
        ctx.out.PrintError("npc.dialog: npc list unavailable");
        return false;
    }
    if (args.size() < 2)
    {
        ctx.out.PrintError("npc.dialog: usage 'npc.dialog <idx> <text...>'");
        return false;
    }
    int idx = 0;
    if (!ParseInt(args[0], idx) || idx < 0 || static_cast<std::size_t>(idx) >= ctx.npcs->size())
    {
        ctx.out.PrintError("npc.dialog: idx out of range");
        return false;
    }
    std::string text;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        if (i > 1)
        {
            text += ' ';
        }
        text.append(args[i]);
    }
    (*ctx.npcs)[static_cast<std::size_t>(idx)].SetDialogue(text);
    char line[96];
    std::snprintf(line, sizeof(line), "npc.dialog [%d]: set", idx);
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// dialogue.active - print mode + current node for tree, or simple text
// ============================================================================

bool Cmd_DialogueActive(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.dialogue == nullptr)
    {
        ctx.out.PrintError("dialogue.active: dialogue manager unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("dialogue.active: usage 'dialogue.active'");
        return false;
    }
    const bool simpleActive = (ctx.game != nullptr && ctx.game->IsInSimpleDialogue());
    const bool treeActive = ctx.dialogue->IsActive();
    if (!simpleActive && !treeActive)
    {
        ctx.out.Print("dialogue.active: no active dialogue");
        return true;
    }
    if (simpleActive)
    {
        ctx.out.Print(std::string("dialogue.active: simple text=\"") +
                      ctx.game->GetSimpleDialogueText() + "\"");
        return true;
    }
    const DialogueNode* node = ctx.dialogue->GetCurrentNode();
    if (node == nullptr)
    {
        ctx.out.Print("dialogue.active: tree (no current node)");
        return true;
    }
    char line[192];
    std::snprintf(line,
                  sizeof(line),
                  "dialogue.active: tree node='%s' selected=%d",
                  node->id.c_str(),
                  ctx.dialogue->GetSelectedOptionIndex());
    ctx.out.Print(line);
    const auto& opts = ctx.dialogue->GetVisibleOptions();
    for (std::size_t i = 0; i < opts.size(); ++i)
    {
        if (opts[i] != nullptr)
        {
            ctx.out.Print(std::string("  ") + std::to_string(i) + ": " + opts[i]->text);
        }
    }
    return true;
}

// ============================================================================
// dialogue.end - close any active dialogue (simple or tree)
// ============================================================================

bool Cmd_DialogueEnd(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.game == nullptr)
    {
        ctx.out.PrintError("dialogue.end: game unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("dialogue.end: usage 'dialogue.end'");
        return false;
    }
    ctx.game->EndAnyDialogue();
    ctx.out.Print("dialogue.end: closed");
    return true;
}

// ============================================================================
// dialogue.skip - confirm current option (advance tree dialogue)
// ============================================================================

bool Cmd_DialogueSkip(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.dialogue == nullptr)
    {
        ctx.out.PrintError("dialogue.skip: dialogue manager unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("dialogue.skip: usage 'dialogue.skip'");
        return false;
    }
    if (!ctx.dialogue->IsActive())
    {
        ctx.out.PrintError("dialogue.skip: no active tree dialogue");
        return false;
    }
    ctx.dialogue->ConfirmSelection();
    ctx.out.Print("dialogue.skip: advanced");
    return true;
}

// ============================================================================
// flag.list - dump every flag in GameStateManager
// ============================================================================

bool Cmd_FlagList(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.gameState == nullptr)
    {
        ctx.out.PrintError("flag.list: game state unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("flag.list: usage 'flag.list'");
        return false;
    }
    const auto& flags = ctx.gameState->GetAllFlags();
    char header[48];
    std::snprintf(
        header, sizeof(header), "flag.list: %zu flag(s)", static_cast<std::size_t>(flags.size()));
    ctx.out.Print(header);
    // Sort for deterministic output (unordered_map iteration order is unstable).
    std::vector<std::string> keys;
    keys.reserve(flags.size());
    for (const auto& [k, _v] : flags)
    {
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    for (const auto& k : keys)
    {
        const auto it = flags.find(k);
        ctx.out.Print(std::string("  ") + k + " = " + (it != flags.end() ? it->second : ""));
    }
    return true;
}

// ============================================================================
// flag.unset <name>
// ============================================================================

bool Cmd_FlagUnset(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.gameState == nullptr)
    {
        ctx.out.PrintError("flag.unset: game state unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("flag.unset: usage 'flag.unset <name>'");
        return false;
    }
    const std::string name(args[0]);
    const bool existed = ctx.gameState->HasFlag(name);
    ctx.gameState->ClearFlag(name);
    if (existed)
    {
        ctx.out.Print(std::string("flag.unset: removed '") + name + "'");
    }
    else
    {
        ctx.out.Print(std::string("flag.unset: '") + name + "' was not set");
    }
    return true;
}

// ============================================================================
// time.scale <multiplier>
// ============================================================================

bool Cmd_TimeScale(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.time == nullptr)
    {
        ctx.out.PrintError("time.scale: time manager unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("time.scale: usage 'time.scale <multiplier>'");
        return false;
    }
    float m = 0.0f;
    if (!ParseFloat(args[0], m))
    {
        ctx.out.PrintError("time.scale: multiplier must be a finite number");
        return false;
    }
    if (m <= 0.0f)
    {
        ctx.out.PrintError("time.scale: multiplier must be > 0");
        return false;
    }
    ctx.time->SetTimeScale(m);
    char line[48];
    std::snprintf(line, sizeof(line), "time.scale: %.3f", static_cast<double>(m));
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// time.weather <clear|overcast>
// ============================================================================

bool Cmd_TimeWeather(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.time == nullptr)
    {
        ctx.out.PrintError("time.weather: time manager unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("time.weather: usage 'time.weather <name>' (use Tab to list states)");
        return false;
    }
    auto parsed = EnumTraits<WeatherState>::FromString(args[0]);
    if (!parsed.has_value())
    {
        ctx.out.PrintError(std::string("time.weather: unknown weather state '") +
                           std::string(args[0]) + "' (use Tab for valid names)");
        return false;
    }
    ctx.time->SetWeather(*parsed);
    ctx.out.Print(std::string("time.weather: ") +
                  std::string(EnumTraits<WeatherState>::ToString(*parsed)));
    return true;
}

bool Cmd_WeatherIntensity(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.time == nullptr)
    {
        ctx.out.PrintError("weather.intensity: time manager unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("weather.intensity: usage 'weather.intensity <0.0-1.0>'");
        return false;
    }
    float v = 0.0f;
    if (!ParseFloat(args[0], v) || v < 0.0f || v > 1.0f)
    {
        ctx.out.PrintError("weather.intensity: value must be in [0.0, 1.0]");
        return false;
    }
    ctx.time->SetWeatherIntensity(v);
    char line[64];
    std::snprintf(line, sizeof(line), "weather.intensity: %.2f", static_cast<double>(v));
    ctx.out.Print(line);
    return true;
}

bool Cmd_WeatherNext(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.time == nullptr)
    {
        ctx.out.PrintError("weather.next: time manager unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("weather.next: usage 'weather.next' (no args)");
        return false;
    }
    WeatherState next = NextEnum(ctx.time->GetWeather());
    ctx.time->SetWeather(next);
    ctx.out.Print(std::string("weather.next: ") +
                  std::string(EnumTraits<WeatherState>::ToString(next)));
    return true;
}

bool Cmd_WeatherRandom(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.time == nullptr)
    {
        ctx.out.PrintError("weather.random: time manager unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("weather.random: usage 'weather.random' (no args)");
        return false;
    }
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0,
                                            static_cast<int>(EnumTraits<WeatherState>::Count) - 1);
    auto pick = static_cast<WeatherState>(dist(rng));
    ctx.time->SetWeather(pick);
    ctx.out.Print(std::string("weather.random: ") +
                  std::string(EnumTraits<WeatherState>::ToString(pick)));
    return true;
}

// ============================================================================
// light.* - WorldLight registry on the active Tilemap
// ============================================================================

bool Cmd_LightAdd(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.tilemap == nullptr)
    {
        ctx.out.PrintError("light.add: tilemap unavailable");
        return false;
    }
    // Accept: x y [r g b] [radius] [schedule]
    // Forms: 2, 5, 6, or 7 args.
    if (args.size() != 2 && args.size() != 5 && args.size() != 6 && args.size() != 7)
    {
        ctx.out.PrintError("light.add: usage 'light.add <x> <y> [r g b] [radius] [schedule]'");
        return false;
    }
    WorldLight light;
    if (!ParseFloat(args[0], light.position.x) || !ParseFloat(args[1], light.position.y))
    {
        ctx.out.PrintError("light.add: x/y must be finite numbers");
        return false;
    }
    if (args.size() >= 5)
    {
        if (!ParseFloat(args[2], light.color.r) || !ParseFloat(args[3], light.color.g) ||
            !ParseFloat(args[4], light.color.b))
        {
            ctx.out.PrintError("light.add: r/g/b must be finite numbers");
            return false;
        }
    }
    if (args.size() >= 6)
    {
        if (!ParseFloat(args[5], light.radius) || light.radius <= 0.0f)
        {
            ctx.out.PrintError("light.add: radius must be a positive number");
            return false;
        }
    }
    if (args.size() == 7)
    {
        auto sched = EnumTraits<LightSchedule>::FromString(args[6]);
        if (!sched.has_value())
        {
            ctx.out.PrintError("light.add: schedule must be AlwaysOn, NightOnly, or DuskToDawn");
            return false;
        }
        light.schedule = *sched;
    }
    ctx.tilemap->AddLight(light);
    char line[160];
    std::snprintf(line,
                  sizeof(line),
                  "light.add: #%zu at (%.1f, %.1f) color=(%.2f, %.2f, %.2f) radius=%.1f %s",
                  ctx.tilemap->GetLights().size() - 1,
                  static_cast<double>(light.position.x),
                  static_cast<double>(light.position.y),
                  static_cast<double>(light.color.r),
                  static_cast<double>(light.color.g),
                  static_cast<double>(light.color.b),
                  static_cast<double>(light.radius),
                  std::string(EnumTraits<LightSchedule>::ToString(light.schedule)).c_str());
    ctx.out.Print(line);
    return true;
}

bool Cmd_LightClear(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.tilemap == nullptr)
    {
        ctx.out.PrintError("light.clear: tilemap unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("light.clear: usage 'light.clear' (no args)");
        return false;
    }
    size_t before = ctx.tilemap->GetLights().size();
    ctx.tilemap->ClearLights();
    char line[64];
    std::snprintf(line, sizeof(line), "light.clear: removed %zu light(s)", before);
    ctx.out.Print(line);
    return true;
}

bool Cmd_LightList(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.tilemap == nullptr)
    {
        ctx.out.PrintError("light.list: tilemap unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("light.list: usage 'light.list' (no args)");
        return false;
    }
    const auto& lights = ctx.tilemap->GetLights();
    char header[64];
    std::snprintf(header, sizeof(header), "light.list: %zu light(s)", lights.size());
    ctx.out.Print(header);
    for (size_t i = 0; i < lights.size(); ++i)
    {
        const WorldLight& l = lights[i];
        char line[160];
        std::snprintf(line,
                      sizeof(line),
                      "  #%zu (%.1f, %.1f) color=(%.2f, %.2f, %.2f) radius=%.1f %s",
                      i,
                      static_cast<double>(l.position.x),
                      static_cast<double>(l.position.y),
                      static_cast<double>(l.color.r),
                      static_cast<double>(l.color.g),
                      static_cast<double>(l.color.b),
                      static_cast<double>(l.radius),
                      std::string(EnumTraits<LightSchedule>::ToString(l.schedule)).c_str());
        ctx.out.Print(line);
    }
    return true;
}

bool Cmd_LightRemove(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.tilemap == nullptr)
    {
        ctx.out.PrintError("light.remove: tilemap unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("light.remove: usage 'light.remove <index>'");
        return false;
    }
    int idx = -1;
    if (!ParseInt(args[0], idx) || idx < 0)
    {
        ctx.out.PrintError("light.remove: index must be a non-negative integer");
        return false;
    }
    if (!ctx.tilemap->RemoveLight(static_cast<size_t>(idx)))
    {
        ctx.out.PrintError("light.remove: index out of range");
        return false;
    }
    char line[64];
    std::snprintf(line, sizeof(line), "light.remove: removed #%d", idx);
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// time.status - print time, period, weather, day count, moon phase
// ============================================================================

bool Cmd_TimeStatus(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.time == nullptr)
    {
        ctx.out.PrintError("time.status: time manager unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("time.status: usage 'time.status'");
        return false;
    }
    const char* periodName = "Unknown";
    switch (ctx.time->GetTimePeriod())
    {
        case TimePeriod::Dawn:
            periodName = "Dawn";
            break;
        case TimePeriod::Morning:
            periodName = "Morning";
            break;
        case TimePeriod::Midday:
            periodName = "Midday";
            break;
        case TimePeriod::Afternoon:
            periodName = "Afternoon";
            break;
        case TimePeriod::Dusk:
            periodName = "Dusk";
            break;
        case TimePeriod::Evening:
            periodName = "Evening";
            break;
        case TimePeriod::Night:
            periodName = "Night";
            break;
        case TimePeriod::LateNight:
            periodName = "LateNight";
            break;
    }
    std::string weatherName{EnumTraits<WeatherState>::ToString(ctx.time->GetWeather())};
    char line[224];
    std::snprintf(
        line,
        sizeof(line),
        "time.status: %.2fh (%s) weather=%s intensity=%.2f day=%d moonPhase=%d scale=%.2f%s",
        static_cast<double>(ctx.time->GetTimeOfDay()),
        periodName,
        weatherName.c_str(),
        static_cast<double>(ctx.time->GetWeatherIntensity()),
        ctx.time->GetDayCount(),
        ctx.time->GetMoonPhase(),
        static_cast<double>(ctx.time->GetTimeScale()),
        ctx.time->IsPaused() ? " [paused]" : "");
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// particle.spawn <type> <worldX> <worldY> - one-shot spawn at world pixels
// ============================================================================

bool Cmd_ParticleSpawn(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.particles == nullptr)
    {
        ctx.out.PrintError("particle.spawn: particle system unavailable");
        return false;
    }
    if (args.size() != 3)
    {
        ctx.out.PrintError("particle.spawn: usage 'particle.spawn <type> <worldX> <worldY>'");
        return false;
    }
    const auto parsed = EnumTraits<ParticleType>::FromString(args[0]);
    if (!parsed.has_value())
    {
        std::string valid;
        for (auto name : EnumTraits<ParticleType>::Names)
        {
            if (!valid.empty())
                valid += ' ';
            valid.append(name);
        }
        ctx.out.PrintError(std::string("particle.spawn: unknown type '") + std::string(args[0]) +
                           "' (valid: " + valid + ")");
        return false;
    }
    float wx = 0.0f;
    float wy = 0.0f;
    if (!ParseFloat(args[1], wx) || !ParseFloat(args[2], wy))
    {
        ctx.out.PrintError("particle.spawn: world coords must be finite numbers");
        return false;
    }
    ctx.particles->SpawnOne(*parsed, glm::vec2(wx, wy));
    char line[112];
    std::snprintf(line,
                  sizeof(line),
                  "particle.spawn: %s at (%.1f, %.1f)",
                  std::string(EnumTraits<ParticleType>::ToString(*parsed)).c_str(),
                  static_cast<double>(wx),
                  static_cast<double>(wy));
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// particle.list - count particles per type, list zones
// ============================================================================

bool Cmd_ParticleList(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.particles == nullptr)
    {
        ctx.out.PrintError("particle.list: particle system unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("particle.list: usage 'particle.list'");
        return false;
    }
    const auto& particles = ctx.particles->GetParticles();
    int counts[EnumTraits<ParticleType>::Count] = {};
    for (const auto& p : particles)
    {
        const int idx = static_cast<int>(p.type);
        if (idx >= 0 && idx < static_cast<int>(EnumTraits<ParticleType>::Count))
        {
            ++counts[idx];
        }
    }
    char header[48];
    std::snprintf(header,
                  sizeof(header),
                  "particle.list: %zu active",
                  static_cast<std::size_t>(particles.size()));
    ctx.out.Print(header);
    for (std::size_t i = 0; i < EnumTraits<ParticleType>::Count; ++i)
    {
        if (counts[i] > 0)
        {
            char line[80];
            std::snprintf(line,
                          sizeof(line),
                          "  %s: %d",
                          std::string(EnumTraits<ParticleType>::Names[i]).c_str(),
                          counts[i]);
            ctx.out.Print(line);
        }
    }
    if (ctx.tilemap != nullptr)
    {
        const std::vector<ParticleZone>* zones = ctx.tilemap->GetParticleZones();
        const std::size_t zoneCount = (zones != nullptr) ? zones->size() : 0;
        char zoneHeader[48];
        std::snprintf(zoneHeader, sizeof(zoneHeader), "  zones: %zu", zoneCount);
        ctx.out.Print(zoneHeader);
    }
    return true;
}

// ============================================================================
// particle.kill_all
// ============================================================================

bool Cmd_ParticleKillAll(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.particles == nullptr)
    {
        ctx.out.PrintError("particle.kill_all: particle system unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("particle.kill_all: usage 'particle.kill_all'");
        return false;
    }
    ctx.particles->Clear();
    ctx.out.Print("particle.kill_all: cleared");
    return true;
}

// ============================================================================
// camera.freecam [on|off|toggle]
// ============================================================================

bool Cmd_CameraFreecam(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.camera == nullptr)
    {
        ctx.out.PrintError("camera.freecam: camera unavailable");
        return false;
    }
    bool target = false;
    if (!ParseToggleArg(args, ctx.camera->GetState().freeMode, "camera.freecam", ctx.out, target))
    {
        return false;
    }
    ctx.camera->GetState().freeMode = target;
    ctx.out.Print(std::string("camera.freecam: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// camera.zoom <factor> - clamp into a sensible developer range
// ============================================================================

bool Cmd_CameraZoom(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.camera == nullptr)
    {
        ctx.out.PrintError("camera.zoom: camera unavailable");
        return false;
    }
    if (args.size() != 1)
    {
        ctx.out.PrintError("camera.zoom: usage 'camera.zoom <factor 0.1-10.0>'");
        return false;
    }
    float z = 0.0f;
    if (!ParseFloat(args[0], z))
    {
        ctx.out.PrintError("camera.zoom: factor must be a finite number");
        return false;
    }
    if (z < 0.1f || z > 10.0f)
    {
        ctx.out.PrintError("camera.zoom: factor out of range [0.1, 10.0]");
        return false;
    }
    ctx.camera->GetState().zoom = z;
    char line[48];
    std::snprintf(line, sizeof(line), "camera.zoom: %.3f", static_cast<double>(z));
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// camera.follow [on|off|toggle] - re-attach camera to player; clears freecam.
// ============================================================================

bool Cmd_CameraFollow(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.camera == nullptr)
    {
        ctx.out.PrintError("camera.follow: camera unavailable");
        return false;
    }
    bool target = false;
    if (!ParseToggleArg(
            args, ctx.camera->GetState().hasFollowTarget, "camera.follow", ctx.out, target))
    {
        return false;
    }
    ctx.camera->GetState().hasFollowTarget = target;
    if (target)
    {
        ctx.camera->GetState().freeMode = false;
    }
    ctx.out.Print(std::string("camera.follow: ") + (target ? "ON" : "OFF"));
    return true;
}

// ============================================================================
// camera.info - dump camera state
// ============================================================================

bool Cmd_CameraInfo(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.camera == nullptr)
    {
        ctx.out.PrintError("camera.info: camera unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("camera.info: usage 'camera.info'");
        return false;
    }
    const auto& s = ctx.camera->GetState();
    char line[192];
    std::snprintf(line,
                  sizeof(line),
                  "camera.info: pos=(%.1f, %.1f) zoom=%.3f tilt=%.2f globe3D=%s freecam=%s "
                  "follow=%s target=(%.1f, %.1f)",
                  static_cast<double>(s.position.x),
                  static_cast<double>(s.position.y),
                  static_cast<double>(s.zoom),
                  static_cast<double>(s.tilt),
                  s.enable3DEffect ? "ON" : "OFF",
                  s.freeMode ? "ON" : "OFF",
                  s.hasFollowTarget ? "ON" : "OFF",
                  static_cast<double>(s.followTarget.x),
                  static_cast<double>(s.followTarget.y));
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// map.save [path] - default path comes from Game::GetSaveMapPath
// ============================================================================

bool Cmd_MapSave(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.tilemap == nullptr || ctx.npcs == nullptr || ctx.player == nullptr)
    {
        ctx.out.PrintError("map.save: world refs unavailable");
        return false;
    }
    if (args.size() > 1)
    {
        ctx.out.PrintError("map.save: usage 'map.save [path]'");
        return false;
    }
    std::string path;
    if (args.empty())
    {
        if (ctx.game == nullptr)
        {
            ctx.out.PrintError("map.save: game ref required for default path");
            return false;
        }
        path = ctx.game->GetSaveMapPath();
    }
    else
    {
        path = std::string(args[0]);
    }
    const glm::vec2 ppos = ctx.player->GetPosition();
    const int playerTileX = static_cast<int>(std::floor(ppos.x / CONSOLE_TILE_SIZE));
    const int playerTileY = static_cast<int>(std::floor((ppos.y - 0.1f) / CONSOLE_TILE_SIZE));
    const int characterType = static_cast<int>(ctx.player->GetCharacterType());
    if (!ctx.tilemap->SaveMapToJSON(path, ctx.npcs, playerTileX, playerTileY, characterType))
    {
        ctx.out.PrintError("map.save: failed to write '" + path + "'");
        return false;
    }
    ctx.out.Print("map.save: wrote '" + path + "'");
    return true;
}

// ============================================================================
// map.size
// ============================================================================

bool Cmd_MapSize(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.tilemap == nullptr)
    {
        ctx.out.PrintError("map.size: tilemap unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("map.size: usage 'map.size'");
        return false;
    }
    const int w = ctx.tilemap->GetMapWidth();
    const int h = ctx.tilemap->GetMapHeight();
    const int tw = ctx.tilemap->GetTileWidth();
    const int th = ctx.tilemap->GetTileHeight();
    char line[112];
    std::snprintf(line,
                  sizeof(line),
                  "map.size: %d x %d tiles (%d x %d px), tile=%dx%d",
                  w,
                  h,
                  w * tw,
                  h * th,
                  tw,
                  th);
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// map.collision <tileX> <tileY>
// ============================================================================

bool Cmd_MapCollision(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.tilemap == nullptr)
    {
        ctx.out.PrintError("map.collision: tilemap unavailable");
        return false;
    }
    if (args.size() != 2)
    {
        ctx.out.PrintError("map.collision: usage 'map.collision <tileX> <tileY>'");
        return false;
    }
    int tx = 0;
    int ty = 0;
    if (!ParseInt(args[0], tx) || !ParseInt(args[1], ty))
    {
        ctx.out.PrintError("map.collision: tile coords must be non-negative integers");
        return false;
    }
    const bool blocked = ctx.tilemap->GetTileCollision(tx, ty);
    char line[80];
    std::snprintf(line,
                  sizeof(line),
                  "map.collision: tile (%d, %d) = %s",
                  tx,
                  ty,
                  blocked ? "BLOCKED" : "open");
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// perf - print FPS, frame time, draw calls
// ============================================================================

// ============================================================================
// console.copy - put the entire scrollback buffer onto the OS clipboard
// ============================================================================

bool Cmd_ConsoleCopy(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (!args.empty())
    {
        ctx.out.PrintError("console.copy: usage 'console.copy' (no args)");
        return false;
    }
    if (ctx.game == nullptr)
    {
        ctx.out.PrintError("console.copy: game unavailable (cannot reach window)");
        return false;
    }
    GLFWwindow* window = ctx.game->GetWindow();
    if (window == nullptr)
    {
        ctx.out.PrintError("console.copy: window unavailable");
        return false;
    }

    // Concatenate every line in the scrollback buffer with newlines so the
    // clipboard text reads as the user saw it in the console.
    const auto& lines = ctx.out.Lines();
    std::string blob;
    size_t reserveSize = 0;
    for (const auto& line : lines)
        reserveSize += line.text.size() + 1;
    blob.reserve(reserveSize);
    for (const auto& line : lines)
    {
        blob.append(line.text);
        blob.push_back('\n');
    }

    glfwSetClipboardString(window, blob.c_str());

    char status[96];
    std::snprintf(status,
                  sizeof(status),
                  "console.copy: %zu lines (%zu chars) copied to clipboard",
                  lines.size(),
                  blob.size());
    ctx.out.Print(status);
    return true;
}

// ============================================================================
// renderer.trace - capture and dump per-frame draw-call trace
// ============================================================================

bool Cmd_RendererTrace(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (args.size() > 1)
    {
        ctx.out.PrintError("renderer.trace: usage 'renderer.trace [on|off|dump|clear]'");
        return false;
    }

    auto printStatus = [&]
    {
        char line[96];
        std::snprintf(line,
                      sizeof(line),
                      "renderer.trace: capture=%s, last frame events=%zu",
                      DrawTracer::IsEnabled() ? "ON" : "off",
                      DrawTracer::LastFrameEvents().size());
        ctx.out.Print(line);
    };

    auto dump = [&]
    {
        const auto& events = DrawTracer::LastFrameEvents();
        if (events.empty())
        {
            ctx.out.Print(
                "renderer.trace: no events captured. Enable with 'renderer.trace on' "
                "and wait one frame, then re-run.");
            return;
        }
        char header[64];
        std::snprintf(header, sizeof(header), "-- last frame: %zu events --", events.size());
        ctx.out.Print(header);
        int prevDraws = 0;
        for (size_t i = 0; i < events.size(); ++i)
        {
            const auto& e = events[i];
            int delta = e.drawCount - prevDraws;
            char line[256];
            std::snprintf(
                line, sizeof(line), "[%4d draws +%d] %s", e.drawCount, delta, e.label.c_str());
            ctx.out.Print(line);
            prevDraws = e.drawCount;
        }
    };

    if (args.empty())
    {
        // No-arg form: print status + dump if anything captured.
        printStatus();
        if (!DrawTracer::LastFrameEvents().empty())
            dump();
        return true;
    }

    const std::string_view sub = args[0];
    if (sub == "on")
    {
        DrawTracer::SetEnabled(true);
        ctx.out.Print("renderer.trace: capture ON (run 'renderer.trace dump' next frame)");
        return true;
    }
    if (sub == "off")
    {
        DrawTracer::SetEnabled(false);
        ctx.out.Print("renderer.trace: capture off, buffers cleared");
        return true;
    }
    if (sub == "dump")
    {
        dump();
        return true;
    }
    if (sub == "clear")
    {
        DrawTracer::Clear();
        ctx.out.Print("renderer.trace: cleared");
        return true;
    }
    ctx.out.PrintError("renderer.trace: unknown subcommand (try on|off|dump|clear)");
    return false;
}

bool Cmd_Perf(std::span<const std::string_view> args, CommandContext& ctx)
{
    if (ctx.game == nullptr)
    {
        ctx.out.PrintError("perf: game unavailable");
        return false;
    }
    if (!args.empty())
    {
        ctx.out.PrintError("perf: usage 'perf'");
        return false;
    }
    const FPSCounter& fps = ctx.game->GetFPSCounter();
    const float frameMs = (fps.currentFps > 0.0f) ? (1000.0f / fps.currentFps) : 0.0f;
    char line[128];
    std::snprintf(line,
                  sizeof(line),
                  "perf: fps=%.1f frame=%.2fms drawCalls=%d target=%.1f",
                  static_cast<double>(fps.currentFps),
                  static_cast<double>(frameMs),
                  fps.currentDrawCalls,
                  static_cast<double>(fps.targetFps));
    ctx.out.Print(line);
    return true;
}

// ============================================================================
// Console::RegisterDefaultCommands - production wiring. Lives here so all
// the default commands and their bindings are in one translation unit.
// ============================================================================

void Console::RegisterDefaultCommands()
{
    auto makeContext = [this]() -> CommandContext
    {
        return CommandContext{
            /* out         */ m_Buffer,
            /* player      */ &m_Game.m_Player,
            /* gameState   */ &m_Game.m_GameState,
            /* time        */ &m_Game.m_TimeManager,
            /* tilemap     */ &m_Game.m_Tilemap,
            /* npcs        */ &m_Game.m_NPCs,
            /* registry    */ &m_Registry,
            /* editor      */ &m_Game.m_Editor,
            /* camera      */ &m_Game.m_Camera,
            /* renderer    */ m_Game.m_Renderer.get(),
            /* game        */ &m_Game,
            /* postFXEnabled */ &m_Game.m_PostFXEnabled,
            /* dialogue    */ &m_Game.m_DialogueManager,
            /* particles   */ &m_Game.m_Particles,
        };
    };

    m_Registry.Register("help",
                        "list available commands",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_Help(args, ctx);
                        });

    m_Registry.Register("clear",
                        "clear scrollback",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_Clear(args, ctx);
                        },
                        {"cls"});

    m_Registry.Register("teleport",
                        "teleport <tx> <ty> - move player to tile coord",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_Teleport(args, ctx);
                        },
                        {"tp"});

    m_Registry.Register("flag.set",
                        "flag.set <name> <value> - set a game state flag",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_FlagSet(args, ctx);
                        });

    m_Registry.Register("flag.get",
                        "flag.get <name> - print a game state flag",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_FlagGet(args, ctx);
                        });

    m_Registry.Register("time.set",
                        "time.set <hours> - set in-game time (0.0-24.0)",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_TimeSet(args, ctx);
                        },
                        {"ts"});

    m_Registry.Register("time.freeze",
                        "[on|off|toggle] - pause/resume day-night cycle",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_TimeFreeze(args, ctx);
                        },
                        {"tm.freeze", "tf"});

    m_Registry.Register("map.load",
                        "map.load <filename> - switch maps",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_MapLoad(args, ctx);
                        });

    m_Registry.Register("state.dump",
                        "state.dump - print player tile, time, NPC count, quests",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_StateDump(args, ctx);
                        });

    m_Registry.Register("noclip",
                        "noclip [on|off] - toggle player tile/NPC collision",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_NoClip(args, ctx);
                        },
                        {"nc"});

    m_Registry.Register("editor",
                        "[on|off|toggle] - toggle level editor mode",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_Editor(args, ctx);
                        },
                        {"ed"});

    m_Registry.Register("appearance.copy",
                        "copy appearance from nearest NPC within 32px",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_AppearanceCopy(args, ctx);
                        },
                        {"appr.copy", "mimic"});

    m_Registry.Register("appearance.restore",
                        "restore original character appearance",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_AppearanceRestore(args, ctx);
                        },
                        {"appr.restore", "unmimic"});

    m_Registry.Register(
        "character.set",
        "<BW1_MALE|BW1_FEMALE|BW2_MALE|BW2_FEMALE|CC_FEMALE> - switch player character",
        [makeContext](auto args, Console&)
        {
            CommandContext ctx = makeContext();
            (void)Cmd_CharacterSet(args, ctx);
        },
        {"char.set", "cs"});

    m_Registry.Register("character.next",
                        "cycle to next player character",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_CharacterNext(args, ctx);
                        },
                        {"char.next", "cn"});

    m_Registry.Register("renderer.set",
                        "<opengl|vulkan> - switch renderer at runtime",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_RendererSet(args, ctx);
                        },
                        {"rndr.set", "gfx"});

    m_Registry.Register("debug.info",
                        "[on|off|toggle] - toggle FPS/coords HUD",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_DebugInfo(args, ctx);
                        },
                        {"dbg.info", "fps"});

    m_Registry.Register("debug.overlays",
                        "[on|off|toggle] - toggle collision/nav/anchor overlays",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_DebugOverlays(args, ctx);
                        },
                        {"dbg.overlays", "dbg"});

    m_Registry.Register("globe",
                        "[on|off|toggle] - toggle 3D globe perspective",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_Globe(args, ctx);
                        });

    m_Registry.Register("time.next",
                        "advance to next time-of-day preset",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_TimeNext(args, ctx);
                        },
                        {"tm.next", "tn"});

    m_Registry.Register("globe.radius",
                        "<50.0-500.0> - set 3D globe radius",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_GlobeRadius(args, ctx);
                        },
                        {"glb.r", "globe.r", "gr"});

    m_Registry.Register("globe.tilt",
                        "<0.0-1.0> - set 3D camera tilt",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_GlobeTilt(args, ctx);
                        },
                        {"glb.t", "globe.t", "gt"});

    m_Registry.Register("globe.intensity",
                        "<up|down> - step globe radius+tilt together",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_GlobeIntensity(args, ctx);
                        },
                        {"glb.i", "globe.i", "gi"});

    m_Registry.Register("postfx",
                        "[on|off|toggle] - master toggle for bloom/grading/vignette/grain",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_PostFX(args, ctx);
                        },
                        {"fx", "pfx"});

    m_Registry.Register("player.speed",
                        "[multiplier] - speedhack: scale player movement (1.0 = normal)",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_PlayerSpeed(args, ctx);
                        },
                        {"pspeed", "speed", "psp"});

    m_Registry.Register("player.pos",
                        "print player tile, world pixel, and facing direction",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_PlayerPos(args, ctx);
                        },
                        {"pos", "ppos"});

    m_Registry.Register("player.bicycle",
                        "[on|off|toggle] - toggle bicycle mode",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_PlayerBicycle(args, ctx);
                        },
                        {"bike"});

    m_Registry.Register("player.run",
                        "[on|off|toggle] - toggle running mode",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_PlayerRun(args, ctx);
                        });

    m_Registry.Register("npc.list",
                        "list NPCs (idx, name, type, tile, AI state)",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_NpcList(args, ctx);
                        },
                        {"npcs"});

    m_Registry.Register("npc.tp",
                        "<idx> <tx> <ty> - teleport NPC by vector index",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_NpcTp(args, ctx);
                        });

    m_Registry.Register("npc.spawn",
                        "<type> <tx> <ty> - spawn an NPC at tile coords",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_NpcSpawn(args, ctx);
                        });

    m_Registry.Register("npc.despawn",
                        "<idx> - remove NPC by index (refuses if in dialogue)",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_NpcDespawn(args, ctx);
                        });

    m_Registry.Register("npc.freeze",
                        "<idx|all> [on|off|toggle] - halt NPC AI",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_NpcFreeze(args, ctx);
                        });

    m_Registry.Register("npc.dialog",
                        "<idx> <text...> - set NPC simple dialogue text",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_NpcDialog(args, ctx);
                        });

    m_Registry.Register("dialogue.active",
                        "print current dialogue node and visible options",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_DialogueActive(args, ctx);
                        },
                        {"dlg"});

    m_Registry.Register("dialogue.end",
                        "force-close any active dialogue (simple or tree)",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_DialogueEnd(args, ctx);
                        },
                        {"dlg.end"});

    m_Registry.Register("dialogue.skip",
                        "advance tree dialogue (confirm current selection)",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_DialogueSkip(args, ctx);
                        },
                        {"dlg.skip"});

    m_Registry.Register("flag.list",
                        "dump every game-state flag",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_FlagList(args, ctx);
                        },
                        {"flags"});

    m_Registry.Register("flag.unset",
                        "<name> - remove a flag",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_FlagUnset(args, ctx);
                        });

    m_Registry.Register("time.scale",
                        "<multiplier> - set time progression scale (1.0 = normal)",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_TimeScale(args, ctx);
                        });

    m_Registry.Register(
        "time.weather",
        "<name> - set weather state (Tab to list)",
        [makeContext](auto args, Console&)
        {
            CommandContext ctx = makeContext();
            (void)Cmd_TimeWeather(args, ctx);
        },
        {"weather"},
        [](std::size_t argIndex) -> std::vector<std::string>
        {
            if (argIndex != 0)
                return {};
            std::vector<std::string> out;
            out.reserve(EnumTraits<WeatherState>::Count);
            for (auto name : EnumTraits<WeatherState>::Names)
                out.emplace_back(name);
            return out;
        });

    m_Registry.Register("weather.intensity",
                        "<0.0-1.0> - set weather density/effect strength",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_WeatherIntensity(args, ctx);
                        });

    m_Registry.Register("weather.next",
                        "cycle to next weather state",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_WeatherNext(args, ctx);
                        });

    m_Registry.Register("weather.random",
                        "set a random weather state",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_WeatherRandom(args, ctx);
                        });

    m_Registry.Register(
        "light.add",
        "<x> <y> [r g b] [radius] [schedule] - place a WorldLight",
        [makeContext](auto args, Console&)
        {
            CommandContext ctx = makeContext();
            (void)Cmd_LightAdd(args, ctx);
        },
        {},
        [](std::size_t argIndex) -> std::vector<std::string>
        {
            // Schedule is the 7th positional arg (index 6) when present.
            if (argIndex != 6)
                return {};
            std::vector<std::string> out;
            out.reserve(EnumTraits<LightSchedule>::Count);
            for (auto name : EnumTraits<LightSchedule>::Names)
                out.emplace_back(name);
            return out;
        });

    m_Registry.Register("light.clear",
                        "remove all WorldLights",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_LightClear(args, ctx);
                        });

    m_Registry.Register("light.list",
                        "list all WorldLights with index, position, color, radius, schedule",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_LightList(args, ctx);
                        });

    m_Registry.Register("light.remove",
                        "<index> - remove a WorldLight by index",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_LightRemove(args, ctx);
                        });

    m_Registry.Register("time.status",
                        "print time, period, weather, day count, moon phase",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_TimeStatus(args, ctx);
                        });

    m_Registry.Register(
        "particle.spawn",
        "<type> <wx> <wy> - spawn particle at world pixel coords",
        [makeContext](auto args, Console&)
        {
            CommandContext ctx = makeContext();
            (void)Cmd_ParticleSpawn(args, ctx);
        },
        {},
        [](std::size_t argIndex) -> std::vector<std::string>
        {
            // Only the first positional arg has canned values
            // (the particle type); world coords are open-ended.
            if (argIndex != 0)
            {
                return {};
            }
            std::vector<std::string> out;
            out.reserve(EnumTraits<ParticleType>::Count);
            for (auto name : EnumTraits<ParticleType>::Names)
            {
                out.emplace_back(name);
            }
            return out;
        });

    m_Registry.Register("particle.list",
                        "count active particles by type, list zones",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_ParticleList(args, ctx);
                        });

    m_Registry.Register("particle.kill_all",
                        "remove all active particles",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_ParticleKillAll(args, ctx);
                        });

    m_Registry.Register("camera.freecam",
                        "[on|off|toggle] - decouple camera from player",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_CameraFreecam(args, ctx);
                        },
                        {"freecam"});

    m_Registry.Register("camera.zoom",
                        "<factor 0.1-10.0> - set camera zoom",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_CameraZoom(args, ctx);
                        },
                        {"zoom"});

    m_Registry.Register("camera.follow",
                        "[on|off|toggle] - re-attach camera to player",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_CameraFollow(args, ctx);
                        });

    m_Registry.Register("camera.info",
                        "dump camera state (pos, zoom, freecam, follow, tilt)",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_CameraInfo(args, ctx);
                        });

    m_Registry.Register("map.save",
                        "[path] - save current map to JSON",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_MapSave(args, ctx);
                        });

    m_Registry.Register("map.size",
                        "print map dimensions in tiles and pixels",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_MapSize(args, ctx);
                        });

    m_Registry.Register("map.collision",
                        "<tx> <ty> - query collision flag at tile",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_MapCollision(args, ctx);
                        });

    m_Registry.Register("perf",
                        "print FPS, frame time, draw calls",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_Perf(args, ctx);
                        });

    m_Registry.Register("renderer.trace",
                        "[on|off|dump|clear] - capture and dump per-frame draw-call trace",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_RendererTrace(args, ctx);
                        },
                        {"draws.trace"});

    m_Registry.Register("console.copy",
                        "copy entire console scrollback to OS clipboard",
                        [makeContext](auto args, Console&)
                        {
                            CommandContext ctx = makeContext();
                            (void)Cmd_ConsoleCopy(args, ctx);
                        },
                        {"copy"});
}
