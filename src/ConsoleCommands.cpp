#include "ConsoleCommands.h"

#include "CameraController.h"
#include "Console.h"
#include "Editor.h"
#include "EnumTraits.h"
#include "Game.h"
#include "GameStateManager.h"
#include "IRenderer.h"
#include "NonPlayerCharacter.h"
#include "PlayerCharacter.h"
#include "Tilemap.h"
#include "TimeManager.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
                        });

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
                        {"pspeed", "speed"});
}
