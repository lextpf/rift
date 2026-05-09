// Tests for the WorldLight schedule math (ComputeLightIntensity) and the
// LightSchedule EnumTraits specialization. Renderer-free.

#include <gtest/gtest.h>

#include "../src/WeatherDefinitions.h"

#include <cmath>
#include <utility>

TEST(WorldLightTests, AlwaysOnIsConstantOne)
{
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::AlwaysOn, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::AlwaysOn, 6.0f), 1.0f);
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::AlwaysOn, 12.0f), 1.0f);
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::AlwaysOn, 23.999f), 1.0f);
}

TEST(WorldLightTests, NightOnlyFullDuringNight)
{
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::NightOnly, 22.0f), 1.0f);
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::NightOnly, 23.0f), 1.0f);
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::NightOnly, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::NightOnly, 3.999f), 1.0f);
}

TEST(WorldLightTests, NightOnlyZeroDuringDay)
{
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::NightOnly, 6.0f), 0.0f);
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::NightOnly, 12.0f), 0.0f);
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::NightOnly, 18.0f), 0.0f);
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::NightOnly, 19.999f), 0.0f);
}

TEST(WorldLightTests, NightOnlyRampsUpAtDusk)
{
    float v20 = ComputeLightIntensity(LightSchedule::NightOnly, 20.0f);
    float v21 = ComputeLightIntensity(LightSchedule::NightOnly, 21.0f);
    float v22 = ComputeLightIntensity(LightSchedule::NightOnly, 22.0f);

    EXPECT_FLOAT_EQ(v20, 0.0f);
    EXPECT_GT(v21, 0.0f);
    EXPECT_LT(v21, 1.0f);
    EXPECT_FLOAT_EQ(v22, 1.0f);
    // Smoothstep is monotonic.
    EXPECT_LT(v20, v21);
    EXPECT_LT(v21, v22);
}

TEST(WorldLightTests, NightOnlyRampsDownAtDawn)
{
    float v4 = ComputeLightIntensity(LightSchedule::NightOnly, 4.0f);
    float v5 = ComputeLightIntensity(LightSchedule::NightOnly, 5.0f);
    float v6 = ComputeLightIntensity(LightSchedule::NightOnly, 6.0f);

    EXPECT_FLOAT_EQ(v4, 1.0f);
    EXPECT_GT(v5, 0.0f);
    EXPECT_LT(v5, 1.0f);
    EXPECT_FLOAT_EQ(v6, 0.0f);
    EXPECT_GT(v4, v5);
    EXPECT_GT(v5, v6);
}

TEST(WorldLightTests, DuskToDawnRampsEarlier)
{
    // DuskToDawn ramps on 18:00-20:00; NightOnly ramps on 20:00-22:00.
    float duskNight = ComputeLightIntensity(LightSchedule::NightOnly, 19.0f);
    float duskDawn = ComputeLightIntensity(LightSchedule::DuskToDawn, 19.0f);
    EXPECT_FLOAT_EQ(duskNight, 0.0f);
    EXPECT_GT(duskDawn, 0.0f);
    EXPECT_LT(duskDawn, 1.0f);
}

TEST(WorldLightTests, DuskToDawnRampsLaterAtDawn)
{
    // DuskToDawn ramps off 04:00-07:00; NightOnly off 04:00-06:00.
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::NightOnly, 6.5f), 0.0f);
    float later = ComputeLightIntensity(LightSchedule::DuskToDawn, 6.5f);
    EXPECT_GT(later, 0.0f);
    EXPECT_LT(later, 1.0f);
}

TEST(WorldLightTests, IntensityClampedToRange)
{
    for (int hour10 = 0; hour10 < 240; ++hour10)
    {
        float h = hour10 * 0.1f;
        float v = ComputeLightIntensity(LightSchedule::NightOnly, h);
        EXPECT_GE(v, 0.0f) << "h=" << h;
        EXPECT_LE(v, 1.0f) << "h=" << h;
    }
}

TEST(WorldLightTests, NegativeHoursWrap)
{
    // Hour -1.0 should be equivalent to 23.0.
    EXPECT_FLOAT_EQ(ComputeLightIntensity(LightSchedule::NightOnly, -1.0f),
                    ComputeLightIntensity(LightSchedule::NightOnly, 23.0f));
}

TEST(WorldLightTests, LightScheduleEnumTraitsRoundTrip)
{
    for (size_t i = 0; i < EnumTraits<LightSchedule>::Count; ++i)
    {
        auto sched = static_cast<LightSchedule>(i);
        std::string_view name = EnumTraits<LightSchedule>::ToString(sched);
        auto parsed = EnumTraits<LightSchedule>::FromString(name);
        ASSERT_TRUE(parsed.has_value()) << name;
        EXPECT_EQ(*parsed, sched);
    }
}

TEST(WorldLightTests, WorldLightDefaults)
{
    WorldLight light;
    EXPECT_EQ(light.position.x, 0.0f);
    EXPECT_EQ(light.position.y, 0.0f);
    EXPECT_FLOAT_EQ(light.color.r, 1.0f);
    EXPECT_FLOAT_EQ(light.color.g, 0.85f);
    EXPECT_FLOAT_EQ(light.color.b, 0.55f);
    EXPECT_FLOAT_EQ(light.radius, 64.0f);
    EXPECT_EQ(light.schedule, LightSchedule::NightOnly);
}
