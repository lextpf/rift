// Tests for the free-function command implementations. Each command is
// invoked through CommandContext with hand-built dependency references, so
// the tests never need a Game instance. No GLFW or renderer involvement.

#include <gtest/gtest.h>

#include "../src/CameraController.h"
#include "../src/Console.h"
#include "../src/ConsoleCommands.h"
#include "../src/Editor.h"
#include "../src/GameStateManager.h"
#include "../src/NonPlayerCharacter.h"
#include "../src/PlayerCharacter.h"
#include "../src/TimeManager.h"

#include <string>
#include <string_view>
#include <vector>

namespace
{
/// Build a span<string_view> from string literals, keeping the underlying
/// strings alive in @p storage so the views remain valid for the call.
struct ArgPack
{
    std::vector<std::string> storage;
    std::vector<std::string_view> views;

    explicit ArgPack(std::initializer_list<std::string_view> args)
    {
        storage.reserve(args.size());
        views.reserve(args.size());
        for (auto a : args)
        {
            storage.emplace_back(a);
        }
        for (const auto& s : storage)
        {
            views.emplace_back(s);
        }
    }

    [[nodiscard]] std::span<const std::string_view> span() const
    {
        return std::span<const std::string_view>(views.data(), views.size());
    }
};

/// True if any line in the buffer contains @p needle.
bool BufferContains(const ConsoleBuffer& buf, std::string_view needle)
{
    for (const auto& line : buf.Lines())
    {
        if (line.text.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}
}  // namespace

// ---------------------------------------------------------------------------
// help
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, HelpListsRegisteredCommands)
{
    ConsoleCommandRegistry reg;
    reg.Register("alpha", "first", [](auto, Console&) {});
    reg.Register("bravo", "second", [](auto, Console&) {});

    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.registry = &reg;

    ArgPack args({});
    EXPECT_TRUE(Cmd_Help(args.span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "alpha"));
    EXPECT_TRUE(BufferContains(buf, "bravo"));
    EXPECT_TRUE(BufferContains(buf, "first"));
    EXPECT_TRUE(BufferContains(buf, "second"));
}

TEST(ConsoleCommandsTests, HelpFailsWithoutRegistry)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ArgPack args({});
    EXPECT_FALSE(Cmd_Help(args.span(), ctx));
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, ClearEmptiesScrollback)
{
    ConsoleBuffer buf;
    buf.Print("dirty");
    buf.Print("dirty");
    CommandContext ctx{buf};
    ArgPack args({});
    EXPECT_TRUE(Cmd_Clear(args.span(), ctx));
    EXPECT_TRUE(buf.Lines().empty());
}

// ---------------------------------------------------------------------------
// teleport
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, TeleportUpdatesPlayerTilePosition)
{
    PlayerCharacter player;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    ArgPack args({"7", "11"});
    EXPECT_TRUE(Cmd_Teleport(args.span(), ctx));

    // SetTilePosition snaps feet to bottom-center of tile (16px tiles):
    //   x = 7*16 + 8 = 120, y = 11*16 + 16 = 192
    EXPECT_FLOAT_EQ(player.GetPosition().x, 120.0f);
    EXPECT_FLOAT_EQ(player.GetPosition().y, 192.0f);
}

TEST(ConsoleCommandsTests, TeleportRejectsBadArgs)
{
    PlayerCharacter player;
    const glm::vec2 before = player.GetPosition();
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    // Wrong arg count
    {
        ArgPack args({"5"});
        EXPECT_FALSE(Cmd_Teleport(args.span(), ctx));
    }
    // Non-numeric
    {
        ArgPack args({"foo", "bar"});
        EXPECT_FALSE(Cmd_Teleport(args.span(), ctx));
    }
    EXPECT_EQ(player.GetPosition(), before);
}

TEST(ConsoleCommandsTests, TeleportFailsWithoutPlayer)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ArgPack args({"1", "2"});
    EXPECT_FALSE(Cmd_Teleport(args.span(), ctx));
}

// ---------------------------------------------------------------------------
// flag.set / flag.get
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, FlagSetGetRoundtrip)
{
    GameStateManager state;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.gameState = &state;

    ArgPack setArgs({"unlocked", "true"});
    EXPECT_TRUE(Cmd_FlagSet(setArgs.span(), ctx));
    EXPECT_TRUE(state.HasFlag("unlocked"));
    EXPECT_EQ(state.GetFlagValue("unlocked"), "true");

    buf.Clear();
    ArgPack getArgs({"unlocked"});
    EXPECT_TRUE(Cmd_FlagGet(getArgs.span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "unlocked"));
    EXPECT_TRUE(BufferContains(buf, "true"));
}

TEST(ConsoleCommandsTests, FlagGetUnsetReportsClearly)
{
    GameStateManager state;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.gameState = &state;

    ArgPack args({"missing"});
    EXPECT_TRUE(Cmd_FlagGet(args.span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "<unset>"));
}

TEST(ConsoleCommandsTests, FlagSetWrongArityRejected)
{
    GameStateManager state;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.gameState = &state;

    ArgPack args({"only_name"});
    EXPECT_FALSE(Cmd_FlagSet(args.span(), ctx));
    EXPECT_FALSE(state.HasFlag("only_name"));
}

// ---------------------------------------------------------------------------
// time.set
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, TimeSetUpdatesTimeOfDay)
{
    TimeManager time;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    ArgPack args({"18.5"});
    EXPECT_TRUE(Cmd_TimeSet(args.span(), ctx));
    EXPECT_FLOAT_EQ(time.GetTimeOfDay(), 18.5f);
}

TEST(ConsoleCommandsTests, TimeSetRejectsNonNumeric)
{
    TimeManager time;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    ArgPack args({"noon"});
    EXPECT_FALSE(Cmd_TimeSet(args.span(), ctx));
}

// ---------------------------------------------------------------------------
// state.dump (read-only summary)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// noclip
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, NoClipDefaultArgTogglesState)
{
    PlayerCharacter player;
    ASSERT_FALSE(player.IsNoClip());
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    ArgPack none({});
    EXPECT_TRUE(Cmd_NoClip(none.span(), ctx));
    EXPECT_TRUE(player.IsNoClip());

    EXPECT_TRUE(Cmd_NoClip(none.span(), ctx));
    EXPECT_FALSE(player.IsNoClip());
}

TEST(ConsoleCommandsTests, NoClipExplicitOnOff)
{
    PlayerCharacter player;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    ArgPack on({"on"});
    EXPECT_TRUE(Cmd_NoClip(on.span(), ctx));
    EXPECT_TRUE(player.IsNoClip());

    ArgPack off({"off"});
    EXPECT_TRUE(Cmd_NoClip(off.span(), ctx));
    EXPECT_FALSE(player.IsNoClip());
}

TEST(ConsoleCommandsTests, NoClipRejectsBadArg)
{
    PlayerCharacter player;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    ArgPack bogus({"maybe"});
    EXPECT_FALSE(Cmd_NoClip(bogus.span(), ctx));
    EXPECT_FALSE(player.IsNoClip());
}

TEST(ConsoleCommandsTests, StateDumpPrintsSummary)
{
    PlayerCharacter player;
    player.SetTilePosition(3, 4);
    GameStateManager state;
    state.SetFlag("done", true);
    TimeManager time;
    time.SetTime(7.0f);
    std::vector<NonPlayerCharacter> npcs;  // empty is fine for the dump

    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;
    ctx.gameState = &state;
    ctx.time = &time;
    ctx.npcs = &npcs;

    ArgPack args({});
    EXPECT_TRUE(Cmd_StateDump(args.span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "player"));
    EXPECT_TRUE(BufferContains(buf, "time"));
    EXPECT_TRUE(BufferContains(buf, "npcs"));
}

// ---------------------------------------------------------------------------
// editor [on|off|toggle]
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, EditorEmptyArgTogglesActive)
{
    Editor editor;
    ASSERT_FALSE(editor.IsActive());
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.editor = &editor;

    EXPECT_TRUE(Cmd_Editor(ArgPack({}).span(), ctx));
    EXPECT_TRUE(editor.IsActive());
    EXPECT_TRUE(editor.IsShowTilePicker());

    EXPECT_TRUE(Cmd_Editor(ArgPack({}).span(), ctx));
    EXPECT_FALSE(editor.IsActive());
    EXPECT_FALSE(editor.IsShowTilePicker());
}

TEST(ConsoleCommandsTests, EditorExplicitOnOff)
{
    Editor editor;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.editor = &editor;

    EXPECT_TRUE(Cmd_Editor(ArgPack({"on"}).span(), ctx));
    EXPECT_TRUE(editor.IsActive());
    EXPECT_TRUE(Cmd_Editor(ArgPack({"on"}).span(), ctx));  // idempotent
    EXPECT_TRUE(editor.IsActive());

    EXPECT_TRUE(Cmd_Editor(ArgPack({"off"}).span(), ctx));
    EXPECT_FALSE(editor.IsActive());

    EXPECT_TRUE(Cmd_Editor(ArgPack({"toggle"}).span(), ctx));
    EXPECT_TRUE(editor.IsActive());
}

TEST(ConsoleCommandsTests, EditorRejectsBadArg)
{
    Editor editor;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.editor = &editor;

    EXPECT_FALSE(Cmd_Editor(ArgPack({"bogus"}).span(), ctx));
    EXPECT_FALSE(editor.IsActive());
}

TEST(ConsoleCommandsTests, EditorFailsWithoutEditorRef)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_Editor(ArgPack({}).span(), ctx));
}

// ---------------------------------------------------------------------------
// debug.info / debug.overlays
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, DebugInfoToggle)
{
    Editor editor;
    ASSERT_FALSE(editor.IsShowDebugInfo());
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.editor = &editor;

    EXPECT_TRUE(Cmd_DebugInfo(ArgPack({}).span(), ctx));
    EXPECT_TRUE(editor.IsShowDebugInfo());

    EXPECT_TRUE(Cmd_DebugInfo(ArgPack({"off"}).span(), ctx));
    EXPECT_FALSE(editor.IsShowDebugInfo());

    EXPECT_FALSE(Cmd_DebugInfo(ArgPack({"weird"}).span(), ctx));
}

TEST(ConsoleCommandsTests, DebugOverlaysMirrorsAnchorVisibility)
{
    Editor editor;
    ASSERT_FALSE(editor.IsDebugMode());
    ASSERT_FALSE(editor.IsShowNoProjectionAnchors());
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.editor = &editor;

    EXPECT_TRUE(Cmd_DebugOverlays(ArgPack({"on"}).span(), ctx));
    EXPECT_TRUE(editor.IsDebugMode());
    EXPECT_TRUE(editor.IsShowNoProjectionAnchors());  // mirrored by SetDebugMode

    EXPECT_TRUE(Cmd_DebugOverlays(ArgPack({"off"}).span(), ctx));
    EXPECT_FALSE(editor.IsDebugMode());
    EXPECT_FALSE(editor.IsShowNoProjectionAnchors());
}

// ---------------------------------------------------------------------------
// globe / globe.radius / globe.tilt / globe.intensity
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, GlobeToggle)
{
    CameraController cam;
    ASSERT_FALSE(cam.Is3DEnabled());
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.camera = &cam;

    EXPECT_TRUE(Cmd_Globe(ArgPack({"on"}).span(), ctx));
    EXPECT_TRUE(cam.Is3DEnabled());

    EXPECT_TRUE(Cmd_Globe(ArgPack({}).span(), ctx));  // toggle by default
    EXPECT_FALSE(cam.Is3DEnabled());
}

TEST(ConsoleCommandsTests, GlobeRadiusSetsAndRejectsOutOfRange)
{
    CameraController cam;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.camera = &cam;

    EXPECT_TRUE(Cmd_GlobeRadius(ArgPack({"123.5"}).span(), ctx));
    EXPECT_FLOAT_EQ(cam.GetState().globeSphereRadius, 123.5f);

    EXPECT_FALSE(Cmd_GlobeRadius(ArgPack({"600"}).span(), ctx));
    EXPECT_FLOAT_EQ(cam.GetState().globeSphereRadius, 123.5f);  // unchanged

    EXPECT_FALSE(Cmd_GlobeRadius(ArgPack({"-5"}).span(), ctx));
    EXPECT_FALSE(Cmd_GlobeRadius(ArgPack({"abc"}).span(), ctx));
    EXPECT_FALSE(Cmd_GlobeRadius(ArgPack({}).span(), ctx));
}

TEST(ConsoleCommandsTests, GlobeTiltSetsAndRejectsOutOfRange)
{
    CameraController cam;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.camera = &cam;

    EXPECT_TRUE(Cmd_GlobeTilt(ArgPack({"0.5"}).span(), ctx));
    EXPECT_FLOAT_EQ(cam.GetState().tilt, 0.5f);

    EXPECT_FALSE(Cmd_GlobeTilt(ArgPack({"1.5"}).span(), ctx));
    EXPECT_FALSE(Cmd_GlobeTilt(ArgPack({"-0.1"}).span(), ctx));
}

TEST(ConsoleCommandsTests, GlobeIntensityUpDownCouples)
{
    CameraController cam;
    cam.GetState().globeSphereRadius = 200.0f;
    cam.GetState().tilt = 0.2f;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.camera = &cam;

    EXPECT_TRUE(Cmd_GlobeIntensity(ArgPack({"up"}).span(), ctx));
    EXPECT_FLOAT_EQ(cam.GetState().globeSphereRadius, 210.0f);
    EXPECT_FLOAT_EQ(cam.GetState().tilt, 0.15f);

    EXPECT_TRUE(Cmd_GlobeIntensity(ArgPack({"down"}).span(), ctx));
    EXPECT_FLOAT_EQ(cam.GetState().globeSphereRadius, 200.0f);
    EXPECT_FLOAT_EQ(cam.GetState().tilt, 0.2f);

    EXPECT_FALSE(Cmd_GlobeIntensity(ArgPack({"sideways"}).span(), ctx));
}

TEST(ConsoleCommandsTests, GlobeIntensityClampsAtBoundaries)
{
    CameraController cam;
    cam.GetState().globeSphereRadius = 500.0f;
    cam.GetState().tilt = 0.0f;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.camera = &cam;

    EXPECT_TRUE(Cmd_GlobeIntensity(ArgPack({"up"}).span(), ctx));
    EXPECT_FLOAT_EQ(cam.GetState().globeSphereRadius, 500.0f);  // capped
    EXPECT_FLOAT_EQ(cam.GetState().tilt, 0.0f);                 // floored

    cam.GetState().globeSphereRadius = 50.0f;
    cam.GetState().tilt = 1.0f;
    EXPECT_TRUE(Cmd_GlobeIntensity(ArgPack({"down"}).span(), ctx));
    EXPECT_FLOAT_EQ(cam.GetState().globeSphereRadius, 50.0f);  // floored
    EXPECT_FLOAT_EQ(cam.GetState().tilt, 1.0f);                // capped
}

// ---------------------------------------------------------------------------
// time.next
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, TimeNextAdvancesThroughPresets)
{
    TimeManager time;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    // Function-local static persists across tests; roll until we land on a
    // known phase by looking at the printed name, then advance one and verify
    // the next preset matches the documented cycle.
    std::vector<std::string_view> presets = {"Dawn (06:00)",
                                             "Morning (08:30)",
                                             "Midday (13:00)",
                                             "Afternoon (17:00)",
                                             "Dusk (19:00)",
                                             "Evening (21:00)",
                                             "Night (01:00)",
                                             "Late Night (04:30)"};
    // Advance up to 8 times, recording the phase at each step. The cycle is
    // periodic with period 8, so within 9 calls we always see at least one
    // wrap and can verify the consecutive-pair invariant.
    std::vector<size_t> seen;
    for (int i = 0; i < 9; ++i)
    {
        buf.Clear();
        EXPECT_TRUE(Cmd_TimeNext(ArgPack({}).span(), ctx));
        for (size_t k = 0; k < presets.size(); ++k)
        {
            if (BufferContains(buf, presets[k]))
            {
                seen.push_back(k);
                break;
            }
        }
    }
    ASSERT_EQ(seen.size(), 9u);
    for (size_t i = 1; i < seen.size(); ++i)
    {
        const size_t expected = (seen[i - 1] + 1) % presets.size();
        EXPECT_EQ(seen[i], expected) << "step " << i << " did not advance by one";
    }
}

TEST(ConsoleCommandsTests, TimeNextRejectsArgs)
{
    TimeManager time;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    EXPECT_FALSE(Cmd_TimeNext(ArgPack({"now"}).span(), ctx));
}

// ---------------------------------------------------------------------------
// character.set / character.next (parse paths only - sprite assets may be
// missing in the test working dir, so SwitchCharacter success is best-effort)
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, CharacterSetUnknownNameRejected)
{
    PlayerCharacter player;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    EXPECT_FALSE(Cmd_CharacterSet(ArgPack({"NOT_A_THING"}).span(), ctx));
    EXPECT_FALSE(Cmd_CharacterSet(ArgPack({}).span(), ctx));  // wrong arity
    EXPECT_FALSE(Cmd_CharacterSet(ArgPack({"a", "b"}).span(), ctx));
}

TEST(ConsoleCommandsTests, CharacterNextRejectsArgs)
{
    PlayerCharacter player;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;
    EXPECT_FALSE(Cmd_CharacterNext(ArgPack({"foo"}).span(), ctx));
}

// ---------------------------------------------------------------------------
// appearance.copy / appearance.restore (failure paths only - success path
// requires a live IRenderer for UploadTextures and is integration-only)
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, AppearanceCopyFailsWithoutRefs)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_AppearanceCopy(ArgPack({}).span(), ctx));
}

TEST(ConsoleCommandsTests, AppearanceRestoreFailsWithoutRefs)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_AppearanceRestore(ArgPack({}).span(), ctx));
}

// ---------------------------------------------------------------------------
// postfx [on|off|toggle]
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, PostFXTogglesViaPointer)
{
    bool flag = true;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.postFXEnabled = &flag;

    EXPECT_TRUE(Cmd_PostFX(ArgPack({}).span(), ctx));  // toggle
    EXPECT_FALSE(flag);

    EXPECT_TRUE(Cmd_PostFX(ArgPack({"on"}).span(), ctx));
    EXPECT_TRUE(flag);

    EXPECT_TRUE(Cmd_PostFX(ArgPack({"off"}).span(), ctx));
    EXPECT_FALSE(flag);

    EXPECT_FALSE(Cmd_PostFX(ArgPack({"weird"}).span(), ctx));
    EXPECT_FALSE(flag);
}

TEST(ConsoleCommandsTests, PostFXFailsWithoutPointer)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_PostFX(ArgPack({}).span(), ctx));
}

// renderer.set is intentionally not unit-tested. Cmd_RendererSet calls
// Game::SwitchRenderer, whose definition lives in Game.cpp (not in the test
// link). The success path requires a live Game + GLFW window + renderer
// factory, so this command is integration-only.
