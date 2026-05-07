#pragma once

#include <span>
#include <string_view>
#include <vector>

class CameraController;
class ConsoleBuffer;
class ConsoleCommandRegistry;
class Editor;
class Game;
class GameStateManager;
class IRenderer;
class NonPlayerCharacter;
class PlayerCharacter;
class TimeManager;
class Tilemap;

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
    PlayerCharacter* player = nullptr;
    GameStateManager* gameState = nullptr;
    TimeManager* time = nullptr;
    Tilemap* tilemap = nullptr;
    std::vector<NonPlayerCharacter>* npcs = nullptr;
    const ConsoleCommandRegistry* registry = nullptr;
    Editor* editor = nullptr;            ///< Level editor state and toggles.
    CameraController* camera = nullptr;  ///< Camera (3D effect, globe radius/tilt).
    IRenderer* renderer = nullptr;       ///< Active renderer for texture uploads.
    Game* game = nullptr;                ///< Game (cross-cutting ops like renderer.set).
    bool* postFXEnabled = nullptr;       ///< Master toggle for post-processing pipeline.
};

/// @name Default console commands
/// @brief Free functions implementing each built-in command.
///
/// All return `true` on success, `false` on error (the error message has
/// already been printed to `ctx.out`). Tests call these directly with a
/// hand-built CommandContext; the production wiring goes through
/// Console::RegisterDefaultCommands.
/// @{
bool Cmd_Help(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_Clear(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_Teleport(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_FlagSet(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_FlagGet(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TimeSet(std::span<const std::string_view> args, CommandContext& ctx);
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
bool Cmd_Globe(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_TimeNext(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_GlobeRadius(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_GlobeTilt(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_GlobeIntensity(std::span<const std::string_view> args, CommandContext& ctx);
bool Cmd_PostFX(std::span<const std::string_view> args, CommandContext& ctx);
/// @}
