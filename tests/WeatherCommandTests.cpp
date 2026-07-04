// Tests for the weather and light console commands. Each free function is
// invoked through CommandContext with hand-built dependency references, so
// the tests never need a Game instance or renderer.

#include <gtest/gtest.h>

#include "../src/Console.hpp"
#include "../src/ConsoleCommands.hpp"
#include "../src/Tilemap.hpp"
#include "../src/TimeManager.hpp"
#include "../src/WeatherDefinitions.hpp"
#include "../src/WeatherDirector.hpp"

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

// Count scrollback lines that open with "day +", i.e. one per weather.forecast
// day entry (night-event lines are indented "  night: ..." and don't match).
int CountDayLines(const ConsoleBuffer& buf)
{
    int count = 0;
    for (const auto& l : buf.Lines())
    {
        if (l.text.starts_with("day +"))
        {
            ++count;
        }
    }
    return count;
}
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

// time.weather routes through the WeatherDirector: an explicit duration starts
// a transition; 0 hard-cuts; a missing director falls back to a bare set.
TEST(WeatherCommandTests, TimeWeatherRoutesThroughDirector)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    const ArgPack withDuration({"Thunderstorm", "5"});
    EXPECT_TRUE(Cmd_TimeWeather(withDuration.span(), ctx));
    EXPECT_EQ(time.GetWeather(), WeatherState::Thunderstorm);
    EXPECT_TRUE(director.IsTransitioning());

    const ArgPack hardCut({"Fog", "0"});
    EXPECT_TRUE(Cmd_TimeWeather(hardCut.span(), ctx));
    EXPECT_EQ(time.GetWeather(), WeatherState::Fog);
    EXPECT_FALSE(director.IsTransitioning());

    // No director: bare set (test isolation contract of CommandContext).
    ctx.weatherDirector = nullptr;
    const ArgPack bare({"Clear"});
    EXPECT_TRUE(Cmd_TimeWeather(bare.span(), ctx));
    EXPECT_EQ(time.GetWeather(), WeatherState::Clear);
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

// weather.next / weather.random now take an optional [seconds] arg (same
// parse/validation as time.weather) and route through RouteWeatherRequest so
// their echo matches time.weather's "(Ns)" vs "(instant)" wording.

TEST(WeatherCommandTests, NextAcceptsOptionalSecondsAndTransitions)
{
    TimeManager time;
    time.Initialize();
    time.SetWeather(WeatherState::Clear);
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_WeatherNext(ArgPack({"5"}).span(), ctx));
    EXPECT_EQ(time.GetWeather(), WeatherState::LightRain);
    EXPECT_TRUE(director.IsTransitioning());
}

TEST(WeatherCommandTests, NextRejectsBadSeconds)
{
    TimeManager time;
    time.Initialize();
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    EXPECT_FALSE(Cmd_WeatherNext(ArgPack({"-1"}).span(), ctx));
    EXPECT_FALSE(Cmd_WeatherNext(ArgPack({"a", "b"}).span(), ctx));
}

TEST(WeatherCommandTests, RandomAcceptsOptionalSecondsAsHardCut)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_WeatherRandom(ArgPack({"0"}).span(), ctx));
    EXPECT_FALSE(director.IsTransitioning());
}

// time.weather's echo is three-way honest: "(Ns)" only when THIS call
// started/retargeted a blend, "(instant)" for hard cuts / disabled or null
// director, "(no change)" when the director ignored a same-target request.

TEST(WeatherCommandTests, TimeWeatherEchoesInstantWithoutDirector)
{
    TimeManager time;
    time.Initialize();
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;

    EXPECT_TRUE(Cmd_TimeWeather(ArgPack({"Fog", "5"}).span(), ctx));
    ASSERT_FALSE(buf.Lines().empty());
    EXPECT_NE(buf.Lines().back().text.find("(instant)"), std::string::npos);
}

TEST(WeatherCommandTests, TimeWeatherEchoesDurationOnlyWhenTransitioning)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_TimeWeather(ArgPack({"Fog", "5"}).span(), ctx));
    EXPECT_NE(buf.Lines().back().text.find("(5.0s)"), std::string::npos);

    EXPECT_TRUE(Cmd_TimeWeather(ArgPack({"Clear", "0"}).span(), ctx));
    EXPECT_NE(buf.Lines().back().text.find("(instant)"), std::string::npos);
}

// Re-requesting the state a transition is already heading to is a director
// no-op (StartWeatherChange's same-target branch): the new duration never
// takes effect. The echo must say "(no change)" -- not claim the requested
// seconds -- and the in-flight transition must be untouched.
TEST(WeatherCommandTests, TimeWeatherSameTargetMidFlightEchoesNoChange)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_TimeWeather(ArgPack({"Thunderstorm", "10"}).span(), ctx));
    ASSERT_TRUE(director.IsTransitioning());
    const WeatherDirector::Transition before = director.GetTransition();

    EXPECT_TRUE(Cmd_TimeWeather(ArgPack({"Thunderstorm", "3"}).span(), ctx));
    EXPECT_NE(buf.Lines().back().text.find("(no change)"), std::string::npos);
    EXPECT_EQ(buf.Lines().back().text.find("(3.0s)"), std::string::npos);

    const WeatherDirector::Transition after = director.GetTransition();
    EXPECT_TRUE(after.active);
    EXPECT_EQ(after.from, before.from);
    EXPECT_EQ(after.to, before.to);
    EXPECT_FLOAT_EQ(after.progress, before.progress);
}

// ---------------------------------------------------------------------------
// weather.auto
// ---------------------------------------------------------------------------

TEST(WeatherCommandTests, AutoTogglesDirectorState)
{
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_WeatherAuto(ArgPack({"off"}).span(), ctx));
    EXPECT_FALSE(director.IsAutoWeather());
    EXPECT_TRUE(Cmd_WeatherAuto(ArgPack({"on"}).span(), ctx));
    EXPECT_TRUE(director.IsAutoWeather());
}

TEST(WeatherCommandTests, AutoNoArgPrintsCurrentStateWithoutChangingIt)
{
    WeatherDirector director;
    director.SetEnabled(true);
    director.SetAutoWeather(false);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_WeatherAuto(ArgPack({}).span(), ctx));
    EXPECT_FALSE(director.IsAutoWeather());
    ASSERT_FALSE(buf.Lines().empty());
    EXPECT_NE(buf.Lines().back().text.find("off"), std::string::npos);
}

TEST(WeatherCommandTests, AutoRejectsGarbageArg)
{
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.weatherDirector = &director;

    EXPECT_FALSE(Cmd_WeatherAuto(ArgPack({"bogus"}).span(), ctx));
    EXPECT_TRUE(director.IsAutoWeather());  // unchanged (default true)
}

TEST(WeatherCommandTests, AutoFailsWithoutDirector)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_WeatherAuto(ArgPack({"on"}).span(), ctx));
}

// ---------------------------------------------------------------------------
// weather.forecast
// ---------------------------------------------------------------------------

TEST(WeatherCommandTests, ForecastDefaultsToThreeDays)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;
    director.SetEnabled(true);
    director.SetForecastSeed(7);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_WeatherForecast(ArgPack({}).span(), ctx));
    EXPECT_EQ(CountDayLines(buf), 3);
}

TEST(WeatherCommandTests, ForecastCapsAtSevenDays)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;
    director.SetEnabled(true);
    director.SetForecastSeed(7);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_WeatherForecast(ArgPack({"50"}).span(), ctx));
    EXPECT_EQ(CountDayLines(buf), 7);
}

TEST(WeatherCommandTests, ForecastRejectsNonPositiveOrGarbageDays)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    EXPECT_FALSE(Cmd_WeatherForecast(ArgPack({"0"}).span(), ctx));
    EXPECT_FALSE(Cmd_WeatherForecast(ArgPack({"-2"}).span(), ctx));
    EXPECT_FALSE(Cmd_WeatherForecast(ArgPack({"abc"}).span(), ctx));
}

TEST(WeatherCommandTests, ForecastFailsWithoutDirectorOrTime)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;

    ConsoleBuffer bufNoDirector;
    CommandContext ctxNoDirector{bufNoDirector};
    ctxNoDirector.time = &time;
    EXPECT_FALSE(Cmd_WeatherForecast(ArgPack({}).span(), ctxNoDirector));

    ConsoleBuffer bufNoTime;
    CommandContext ctxNoTime{bufNoTime};
    ctxNoTime.weatherDirector = &director;
    EXPECT_FALSE(Cmd_WeatherForecast(ArgPack({}).span(), ctxNoTime));
}

// ---------------------------------------------------------------------------
// weather.status
// ---------------------------------------------------------------------------

TEST(WeatherCommandTests, StatusReportsIdleWeather)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_WeatherStatus(ArgPack({}).span(), ctx));
    bool sawWeatherLine = false;
    for (const auto& l : buf.Lines())
    {
        if (l.text.find("weather.status: Clear") != std::string::npos)
        {
            sawWeatherLine = true;
        }
    }
    EXPECT_TRUE(sawWeatherLine);
}

TEST(WeatherCommandTests, StatusReportsTransitionPairAndManualHold)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_TimeWeather(ArgPack({"Thunderstorm", "5"}).span(), ctx));
    ASSERT_TRUE(director.IsTransitioning());
    EXPECT_TRUE(director.IsManualHold());

    EXPECT_TRUE(Cmd_WeatherStatus(ArgPack({}).span(), ctx));
    bool sawTransition = false;
    for (const auto& l : buf.Lines())
    {
        if (l.text.find("Clear -> Thunderstorm") != std::string::npos &&
            l.text.find('%') != std::string::npos)
        {
            sawTransition = true;
        }
    }
    EXPECT_TRUE(sawTransition);
}

TEST(WeatherCommandTests, StatusFailsWithoutDirectorOrTime)
{
    TimeManager time;
    time.Initialize();
    WeatherDirector director;

    ConsoleBuffer bufNoDirector;
    CommandContext ctxNoDirector{bufNoDirector};
    ctxNoDirector.time = &time;
    EXPECT_FALSE(Cmd_WeatherStatus(ArgPack({}).span(), ctxNoDirector));

    ConsoleBuffer bufNoTime;
    CommandContext ctxNoTime{bufNoTime};
    ctxNoTime.weatherDirector = &director;
    EXPECT_FALSE(Cmd_WeatherStatus(ArgPack({}).span(), ctxNoTime));
}

// ---------------------------------------------------------------------------
// weather.wind
// ---------------------------------------------------------------------------

TEST(WeatherCommandTests, WindReadoutReturnsTrueAndPrints)
{
    WeatherDirector director;
    director.SetEnabled(true);
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_WeatherWind(ArgPack({}).span(), ctx));
    ASSERT_FALSE(buf.Lines().empty());
    EXPECT_NE(buf.Lines().back().text.find("weather.wind:"), std::string::npos);
}

TEST(WeatherCommandTests, WindFailsWithoutDirector)
{
    ConsoleBuffer buf;
    CommandContext ctx{buf};
    EXPECT_FALSE(Cmd_WeatherWind(ArgPack({}).span(), ctx));
}

// ---------------------------------------------------------------------------
// config.dump: weather line must be name-based/replayable, plus weather.auto
// ---------------------------------------------------------------------------

// weather.auto toggles the director; weather.forecast prints per-day lines;
// weather.status reports the transition pair; config.dump emits a replayable
// name (not the old integer) plus the auto state. Adapted from the brief's
// RunCommandLine shape to this file's direct Cmd_* + ArgPack pattern (no
// RunCommandLine helper exists here).
TEST(WeatherCommandTests, ConfigDumpEmitsReplayableWeatherNameAndAutoState)
{
    ConsoleBuffer buffer;
    TimeManager time;
    time.Initialize();
    WeatherDirector director;
    director.SetEnabled(true);
    director.SetForecastSeed(7);

    CommandContext ctx{buffer};
    ctx.time = &time;
    ctx.weatherDirector = &director;

    EXPECT_TRUE(Cmd_WeatherAuto(ArgPack({"off"}).span(), ctx));
    EXPECT_FALSE(director.IsAutoWeather());
    EXPECT_TRUE(Cmd_WeatherAuto(ArgPack({"on"}).span(), ctx));
    EXPECT_TRUE(director.IsAutoWeather());

    EXPECT_TRUE(Cmd_WeatherForecast(ArgPack({"2"}).span(), ctx));
    EXPECT_TRUE(Cmd_WeatherStatus(ArgPack({}).span(), ctx));
    EXPECT_TRUE(Cmd_WeatherWind(ArgPack({}).span(), ctx));

    // config.dump: the weather line must round-trip through the name parser.
    EXPECT_TRUE(Cmd_ConfigDump(ArgPack({}).span(), ctx));
    bool sawName = false;
    bool sawAuto = false;
    for (const auto& l : buffer.Lines())
    {
        if (l.text.find("time.weather Clear 0") != std::string::npos)
        {
            sawName = true;
        }
        if (l.text == "weather.auto on")
        {
            sawAuto = true;
        }
    }
    EXPECT_TRUE(sawName) << "config.dump weather line not name-based/replayable";
    EXPECT_TRUE(sawAuto) << "config.dump did not emit weather.auto state";
}

TEST(WeatherCommandTests, ConfigDumpOmitsAutoLineWithoutDirector)
{
    ConsoleBuffer buf;
    TimeManager time;
    time.Initialize();
    CommandContext ctx{buf};
    ctx.time = &time;

    EXPECT_TRUE(Cmd_ConfigDump(ArgPack({}).span(), ctx));
    bool sawName = false;
    bool sawAuto = false;
    for (const auto& l : buf.Lines())
    {
        if (l.text.find("time.weather Clear 0") != std::string::npos)
        {
            sawName = true;
        }
        if (l.text.find("weather.auto") != std::string::npos)
        {
            sawAuto = true;
        }
    }
    EXPECT_TRUE(sawName);
    EXPECT_FALSE(sawAuto);
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
