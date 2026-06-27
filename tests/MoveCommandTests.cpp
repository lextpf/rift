// Tests for the move.* console commands. CommandContext is hand-built; no Game,
// no GLFW, no renderer.
#include <gtest/gtest.h>

#include "../src/CameraController.hpp"
#include "../src/Console.hpp"
#include "../src/ConsoleCommands.hpp"
#include "../src/EntityStore.hpp"
#include "../src/Motor.hpp"

#include <ecs.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
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

TEST(MoveCommandTests, AccelSetsValue)
{
    ecs::registry world;
    ecs::entity player = EntityStore::SpawnPlayer(world);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &world;
    ctx.playerEntity = player;

    ArgPack args({"350"});
    EXPECT_TRUE(Cmd_MoveAccel(args.span(), ctx));
    EXPECT_NEAR(world.get<Motor>(player).params.accel, 350.0f, 1e-3f);
}

TEST(MoveCommandTests, AccelRejectsNonPositive)
{
    ecs::registry world;
    ecs::entity player = EntityStore::SpawnPlayer(world);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &world;
    ctx.playerEntity = player;

    ArgPack args({"0"});
    EXPECT_FALSE(Cmd_MoveAccel(args.span(), ctx));
}

TEST(MoveCommandTests, DecelSetsValue)
{
    ecs::registry world;
    ecs::entity player = EntityStore::SpawnPlayer(world);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &world;
    ctx.playerEntity = player;

    ArgPack args({"275"});
    EXPECT_TRUE(Cmd_MoveDecel(args.span(), ctx));
    EXPECT_NEAR(world.get<Motor>(player).params.decel, 275.0f, 1e-3f);
}

TEST(MoveCommandTests, LookaheadSetsValue)
{
    CameraController cam;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.camera = &cam;

    ArgPack args({"20"});
    EXPECT_TRUE(Cmd_MoveLookahead(args.span(), ctx));
    EXPECT_NEAR(cam.GetLookAheadDistance(), 20.0f, 1e-3f);
}

TEST(MoveCommandTests, DumpReportsValues)
{
    ecs::registry world;
    ecs::entity player = EntityStore::SpawnPlayer(world);
    CameraController cam;
    world.get<Motor>(player).params.accel = 123.0f;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.npcs = &world;
    ctx.playerEntity = player;
    ctx.camera = &cam;

    ArgPack args({});
    EXPECT_TRUE(Cmd_MoveDump(args.span(), ctx));
    EXPECT_TRUE(BufferContains(buf, "123"));
}
