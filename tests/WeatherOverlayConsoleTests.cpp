// Tests for the weather.overlay console command, which sets/clears the
// manual sky overlay weather on TimeManager (Task 4 of the weather-overlay
// plan). Mirrors the CommandContext construction boilerplate used throughout
// tests/ConsoleCommandsTests.cpp - no GLFW/renderer involvement.

#include <gtest/gtest.h>

#include "../src/Console.hpp"
#include "../src/ConsoleCommands.hpp"
#include "../src/TimeManager.hpp"
#include "../src/WeatherDefinitions.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace
{
// Build a span<string_view> from string literals, keeping the underlying
// strings alive in `storage` so the views remain valid for the call.
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
}  // namespace

TEST(WeatherOverlayConsoleTests, SetsOverlayFromValidName)
{
    TimeManager tm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &tm;

    EXPECT_TRUE(Cmd_WeatherOverlay(ArgPack({"Aurora"}).span(), ctx));
    EXPECT_TRUE(tm.HasWeatherOverlay());
    EXPECT_EQ(tm.GetWeatherOverlay(), WeatherState::Aurora);
}

TEST(WeatherOverlayConsoleTests, OffClearsOverlay)
{
    TimeManager tm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &tm;

    EXPECT_TRUE(Cmd_WeatherOverlay(ArgPack({"Aurora"}).span(), ctx));
    ASSERT_TRUE(tm.HasWeatherOverlay());

    EXPECT_TRUE(Cmd_WeatherOverlay(ArgPack({"off"}).span(), ctx));
    EXPECT_FALSE(tm.HasWeatherOverlay());
}

TEST(WeatherOverlayConsoleTests, RejectsUnknownStateAndBadArity)
{
    TimeManager tm;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &tm;

    EXPECT_FALSE(Cmd_WeatherOverlay(ArgPack({"NotARealState"}).span(), ctx));
    EXPECT_FALSE(tm.HasWeatherOverlay());

    EXPECT_FALSE(Cmd_WeatherOverlay(ArgPack({}).span(), ctx));
    EXPECT_FALSE(Cmd_WeatherOverlay(ArgPack({"Aurora", "extra"}).span(), ctx));
    EXPECT_FALSE(Cmd_WeatherOverlay(ArgPack({"AuroraNight"}).span(), ctx));
}

TEST(WeatherOverlayConsoleTests, FailsWithoutTimeManager)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};

    EXPECT_FALSE(Cmd_WeatherOverlay(ArgPack({"Aurora"}).span(), ctx));
}
