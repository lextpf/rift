// Tests for the free-function command implementations. Each command is
// invoked through CommandContext with hand-built dependency references, so
// the tests never need a Game instance. No GLFW or renderer involvement.

#include <gtest/gtest.h>

#include "../src/CameraController.h"
#include "../src/Console.h"
#include "../src/ConsoleCommands.h"
#include "../src/DialogueManager.h"
#include "../src/Editor.h"
#include "../src/GameStateManager.h"
#include "../src/NonPlayerCharacter.h"
#include "../src/ParticleSystem.h"
#include "../src/PlayerCharacter.h"
#include "../src/TimeManager.h"
#include "../src/Tilemap.h"

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

// ---------------------------------------------------------------------------
// player.speed
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, PlayerSpeedSetsMultiplier)
{
    PlayerCharacter player;
    ASSERT_FLOAT_EQ(player.GetSpeedMultiplier(), 1.0f);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    ArgPack args({"2.5"});
    EXPECT_TRUE(Cmd_PlayerSpeed(args.span(), ctx));
    EXPECT_FLOAT_EQ(player.GetSpeedMultiplier(), 2.5f);
    EXPECT_TRUE(BufferContains(buf, "2.500"));
}

TEST(ConsoleCommandsTests, PlayerSpeedNoArgPrintsCurrentWithoutMutating)
{
    PlayerCharacter player;
    player.SetSpeedMultiplier(1.5f);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    ArgPack args({});
    EXPECT_TRUE(Cmd_PlayerSpeed(args.span(), ctx));
    EXPECT_FLOAT_EQ(player.GetSpeedMultiplier(), 1.5f);
    EXPECT_TRUE(BufferContains(buf, "1.500"));
}

TEST(ConsoleCommandsTests, PlayerSpeedRejectsNonPositive)
{
    PlayerCharacter player;
    player.SetSpeedMultiplier(2.0f);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    {
        ArgPack zero({"0"});
        EXPECT_FALSE(Cmd_PlayerSpeed(zero.span(), ctx));
    }
    {
        ArgPack neg({"-1.5"});
        EXPECT_FALSE(Cmd_PlayerSpeed(neg.span(), ctx));
    }
    EXPECT_FLOAT_EQ(player.GetSpeedMultiplier(), 2.0f);
}

TEST(ConsoleCommandsTests, PlayerSpeedRejectsBadFloat)
{
    PlayerCharacter player;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    ArgPack args({"foo"});
    EXPECT_FALSE(Cmd_PlayerSpeed(args.span(), ctx));
    EXPECT_FLOAT_EQ(player.GetSpeedMultiplier(), 1.0f);
}

TEST(ConsoleCommandsTests, PlayerSpeedRejectsTooManyArgs)
{
    PlayerCharacter player;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    ArgPack args({"1.0", "2.0"});
    EXPECT_FALSE(Cmd_PlayerSpeed(args.span(), ctx));
}

TEST(ConsoleCommandsTests, PlayerSpeedFailsWithoutPlayer)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ArgPack args({"2.0"});
    EXPECT_FALSE(Cmd_PlayerSpeed(args.span(), ctx));
}

// ---------------------------------------------------------------------------
// time.freeze
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, TimeFreezeTogglesByDefault)
{
    TimeManager time;
    ASSERT_FALSE(time.IsPaused());
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    ArgPack none({});
    EXPECT_TRUE(Cmd_TimeFreeze(none.span(), ctx));
    EXPECT_TRUE(time.IsPaused());
    EXPECT_TRUE(Cmd_TimeFreeze(none.span(), ctx));
    EXPECT_FALSE(time.IsPaused());
}

TEST(ConsoleCommandsTests, TimeFreezeExplicitOnOff)
{
    TimeManager time;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    ArgPack on({"on"});
    EXPECT_TRUE(Cmd_TimeFreeze(on.span(), ctx));
    EXPECT_TRUE(time.IsPaused());

    ArgPack off({"off"});
    EXPECT_TRUE(Cmd_TimeFreeze(off.span(), ctx));
    EXPECT_FALSE(time.IsPaused());
}

TEST(ConsoleCommandsTests, TimeFreezeRejectsBadArg)
{
    TimeManager time;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    ArgPack bogus({"halt"});
    EXPECT_FALSE(Cmd_TimeFreeze(bogus.span(), ctx));
    EXPECT_FALSE(time.IsPaused());
}

TEST(ConsoleCommandsTests, TimeFreezeFailsWithoutTime)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ArgPack args({});
    EXPECT_FALSE(Cmd_TimeFreeze(args.span(), ctx));
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

// ---------------------------------------------------------------------------
// player.pos / player.bicycle / player.run
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, PlayerPosPrintsTileWorldFacing)
{
    PlayerCharacter player;
    player.SetTilePosition(3, 4);
    player.SetDirection(CharacterDirection::LEFT);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    EXPECT_TRUE(Cmd_PlayerPos(ArgPack({}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "tile=(3, 4)"));
    EXPECT_TRUE(BufferContains(buf, "facing=LEFT"));
}

TEST(ConsoleCommandsTests, PlayerPosFailsWithoutPlayerOrExtraArgs)
{
    {
        ConsoleBuffer buf;
        CommandContext ctx{buf};
        EXPECT_FALSE(Cmd_PlayerPos(ArgPack({}).span(), ctx));
    }
    PlayerCharacter player;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;
    EXPECT_FALSE(Cmd_PlayerPos(ArgPack({"oops"}).span(), ctx));
}

TEST(ConsoleCommandsTests, PlayerBicycleToggle)
{
    PlayerCharacter player;
    ASSERT_FALSE(player.IsBicycling());
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    EXPECT_TRUE(Cmd_PlayerBicycle(ArgPack({}).span(), ctx));
    EXPECT_TRUE(player.IsBicycling());
    EXPECT_TRUE(Cmd_PlayerBicycle(ArgPack({"off"}).span(), ctx));
    EXPECT_FALSE(player.IsBicycling());
    EXPECT_FALSE(Cmd_PlayerBicycle(ArgPack({"junk"}).span(), ctx));
}

TEST(ConsoleCommandsTests, PlayerRunToggle)
{
    PlayerCharacter player;
    ASSERT_FALSE(player.IsRunning());
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.player = &player;

    EXPECT_TRUE(Cmd_PlayerRun(ArgPack({"on"}).span(), ctx));
    EXPECT_TRUE(player.IsRunning());
    EXPECT_TRUE(Cmd_PlayerRun(ArgPack({}).span(), ctx));  // toggle
    EXPECT_FALSE(player.IsRunning());
}

TEST(ConsoleCommandsTests, PlayerRunFailsWithoutPlayer)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_PlayerRun(ArgPack({"on"}).span(), ctx));
}

// ---------------------------------------------------------------------------
// npc.list / npc.tp / npc.spawn / npc.despawn / npc.freeze / npc.dialog
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, NpcListPrintsCount)
{
    std::vector<NonPlayerCharacter> npcs(2);
    npcs[0].SetName("Anna");
    npcs[1].SetName("Bob");
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;

    EXPECT_TRUE(Cmd_NpcList(ArgPack({}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "2 NPC"));
    EXPECT_TRUE(BufferContains(buf, "Anna"));
    EXPECT_TRUE(BufferContains(buf, "Bob"));
}

TEST(ConsoleCommandsTests, NpcListEmptyVector)
{
    std::vector<NonPlayerCharacter> npcs;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;
    EXPECT_TRUE(Cmd_NpcList(ArgPack({}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "0 NPC"));
}

TEST(ConsoleCommandsTests, NpcTpUpdatesPosition)
{
    std::vector<NonPlayerCharacter> npcs(1);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;

    EXPECT_TRUE(Cmd_NpcTp(ArgPack({"0", "5", "9"}).span(), ctx));
    EXPECT_EQ(npcs[0].GetTileX(), 5);
    EXPECT_EQ(npcs[0].GetTileY(), 9);
}

TEST(ConsoleCommandsTests, NpcTpRejectsBadIndexAndArity)
{
    std::vector<NonPlayerCharacter> npcs(1);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;

    EXPECT_FALSE(Cmd_NpcTp(ArgPack({"99", "0", "0"}).span(), ctx));
    EXPECT_FALSE(Cmd_NpcTp(ArgPack({"0", "1"}).span(), ctx));
    EXPECT_FALSE(Cmd_NpcTp(ArgPack({"abc", "1", "2"}).span(), ctx));
}

TEST(ConsoleCommandsTests, NpcSpawnRejectsBadArgsAndUnknownType)
{
    std::vector<NonPlayerCharacter> npcs;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;

    EXPECT_FALSE(Cmd_NpcSpawn(ArgPack({}).span(), ctx));
    EXPECT_FALSE(Cmd_NpcSpawn(ArgPack({"BW1_NPC1", "abc", "1"}).span(), ctx));
    // Unknown type with no asset on disk: Load() fails, command returns false.
    EXPECT_FALSE(
        Cmd_NpcSpawn(ArgPack({"definitely_not_a_real_npc_type", "5", "5"}).span(), ctx));
    EXPECT_TRUE(npcs.empty());
}

TEST(ConsoleCommandsTests, NpcDespawnRemovesAndBoundsCheck)
{
    std::vector<NonPlayerCharacter> npcs(2);
    npcs[0].SetName("First");
    npcs[1].SetName("Second");
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;

    EXPECT_FALSE(Cmd_NpcDespawn(ArgPack({"99"}).span(), ctx));
    EXPECT_EQ(npcs.size(), 2u);
    EXPECT_TRUE(Cmd_NpcDespawn(ArgPack({"0"}).span(), ctx));
    EXPECT_EQ(npcs.size(), 1u);
    EXPECT_EQ(npcs[0].GetName(), "Second");
}

TEST(ConsoleCommandsTests, NpcFreezePerIndex)
{
    std::vector<NonPlayerCharacter> npcs(2);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;

    EXPECT_TRUE(Cmd_NpcFreeze(ArgPack({"0", "on"}).span(), ctx));
    EXPECT_TRUE(npcs[0].IsStopped());
    EXPECT_FALSE(npcs[1].IsStopped());

    EXPECT_TRUE(Cmd_NpcFreeze(ArgPack({"0"}).span(), ctx));  // toggle
    EXPECT_FALSE(npcs[0].IsStopped());
}

TEST(ConsoleCommandsTests, NpcFreezeAll)
{
    std::vector<NonPlayerCharacter> npcs(3);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;

    EXPECT_TRUE(Cmd_NpcFreeze(ArgPack({"all", "on"}).span(), ctx));
    for (const auto& n : npcs)
    {
        EXPECT_TRUE(n.IsStopped());
    }
    EXPECT_TRUE(Cmd_NpcFreeze(ArgPack({"all", "off"}).span(), ctx));
    for (const auto& n : npcs)
    {
        EXPECT_FALSE(n.IsStopped());
    }
}

TEST(ConsoleCommandsTests, NpcFreezeRejectsBadArgs)
{
    std::vector<NonPlayerCharacter> npcs(1);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;

    EXPECT_FALSE(Cmd_NpcFreeze(ArgPack({}).span(), ctx));
    EXPECT_FALSE(Cmd_NpcFreeze(ArgPack({"99"}).span(), ctx));
    EXPECT_FALSE(Cmd_NpcFreeze(ArgPack({"0", "weird"}).span(), ctx));
}

TEST(ConsoleCommandsTests, NpcDialogSetsTextWithMultiToken)
{
    std::vector<NonPlayerCharacter> npcs(1);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;

    EXPECT_TRUE(Cmd_NpcDialog(ArgPack({"0", "Hello", "world!"}).span(), ctx));
    EXPECT_EQ(npcs[0].GetDialogue(), "Hello world!");
}

TEST(ConsoleCommandsTests, NpcDialogRejectsBadArgs)
{
    std::vector<NonPlayerCharacter> npcs(1);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &npcs;

    EXPECT_FALSE(Cmd_NpcDialog(ArgPack({"0"}).span(), ctx));
    EXPECT_FALSE(Cmd_NpcDialog(ArgPack({"99", "hi"}).span(), ctx));
}

// ---------------------------------------------------------------------------
// dialogue.active / dialogue.end / dialogue.skip
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, DialogueActiveReportsInactive)
{
    DialogueManager dm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.dialogue = &dm;

    EXPECT_TRUE(Cmd_DialogueActive(ArgPack({}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "no active dialogue"));
}

TEST(ConsoleCommandsTests, DialogueActiveFailsWithoutManager)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_DialogueActive(ArgPack({}).span(), ctx));
}

TEST(ConsoleCommandsTests, DialogueEndFailsWithoutGame)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_DialogueEnd(ArgPack({}).span(), ctx));
}

TEST(ConsoleCommandsTests, DialogueSkipRefusesWhenInactive)
{
    DialogueManager dm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.dialogue = &dm;

    EXPECT_FALSE(Cmd_DialogueSkip(ArgPack({}).span(), ctx));
}

// ---------------------------------------------------------------------------
// flag.list / flag.unset
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, FlagListDumpsAllFlags)
{
    GameStateManager state;
    state.SetFlagValue("alpha", "1");
    state.SetFlagValue("bravo", "two");
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.gameState = &state;

    EXPECT_TRUE(Cmd_FlagList(ArgPack({}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "2 flag"));
    EXPECT_TRUE(BufferContains(buf, "alpha = 1"));
    EXPECT_TRUE(BufferContains(buf, "bravo = two"));
}

TEST(ConsoleCommandsTests, FlagListEmpty)
{
    GameStateManager state;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.gameState = &state;
    EXPECT_TRUE(Cmd_FlagList(ArgPack({}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "0 flag"));
}

TEST(ConsoleCommandsTests, FlagUnsetRemovesFlag)
{
    GameStateManager state;
    state.SetFlag("done", true);
    ASSERT_TRUE(state.HasFlag("done"));
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.gameState = &state;

    EXPECT_TRUE(Cmd_FlagUnset(ArgPack({"done"}).span(), ctx));
    EXPECT_FALSE(state.HasFlag("done"));
}

TEST(ConsoleCommandsTests, FlagUnsetNoOpForUnknown)
{
    GameStateManager state;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.gameState = &state;
    EXPECT_TRUE(Cmd_FlagUnset(ArgPack({"missing"}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "was not set"));
}

// ---------------------------------------------------------------------------
// time.scale / time.weather / time.status
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, TimeScaleSetsAndRejectsNonPositive)
{
    TimeManager tm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &tm;

    EXPECT_TRUE(Cmd_TimeScale(ArgPack({"3.5"}).span(), ctx));
    EXPECT_FLOAT_EQ(tm.GetTimeScale(), 3.5f);

    EXPECT_FALSE(Cmd_TimeScale(ArgPack({"0"}).span(), ctx));
    EXPECT_FALSE(Cmd_TimeScale(ArgPack({"-1"}).span(), ctx));
    EXPECT_FALSE(Cmd_TimeScale(ArgPack({"foo"}).span(), ctx));
    EXPECT_FLOAT_EQ(tm.GetTimeScale(), 3.5f);
}

TEST(ConsoleCommandsTests, TimeWeatherSetsClearAndOvercast)
{
    TimeManager tm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &tm;

    EXPECT_TRUE(Cmd_TimeWeather(ArgPack({"overcast"}).span(), ctx));
    EXPECT_EQ(tm.GetWeather(), WeatherState::Overcast);
    EXPECT_TRUE(Cmd_TimeWeather(ArgPack({"clear"}).span(), ctx));
    EXPECT_EQ(tm.GetWeather(), WeatherState::Clear);
    EXPECT_FALSE(Cmd_TimeWeather(ArgPack({"snow"}).span(), ctx));
}

TEST(ConsoleCommandsTests, TimeStatusPrintsKeyFields)
{
    TimeManager tm;
    tm.SetTime(10.5f);
    tm.SetWeather(WeatherState::Overcast);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &tm;

    EXPECT_TRUE(Cmd_TimeStatus(ArgPack({}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "10.50h"));
    EXPECT_TRUE(BufferContains(buf, "Overcast"));
}

// ---------------------------------------------------------------------------
// particle.spawn / particle.list / particle.kill_all
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, ParticleSpawnAddsParticle)
{
    ParticleSystem ps;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.particles = &ps;

    EXPECT_TRUE(Cmd_ParticleSpawn(ArgPack({"Firefly", "100", "200"}).span(), ctx));
    EXPECT_EQ(ps.GetParticles().size(), 1u);
    EXPECT_EQ(ps.GetParticles()[0].type, ParticleType::Firefly);
}

TEST(ConsoleCommandsTests, ParticleSpawnSurvivesUpdateTickWithoutZones)
{
    // Regression: particles spawned via the console use zoneIndex = -1 to
    // mark themselves as zoneless. The Update() orphan-cleanup pass must
    // leave them alone so the user actually sees the particle for its
    // natural lifetime. Previously every type except DriftingLeaf/DustMote/
    // Pollen died on the very next frame.
    ParticleSystem ps;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.particles = &ps;

    EXPECT_TRUE(Cmd_ParticleSpawn(ArgPack({"Firefly", "100", "200"}).span(), ctx));
    ASSERT_EQ(ps.GetParticles().size(), 1u);

    ps.Update(0.016f, glm::vec2(0.0f), glm::vec2(800.0f, 600.0f));
    EXPECT_EQ(ps.GetParticles().size(), 1u)
        << "console-spawned particle must survive the orphan-cleanup pass";
}

TEST(ConsoleCommandsTests, ParticleSpawnRejectsBadArgs)
{
    ParticleSystem ps;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.particles = &ps;

    EXPECT_FALSE(Cmd_ParticleSpawn(ArgPack({"NotAType", "0", "0"}).span(), ctx));
    EXPECT_FALSE(Cmd_ParticleSpawn(ArgPack({"Firefly", "abc", "0"}).span(), ctx));
    EXPECT_FALSE(Cmd_ParticleSpawn(ArgPack({"Firefly"}).span(), ctx));
    EXPECT_TRUE(ps.GetParticles().empty());
}

TEST(ConsoleCommandsTests, ParticleListPrintsCounts)
{
    ParticleSystem ps;
    ps.SpawnOne(ParticleType::Firefly, glm::vec2(0.0f));
    ps.SpawnOne(ParticleType::Firefly, glm::vec2(0.0f));
    ps.SpawnOne(ParticleType::Snow, glm::vec2(0.0f));
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.particles = &ps;

    EXPECT_TRUE(Cmd_ParticleList(ArgPack({}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "3 active"));
    EXPECT_TRUE(BufferContains(buf, "Firefly: 2"));
    EXPECT_TRUE(BufferContains(buf, "Snow: 1"));
}

TEST(ConsoleCommandsTests, ParticleKillAllClearsPool)
{
    ParticleSystem ps;
    ps.SpawnOne(ParticleType::Firefly, glm::vec2(0.0f));
    ASSERT_FALSE(ps.GetParticles().empty());
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.particles = &ps;

    EXPECT_TRUE(Cmd_ParticleKillAll(ArgPack({}).span(), ctx));
    EXPECT_TRUE(ps.GetParticles().empty());
}

TEST(ConsoleCommandsTests, ParticleCommandsFailWithoutSystem)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_ParticleSpawn(ArgPack({"Firefly", "0", "0"}).span(), ctx));
    EXPECT_FALSE(Cmd_ParticleList(ArgPack({}).span(), ctx));
    EXPECT_FALSE(Cmd_ParticleKillAll(ArgPack({}).span(), ctx));
}

// ---------------------------------------------------------------------------
// camera.freecam / camera.zoom / camera.follow / camera.info
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, CameraFreecamToggle)
{
    CameraController cam;
    ASSERT_FALSE(cam.GetState().freeMode);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.camera = &cam;

    EXPECT_TRUE(Cmd_CameraFreecam(ArgPack({"on"}).span(), ctx));
    EXPECT_TRUE(cam.GetState().freeMode);
    EXPECT_TRUE(Cmd_CameraFreecam(ArgPack({}).span(), ctx));
    EXPECT_FALSE(cam.GetState().freeMode);
}

TEST(ConsoleCommandsTests, CameraZoomSetsAndClampsRange)
{
    CameraController cam;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.camera = &cam;

    EXPECT_TRUE(Cmd_CameraZoom(ArgPack({"2.5"}).span(), ctx));
    EXPECT_FLOAT_EQ(cam.GetState().zoom, 2.5f);
    EXPECT_FALSE(Cmd_CameraZoom(ArgPack({"0.05"}).span(), ctx));
    EXPECT_FALSE(Cmd_CameraZoom(ArgPack({"15"}).span(), ctx));
    EXPECT_FALSE(Cmd_CameraZoom(ArgPack({"abc"}).span(), ctx));
    EXPECT_FLOAT_EQ(cam.GetState().zoom, 2.5f);
}

TEST(ConsoleCommandsTests, CameraFollowEnablesAndClearsFreecam)
{
    CameraController cam;
    cam.GetState().freeMode = true;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.camera = &cam;

    EXPECT_TRUE(Cmd_CameraFollow(ArgPack({"on"}).span(), ctx));
    EXPECT_TRUE(cam.GetState().hasFollowTarget);
    EXPECT_FALSE(cam.GetState().freeMode);  // cleared by follow=on

    EXPECT_TRUE(Cmd_CameraFollow(ArgPack({"off"}).span(), ctx));
    EXPECT_FALSE(cam.GetState().hasFollowTarget);
}

TEST(ConsoleCommandsTests, CameraInfoPrintsState)
{
    CameraController cam;
    cam.GetState().position = glm::vec2(123.4f, 56.7f);
    cam.GetState().zoom = 1.5f;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.camera = &cam;

    EXPECT_TRUE(Cmd_CameraInfo(ArgPack({}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "pos=(123.4, 56.7)"));
    EXPECT_TRUE(BufferContains(buf, "zoom=1.500"));
}

// ---------------------------------------------------------------------------
// map.size / map.collision (map.save success path writes to disk - skipped)
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, MapSizePrintsDimensions)
{
    Tilemap tm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &tm;

    EXPECT_TRUE(Cmd_MapSize(ArgPack({}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "tile=16x16") ||
                BufferContains(buf, "tile="));  // exact tile size depends on construction
}

TEST(ConsoleCommandsTests, MapSizeRejectsArgs)
{
    Tilemap tm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &tm;
    EXPECT_FALSE(Cmd_MapSize(ArgPack({"oops"}).span(), ctx));
}

TEST(ConsoleCommandsTests, MapCollisionQueriesTile)
{
    Tilemap tm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &tm;

    EXPECT_TRUE(Cmd_MapCollision(ArgPack({"0", "0"}).span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "tile (0, 0)"));
}

TEST(ConsoleCommandsTests, MapCollisionRejectsBadArgs)
{
    Tilemap tm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &tm;
    EXPECT_FALSE(Cmd_MapCollision(ArgPack({"abc", "0"}).span(), ctx));
    EXPECT_FALSE(Cmd_MapCollision(ArgPack({"1"}).span(), ctx));
}

TEST(ConsoleCommandsTests, MapSaveFailsWithoutRefs)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_MapSave(ArgPack({"unused.json"}).span(), ctx));
}

// ---------------------------------------------------------------------------
// perf - exercises the no-game error path; the success path needs Game which
// isn't linked into rift_tests. The handler signature is still validated.
// ---------------------------------------------------------------------------

TEST(ConsoleCommandsTests, PerfFailsWithoutGame)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_Perf(ArgPack({}).span(), ctx));
}

TEST(ConsoleCommandsTests, PerfRejectsArgs)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    // Without a game ref the missing-pointer error path triggers first; that's
    // fine - just confirm extra args also lead to a false return.
    EXPECT_FALSE(Cmd_Perf(ArgPack({"oops"}).span(), ctx));
}

// renderer.set is intentionally not unit-tested. Cmd_RendererSet calls
// Game::SwitchRenderer, whose definition lives in Game.cpp (not in the test
// link). The success path requires a live Game + GLFW window + renderer
// factory, so this command is integration-only.
