// Tests for ConsoleCommandRegistry: name registration, lookup, and prefix
// matching used by tab completion. No GLFW or renderer involvement.

#include <gtest/gtest.h>

#include "../src/Console.h"

#include <string>

namespace
{
ConsoleCommandRegistry::Handler NoOp()
{
    return [](std::span<const std::string_view>, Console&) {};
}
}  // namespace

TEST(ConsoleCommandRegistryTests, RegisterAndLookup)
{
    ConsoleCommandRegistry r;
    r.Register("teleport", "tp", NoOp());
    const auto* cmd = r.Lookup("teleport");
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->name, "teleport");
    EXPECT_EQ(cmd->description, "tp");
}

TEST(ConsoleCommandRegistryTests, LookupReturnsNullForUnknown)
{
    ConsoleCommandRegistry r;
    EXPECT_EQ(r.Lookup("nope"), nullptr);
}

TEST(ConsoleCommandRegistryTests, EmptyNameRejected)
{
    ConsoleCommandRegistry r;
    r.Register("", "x", NoOp());
    EXPECT_TRUE(r.All().empty());
}

TEST(ConsoleCommandRegistryTests, ReregisterOverwrites)
{
    ConsoleCommandRegistry r;
    r.Register("ping", "first", NoOp());
    r.Register("ping", "second", NoOp());
    ASSERT_EQ(r.All().size(), 1u);
    EXPECT_EQ(r.Lookup("ping")->description, "second");
}

TEST(ConsoleCommandRegistryTests, MatchPrefixReturnsAlphabeticalSubset)
{
    ConsoleCommandRegistry r;
    r.Register("flag.set", "", NoOp());
    r.Register("flag.get", "", NoOp());
    r.Register("teleport", "", NoOp());
    r.Register("time.set", "", NoOp());

    auto fl = r.MatchPrefix("fl");
    ASSERT_EQ(fl.size(), 2u);
    EXPECT_EQ(fl[0], "flag.get");
    EXPECT_EQ(fl[1], "flag.set");
}

TEST(ConsoleCommandRegistryTests, MatchPrefixEmptyReturnsAll)
{
    ConsoleCommandRegistry r;
    r.Register("a", "", NoOp());
    r.Register("b", "", NoOp());
    EXPECT_EQ(r.MatchPrefix("").size(), 2u);
}

TEST(ConsoleCommandRegistryTests, MatchPrefixNoMatchReturnsEmpty)
{
    ConsoleCommandRegistry r;
    r.Register("alpha", "", NoOp());
    EXPECT_TRUE(r.MatchPrefix("zzz").empty());
}

TEST(ConsoleCommandRegistryTests, MatchPrefixIsCaseSensitive)
{
    ConsoleCommandRegistry r;
    r.Register("Time", "", NoOp());
    EXPECT_TRUE(r.MatchPrefix("time").empty());
    EXPECT_EQ(r.MatchPrefix("Time").size(), 1u);
}

TEST(ConsoleCommandRegistryTests, AliasResolvesToSameCommand)
{
    ConsoleCommandRegistry r;
    r.Register("globe.intensity", "step radius+tilt", NoOp(), {"glb.i", "globe.i", "gi"});

    const auto* canonical = r.Lookup("globe.intensity");
    ASSERT_NE(canonical, nullptr);
    EXPECT_EQ(r.Lookup("glb.i"), canonical);
    EXPECT_EQ(r.Lookup("globe.i"), canonical);
    EXPECT_EQ(r.Lookup("gi"), canonical);
    EXPECT_EQ(r.Lookup("nope"), nullptr);
    ASSERT_EQ(canonical->aliases.size(), 3u);
}

TEST(ConsoleCommandRegistryTests, MatchPrefixIncludesAliases)
{
    ConsoleCommandRegistry r;
    r.Register("globe.intensity", "", NoOp(), {"glb.i", "globe.i", "gi"});
    r.Register("globe.radius", "", NoOp(), {"glb.r", "globe.r", "gr"});

    // All canonical + alias names starting with 'g':
    //   globe.intensity, globe.radius (canonical x2)
    //   globe.i, gi, globe.r, gr (aliases x4)
    //   glb.i, glb.r (aliases x2)
    EXPECT_EQ(r.MatchPrefix("g").size(), 8u);

    const auto glb = r.MatchPrefix("glb");
    ASSERT_EQ(glb.size(), 2u);
    EXPECT_EQ(glb[0], "glb.i");
    EXPECT_EQ(glb[1], "glb.r");
}
