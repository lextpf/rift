// Tests for the weather and light console commands. Each free function is
// invoked through CommandContext with hand-built dependency references, so
// the tests never need a Game instance or renderer.

#include <gtest/gtest.h>

#include "../src/Console.hpp"
#include "../src/ConsoleCommands.hpp"
#include "../src/Tilemap.hpp"
#include "../src/TimeManager.hpp"
#include "../src/WeatherDefinitions.hpp"

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
            storage.emplace_back(a);
        for (const auto& s : storage)
            views.emplace_back(s);
    }

    [[nodiscard]] std::span<const std::string_view> span() const
    {
        return std::span<const std::string_view>(views.data(), views.size());
    }
};
}  // namespace

// ---------------------------------------------------------------------------
// time.weather
// ---------------------------------------------------------------------------

TEST(WeatherCommandTests, TimeWeatherSetsState)
{
    TimeManager time;
    time.Initialize();
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    ArgPack args({"Thunderstorm"});
    EXPECT_TRUE(Cmd_TimeWeather(args.span(), ctx));
    EXPECT_EQ(time.GetWeather(), WeatherState::Thunderstorm);
}

TEST(WeatherCommandTests, TimeWeatherAcceptsCanonicalNamesOnly)
{
    TimeManager time;
    time.Initialize();
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    // Lowercase form is a breaking change vs the old command, intentionally.
    ArgPack lower({"thunderstorm"});
    EXPECT_FALSE(Cmd_TimeWeather(lower.span(), ctx));
    EXPECT_EQ(time.GetWeather(), WeatherState::Clear);

    ArgPack canonical({"Thunderstorm"});
    EXPECT_TRUE(Cmd_TimeWeather(canonical.span(), ctx));
    EXPECT_EQ(time.GetWeather(), WeatherState::Thunderstorm);
}

TEST(WeatherCommandTests, TimeWeatherRejectsGarbage)
{
    TimeManager time;
    time.Initialize();
    time.SetWeather(WeatherState::Blizzard);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    ArgPack args({"NotARealState"});
    EXPECT_FALSE(Cmd_TimeWeather(args.span(), ctx));
    EXPECT_EQ(time.GetWeather(), WeatherState::Blizzard);
}

TEST(WeatherCommandTests, TimeWeatherRejectsWrongArgCount)
{
    TimeManager time;
    time.Initialize();
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    EXPECT_FALSE(Cmd_TimeWeather(ArgPack({}).span(), ctx));
    EXPECT_FALSE(Cmd_TimeWeather(ArgPack({"Clear", "Extra"}).span(), ctx));
}

TEST(WeatherCommandTests, TimeWeatherFailsWithoutTimeManager)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ArgPack args({"Clear"});
    EXPECT_FALSE(Cmd_TimeWeather(args.span(), ctx));
}

// ---------------------------------------------------------------------------
// weather.intensity
// ---------------------------------------------------------------------------

TEST(WeatherCommandTests, IntensitySetsValue)
{
    TimeManager time;
    time.Initialize();
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    ArgPack args({"0.5"});
    EXPECT_TRUE(Cmd_WeatherIntensity(args.span(), ctx));
    EXPECT_FLOAT_EQ(time.GetWeatherIntensity(), 0.5f);
}

TEST(WeatherCommandTests, IntensityRejectsOutOfRange)
{
    TimeManager time;
    time.Initialize();
    time.SetWeatherIntensity(0.7f);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    EXPECT_FALSE(Cmd_WeatherIntensity(ArgPack({"-0.1"}).span(), ctx));
    EXPECT_FALSE(Cmd_WeatherIntensity(ArgPack({"1.1"}).span(), ctx));
    EXPECT_FALSE(Cmd_WeatherIntensity(ArgPack({"abc"}).span(), ctx));
    EXPECT_FALSE(Cmd_WeatherIntensity(ArgPack({}).span(), ctx));
    // Value should be unchanged after rejected calls.
    EXPECT_FLOAT_EQ(time.GetWeatherIntensity(), 0.7f);
}

TEST(WeatherCommandTests, IntensityAcceptsBoundaries)
{
    TimeManager time;
    time.Initialize();
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    EXPECT_TRUE(Cmd_WeatherIntensity(ArgPack({"0.0"}).span(), ctx));
    EXPECT_FLOAT_EQ(time.GetWeatherIntensity(), 0.0f);
    EXPECT_TRUE(Cmd_WeatherIntensity(ArgPack({"1.0"}).span(), ctx));
    EXPECT_FLOAT_EQ(time.GetWeatherIntensity(), 1.0f);
}

// ---------------------------------------------------------------------------
// weather.next
// ---------------------------------------------------------------------------

TEST(WeatherCommandTests, NextCyclesForward)
{
    // After the weather overhaul (Overcast removed), LightRain is now the
    // second enum value so weather.next from Clear advances to it.
    TimeManager time;
    time.Initialize();
    time.SetWeather(WeatherState::Clear);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    ArgPack args({});
    EXPECT_TRUE(Cmd_WeatherNext(args.span(), ctx));
    EXPECT_EQ(time.GetWeather(), WeatherState::LightRain);
}

TEST(WeatherCommandTests, NextWrapsFromLastToFirst)
{
    TimeManager time;
    time.Initialize();
    // Derive the last weather from the enum (Count - 1) so this wrap test stays
    // correct when weather states change -- it previously hardcoded EmberStorm,
    // which stopped being the last state when GodRays was added.
    constexpr auto lastWeather = static_cast<WeatherState>(EnumTraits<WeatherState>::Count - 1);
    time.SetWeather(lastWeather);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    ArgPack args({});
    EXPECT_TRUE(Cmd_WeatherNext(args.span(), ctx));
    EXPECT_EQ(time.GetWeather(), WeatherState::Clear);
}

TEST(WeatherCommandTests, NextRejectsArgs)
{
    TimeManager time;
    time.Initialize();
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    EXPECT_FALSE(Cmd_WeatherNext(ArgPack({"extra"}).span(), ctx));
}

// ---------------------------------------------------------------------------
// weather.random
// ---------------------------------------------------------------------------

TEST(WeatherCommandTests, RandomProducesValidState)
{
    TimeManager time;
    time.Initialize();
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    for (int i = 0; i < 16; ++i)
    {
        ArgPack args({});
        EXPECT_TRUE(Cmd_WeatherRandom(args.span(), ctx));
        auto idx = static_cast<size_t>(std::to_underlying(time.GetWeather()));
        EXPECT_LT(idx, EnumTraits<WeatherState>::Count);
    }
}

// ---------------------------------------------------------------------------
// light.add / list / remove / clear
// ---------------------------------------------------------------------------

TEST(WeatherCommandTests, LightAddAppendsToTilemap)
{
    Tilemap map;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &map;

    EXPECT_EQ(map.GetLights().size(), 0u);
    EXPECT_TRUE(Cmd_LightAdd(ArgPack({"100", "200"}).span(), ctx));
    ASSERT_EQ(map.GetLights().size(), 1u);
    EXPECT_FLOAT_EQ(map.GetLights()[0].position.x, 100.0f);
    EXPECT_FLOAT_EQ(map.GetLights()[0].position.y, 200.0f);
}

TEST(WeatherCommandTests, LightAddAcceptsFullArgList)
{
    Tilemap map;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &map;

    ArgPack args({"50", "75", "0.8", "0.4", "0.1", "128", "DuskToDawn"});
    EXPECT_TRUE(Cmd_LightAdd(args.span(), ctx));
    ASSERT_EQ(map.GetLights().size(), 1u);
    const WorldLight& l = map.GetLights()[0];
    EXPECT_FLOAT_EQ(l.color.r, 0.8f);
    EXPECT_FLOAT_EQ(l.radius, 128.0f);
    EXPECT_EQ(l.schedule, LightSchedule::DuskToDawn);
}

TEST(WeatherCommandTests, LightAddRejectsInvalidSchedule)
{
    Tilemap map;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &map;

    ArgPack args({"50", "75", "1.0", "1.0", "1.0", "64", "Bogus"});
    EXPECT_FALSE(Cmd_LightAdd(args.span(), ctx));
    EXPECT_EQ(map.GetLights().size(), 0u);
}

TEST(WeatherCommandTests, LightAddRejectsNonPositiveRadius)
{
    Tilemap map;
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &map;

    ArgPack args({"0", "0", "1.0", "1.0", "1.0", "0"});
    EXPECT_FALSE(Cmd_LightAdd(args.span(), ctx));
}

TEST(WeatherCommandTests, LightClearRemovesAll)
{
    Tilemap map;
    map.AddLight({});
    map.AddLight({});
    ASSERT_EQ(map.GetLights().size(), 2u);

    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &map;

    EXPECT_TRUE(Cmd_LightClear(ArgPack({}).span(), ctx));
    EXPECT_EQ(map.GetLights().size(), 0u);
}

TEST(WeatherCommandTests, LightRemoveByIndex)
{
    Tilemap map;
    WorldLight a;
    a.position = {1.0f, 1.0f};
    WorldLight b;
    b.position = {2.0f, 2.0f};
    map.AddLight(a);
    map.AddLight(b);

    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &map;

    EXPECT_TRUE(Cmd_LightRemove(ArgPack({"0"}).span(), ctx));
    ASSERT_EQ(map.GetLights().size(), 1u);
    // Surviving light is the one originally at index 1.
    EXPECT_FLOAT_EQ(map.GetLights()[0].position.x, 2.0f);
}

TEST(WeatherCommandTests, LightRemoveOutOfRangeFails)
{
    Tilemap map;
    map.AddLight({});

    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.tilemap = &map;

    EXPECT_FALSE(Cmd_LightRemove(ArgPack({"5"}).span(), ctx));
    EXPECT_FALSE(Cmd_LightRemove(ArgPack({"-1"}).span(), ctx));
    EXPECT_EQ(map.GetLights().size(), 1u);
}

TEST(WeatherCommandTests, LightCommandsFailWithoutTilemap)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_LightAdd(ArgPack({"0", "0"}).span(), ctx));
    EXPECT_FALSE(Cmd_LightClear(ArgPack({}).span(), ctx));
    EXPECT_FALSE(Cmd_LightList(ArgPack({}).span(), ctx));
    EXPECT_FALSE(Cmd_LightRemove(ArgPack({"0"}).span(), ctx));
}
