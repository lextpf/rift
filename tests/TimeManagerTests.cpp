#include <gtest/gtest.h>
#include "../src/TimeManager.hpp"

class TimeManagerTest : public ::testing::Test
{
protected:
    TimeManager tm;

    void SetUp() override { tm.Initialize(); }
};

// --- Time Period Tests ---

TEST_F(TimeManagerTest, GetTimePeriod_Dawn)
{
    tm.SetTime(5.5f);
    EXPECT_EQ(tm.GetTimePeriod(), TimePeriod::Dawn);
}

TEST_F(TimeManagerTest, GetTimePeriod_Morning)
{
    tm.SetTime(8.0f);
    EXPECT_EQ(tm.GetTimePeriod(), TimePeriod::Morning);
}

TEST_F(TimeManagerTest, GetTimePeriod_Midday)
{
    tm.SetTime(12.0f);
    EXPECT_EQ(tm.GetTimePeriod(), TimePeriod::Midday);
}

TEST_F(TimeManagerTest, GetTimePeriod_Afternoon)
{
    tm.SetTime(17.0f);
    EXPECT_EQ(tm.GetTimePeriod(), TimePeriod::Afternoon);
}

TEST_F(TimeManagerTest, GetTimePeriod_Dusk)
{
    tm.SetTime(19.0f);
    EXPECT_EQ(tm.GetTimePeriod(), TimePeriod::Dusk);
}

TEST_F(TimeManagerTest, GetTimePeriod_Evening)
{
    tm.SetTime(21.0f);
    EXPECT_EQ(tm.GetTimePeriod(), TimePeriod::Evening);
}

TEST_F(TimeManagerTest, GetTimePeriod_Night)
{
    tm.SetTime(23.0f);
    EXPECT_EQ(tm.GetTimePeriod(), TimePeriod::Night);
}

TEST_F(TimeManagerTest, GetTimePeriod_LateNight)
{
    tm.SetTime(4.5f);
    EXPECT_EQ(tm.GetTimePeriod(), TimePeriod::LateNight);
}

// --- Sun Arc Tests ---

TEST_F(TimeManagerTest, GetSunArc_BelowHorizonBeforeSunrise)
{
    tm.SetTime(5.0f);
    EXPECT_FLOAT_EQ(tm.GetSunArc(), -1.0f);
}

TEST_F(TimeManagerTest, GetSunArc_AtSunrise)
{
    tm.SetTime(6.0f);
    EXPECT_FLOAT_EQ(tm.GetSunArc(), 0.0f);
}

TEST_F(TimeManagerTest, GetSunArc_AtNoon)
{
    tm.SetTime(13.0f);  // Midpoint of 6-20 = 13
    EXPECT_FLOAT_EQ(tm.GetSunArc(), 0.5f);
}

TEST_F(TimeManagerTest, GetSunArc_AtSunset)
{
    tm.SetTime(20.0f);
    EXPECT_FLOAT_EQ(tm.GetSunArc(), 1.0f);
}

TEST_F(TimeManagerTest, GetSunArc_BelowHorizonAfterSunset)
{
    tm.SetTime(21.0f);
    EXPECT_FLOAT_EQ(tm.GetSunArc(), -1.0f);
}

// --- Moon Arc Tests ---

TEST_F(TimeManagerTest, GetMoonArc_AtMoonrise)
{
    tm.SetTime(19.0f);
    EXPECT_FLOAT_EQ(tm.GetMoonArc(), 0.0f);
}

TEST_F(TimeManagerTest, GetMoonArc_AtMidnight)
{
    tm.SetTime(0.0f);
    // At midnight: (0 + (24 - 19)) / 12 = 5/12
    EXPECT_NEAR(tm.GetMoonArc(), 5.0f / 12.0f, 0.001f);
}

TEST_F(TimeManagerTest, GetMoonArc_AtMoonset)
{
    tm.SetTime(7.0f);
    EXPECT_FLOAT_EQ(tm.GetMoonArc(), 1.0f);
}

TEST_F(TimeManagerTest, GetMoonArc_BelowHorizon)
{
    tm.SetTime(12.0f);
    EXPECT_FLOAT_EQ(tm.GetMoonArc(), -1.0f);
}

// --- Moon Phase Tests ---

TEST_F(TimeManagerTest, GetMoonPhase_CyclesEvery8Days)
{
    for (int day = 0; day < 16; ++day)
    {
        // Manually set day count by advancing time
        tm.Initialize();
        tm.SetDayDuration(1.0f);  // 1 second = 1 day
        for (int d = 0; d < day; ++d)
        {
            tm.Update(1.0f);
        }
        EXPECT_EQ(tm.GetMoonPhase(), day % 8);
    }
}

// --- Day/Night Tests ---

TEST_F(TimeManagerTest, IsDay_AtNoon)
{
    tm.SetTime(12.0f);
    EXPECT_TRUE(tm.IsDay());
    EXPECT_FALSE(tm.IsNight());
}

TEST_F(TimeManagerTest, IsNight_AtMidnight)
{
    tm.SetTime(0.0f);
    EXPECT_TRUE(tm.IsNight());
    EXPECT_FALSE(tm.IsDay());
}

TEST_F(TimeManagerTest, IsDay_AtSunrise)
{
    tm.SetTime(6.0f);
    EXPECT_TRUE(tm.IsDay());
}

TEST_F(TimeManagerTest, IsDay_AtSunset)
{
    tm.SetTime(20.0f);
    EXPECT_TRUE(tm.IsDay());
}

TEST_F(TimeManagerTest, IsNight_JustAfterSunset)
{
    tm.SetTime(20.1f);
    EXPECT_TRUE(tm.IsNight());
}

// --- Time Control Tests ---

TEST_F(TimeManagerTest, SetTime_Wraps24Hours)
{
    tm.SetTime(25.0f);
    EXPECT_NEAR(tm.GetTimeOfDay(), 1.0f, 0.001f);
}

TEST_F(TimeManagerTest, SetTime_WrapsNegative)
{
    tm.SetTime(-1.0f);
    EXPECT_NEAR(tm.GetTimeOfDay(), 23.0f, 0.001f);
}

TEST_F(TimeManagerTest, AdvanceTime_Basic)
{
    tm.SetTime(10.0f);
    tm.AdvanceTime(2.0f);
    EXPECT_NEAR(tm.GetTimeOfDay(), 12.0f, 0.001f);
}

TEST_F(TimeManagerTest, AdvanceTime_WrapsAtMidnight)
{
    tm.SetTime(23.0f);
    tm.AdvanceTime(3.0f);
    EXPECT_NEAR(tm.GetTimeOfDay(), 2.0f, 0.001f);
}

// --- Pause Tests ---

TEST_F(TimeManagerTest, Pause_StopsTimeProgression)
{
    tm.SetTime(12.0f);
    tm.SetPaused(true);
    tm.Update(100.0f);
    EXPECT_FLOAT_EQ(tm.GetTimeOfDay(), 12.0f);
}

TEST_F(TimeManagerTest, TogglePause)
{
    EXPECT_FALSE(tm.IsPaused());
    tm.TogglePause();
    EXPECT_TRUE(tm.IsPaused());
    tm.TogglePause();
    EXPECT_FALSE(tm.IsPaused());
}

// --- Time Scale Tests ---

TEST_F(TimeManagerTest, TimeScale_DoublesSpeed)
{
    tm.SetTime(0.0f);
    tm.SetDayDuration(24.0f);  // 24 seconds = 24 hours, so 1 sec = 1 hour
    tm.SetTimeScale(2.0f);
    tm.Update(1.0f);
    EXPECT_NEAR(tm.GetTimeOfDay(), 2.0f, 0.001f);
}

TEST_F(TimeManagerTest, TimeScale_HalvesSpeed)
{
    tm.SetTime(0.0f);
    tm.SetDayDuration(24.0f);
    tm.SetTimeScale(0.5f);
    tm.Update(1.0f);
    EXPECT_NEAR(tm.GetTimeOfDay(), 0.5f, 0.001f);
}

// --- Star Visibility Tests ---

TEST_F(TimeManagerTest, GetStarVisibility_ZeroAtMidday)
{
    tm.SetTime(12.0f);
    EXPECT_FLOAT_EQ(tm.GetStarVisibility(), 0.0f);
}

TEST_F(TimeManagerTest, GetStarVisibility_FullAtMidnight)
{
    tm.SetTime(0.0f);
    EXPECT_FLOAT_EQ(tm.GetStarVisibility(), 1.0f);
}

TEST_F(TimeManagerTest, GetStarVisibility_ZeroInHeavyRain)
{
    // Equivalent night-time star-suppression check using HeavyRain since
    // Overcast was removed in the weather overhaul.
    tm.SetTime(0.0f);
    tm.SetWeather(WeatherState::HeavyRain);
    EXPECT_FLOAT_EQ(tm.GetStarVisibility(), 0.0f);
}

// --- Dawn Intensity Tests ---

TEST_F(TimeManagerTest, GetDawnIntensity_ZeroAtNoon)
{
    tm.SetTime(12.0f);
    EXPECT_FLOAT_EQ(tm.GetDawnIntensity(), 0.0f);
}

TEST_F(TimeManagerTest, GetDawnIntensity_PeakAt6)
{
    tm.SetTime(6.0f);
    EXPECT_FLOAT_EQ(tm.GetDawnIntensity(), 1.0f);
}

TEST_F(TimeManagerTest, GetDawnIntensity_FadingIn)
{
    tm.SetTime(5.0f);  // Midpoint of 4.5-5.5 fade in
    EXPECT_NEAR(tm.GetDawnIntensity(), 0.5f, 0.01f);
}

// --- Color Tests ---

TEST_F(TimeManagerTest, GetAmbientColor_NotZeroAtNight)
{
    tm.SetTime(0.0f);
    glm::vec3 color = tm.GetAmbientColor();
    EXPECT_GT(color.r, 0.0f);
    EXPECT_GT(color.g, 0.0f);
    EXPECT_GT(color.b, 0.0f);
}

TEST_F(TimeManagerTest, GetAmbientColor_BrightestAtMidday)
{
    tm.SetTime(12.0f);
    glm::vec3 middayColor = tm.GetAmbientColor();

    tm.SetTime(0.0f);
    glm::vec3 midnightColor = tm.GetAmbientColor();

    float middayBrightness = middayColor.r + middayColor.g + middayColor.b;
    float midnightBrightness = midnightColor.r + midnightColor.g + midnightColor.b;

    EXPECT_GT(middayBrightness, midnightBrightness);
}

TEST_F(TimeManagerTest, GetSunColor_ZeroAtNight)
{
    tm.SetTime(0.0f);
    glm::vec3 color = tm.GetSunColor();
    EXPECT_FLOAT_EQ(color.r, 0.0f);
    EXPECT_FLOAT_EQ(color.g, 0.0f);
    EXPECT_FLOAT_EQ(color.b, 0.0f);
}

TEST_F(TimeManagerTest, GetSunColor_NotZeroAtNoon)
{
    tm.SetTime(12.0f);
    glm::vec3 color = tm.GetSunColor();
    EXPECT_GT(color.r, 0.0f);
    EXPECT_GT(color.g, 0.0f);
    EXPECT_GT(color.b, 0.0f);
}
