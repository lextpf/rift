#include <gtest/gtest.h>

#include "../src/TimeManager.hpp"
#include "../src/WeatherBlend.hpp"
#include "../src/WeatherDefinitions.hpp"

#include <glm/glm.hpp>

TEST(WeatherOverlayMerge, ScalarBlendZeroIsBase)
{
    EXPECT_FLOAT_EQ(BlendOverlayScalar(0.0f, 0.85f, 0.0f), 0.0f);
}
TEST(WeatherOverlayMerge, ScalarBlendOneTakesMax)
{
    EXPECT_FLOAT_EQ(BlendOverlayScalar(0.0f, 0.85f, 1.0f), 0.85f);
    EXPECT_FLOAT_EQ(BlendOverlayScalar(0.9f, 0.85f, 1.0f), 0.9f);  // base already higher
}
TEST(WeatherOverlayMerge, TintMultipliesTowardOverlay)
{
    glm::vec3 out = BlendOverlayTint(glm::vec3(1.0f), glm::vec3(0.5f), 1.0f);
    EXPECT_FLOAT_EQ(out.r, 0.5f);
    glm::vec3 none = BlendOverlayTint(glm::vec3(1.0f), glm::vec3(0.5f), 0.0f);
    EXPECT_FLOAT_EQ(none.r, 1.0f);  // amount 0 -> unchanged
}
TEST(WeatherOverlayMerge, AuroraFadeOrsWhenOverlayHasAurora)
{
    EXPECT_FLOAT_EQ(BlendOverlayAuroraFade(0.0f, 1.0f, true), 1.0f);
    EXPECT_FLOAT_EQ(BlendOverlayAuroraFade(0.3f, 1.0f, false), 0.3f);  // no aurora -> base
}

static void SettleOverlay(TimeManager& tm)  // drive blend to steady state
{
    for (int i = 0; i < 400; ++i)
    {
        tm.Update(0.1f);
    }
}

TEST(TimeManagerOverlay, AuroraOverlayUsesNaturalStarsAndShowsAurora)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(2.0f);                        // night
    tm.SetTimeScale(0.0f);                   // Keep natural stars at their night value.
    tm.SetWeather(WeatherState::HeavyRain);  // base: starVis override 0, no aurora
    const float baseStar = tm.GetStarVisibility();
    tm.SetWeatherOverlay(WeatherState::Aurora);  // overlay restores natural stars at night
    SettleOverlay(tm);
    EXPECT_GT(tm.GetStarVisibility(), baseStar);
    EXPECT_GE(tm.GetStarVisibility(), 0.8f);
    EXPECT_GT(tm.GetAuroraFade(), 0.5f);
    EXPECT_TRUE(tm.HasWeatherOverlay());
    EXPECT_EQ(tm.GetWeatherOverlay(), WeatherState::Aurora);
}

TEST(TimeManagerOverlay, ClearFadesBackToBase)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(2.0f);
    tm.SetWeather(WeatherState::HeavyRain);
    tm.SetWeatherOverlay(WeatherState::Aurora);
    SettleOverlay(tm);
    tm.ClearWeatherOverlay();
    SettleOverlay(tm);
    EXPECT_FALSE(tm.HasWeatherOverlay());
    EXPECT_LT(tm.GetAuroraFade(), 0.01f);
    // HeavyRain overrides starVisibility to 0 at night; after the overlay
    // fades back out, the base value should be resolved again.
    EXPECT_LT(tm.GetStarVisibility(), 0.05f);
}

TEST(TimeManagerOverlay, InitializeResetsOverlay)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetWeatherOverlay(WeatherState::Aurora);
    tm.Initialize();
    EXPECT_FALSE(tm.HasWeatherOverlay());
    EXPECT_FLOAT_EQ(tm.GetOverlayBlend(), 0.0f);
}

TEST(TimeManagerOverlay, FadeAdvancesWhileClockIsFrozen)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(2.0f);
    tm.SetPaused(true);
    tm.SetWeatherOverlay(WeatherState::Aurora);

    SettleOverlay(tm);

    EXPECT_FLOAT_EQ(tm.GetTimeOfDay(), 2.0f);
    EXPECT_FLOAT_EQ(tm.GetOverlayBlend(), 1.0f);
    EXPECT_GT(tm.GetAuroraFade(), 0.5f);
}

TEST(TimeManagerOverlay, WeatherEffectsCanAdvanceWithoutAdvancingTitleClock)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(2.0f);
    tm.SetWeatherOverlay(WeatherState::Aurora);

    for (int i = 0; i < 400; ++i)
    {
        tm.UpdateWeatherEffects(0.1f);
    }

    EXPECT_FLOAT_EQ(tm.GetTimeOfDay(), 2.0f);
    EXPECT_FLOAT_EQ(tm.GetOverlayBlend(), 1.0f);
    EXPECT_GT(tm.GetAuroraFade(), 0.5f);
}

TEST(TimeManagerOverlay, FoldSurvivesBaseTransition)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(2.0f);
    tm.SetTimeScale(0.0f);
    const WeatherDefinition& a = GetWeatherDefinition(WeatherState::Clear);
    const WeatherDefinition& b = GetWeatherDefinition(WeatherState::HeavyRain);
    tm.SetWeatherBlend(&a, &b, 0.5f, &b);  // mid base transition
    tm.SetWeatherOverlay(WeatherState::Aurora);
    for (int i = 0; i < 400; ++i)
    {
        tm.Update(0.1f);
    }
    EXPECT_GT(tm.GetStarVisibility(), 0.5f);  // overlay star folds through the blend
    EXPECT_GT(tm.GetAuroraFade(), 0.5f);
}

TEST(TimeManagerOverlay, MeteorOverlayRaisesEffectiveMeteorRate)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetWeather(WeatherState::Clear);  // base meteorRateMultiplier = 1.0
    const float baseRate = tm.GetEffectiveMeteorRate();
    tm.SetWeatherOverlay(WeatherState::MeteorShower);  // high meteorRateMultiplier
    for (int i = 0; i < 400; ++i)
    {
        tm.Update(0.1f);
    }
    EXPECT_GT(tm.GetEffectiveMeteorRate(), baseRate);
    tm.ClearWeatherOverlay();
    for (int i = 0; i < 400; ++i)
    {
        tm.Update(0.1f);
    }
    EXPECT_NEAR(tm.GetEffectiveMeteorRate(), baseRate, 1e-4f);
}

TEST(TimeManagerOverlay, AuroraOverlayPreservesDaytimeSky)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(12.0f);      // noon
    tm.SetTimeScale(0.0f);  // freeze the hour so the ramp loop below doesn't drift it
    tm.SetWeather(WeatherState::HeavyRain);
    const glm::vec3 daySky = tm.GetSkyColor();
    const float dayStars = tm.GetStarVisibility();
    tm.SetWeatherOverlay(WeatherState::Aurora);
    SettleOverlay(tm);

    const glm::vec3 auroraSky = tm.GetSkyColor();
    EXPECT_NEAR(auroraSky.r, daySky.r, 1e-5f);
    EXPECT_NEAR(auroraSky.g, daySky.g, 1e-5f);
    EXPECT_NEAR(auroraSky.b, daySky.b, 1e-5f);
    EXPECT_NEAR(tm.GetStarVisibility(), dayStars, 1e-5f);
    EXPECT_GT(tm.GetAuroraFade(), 0.5f);
}

TEST(TimeManagerOverlay, AuroraOverlayPreservesDaytimeCelestial)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(12.0f);
    tm.SetTimeScale(0.0f);               // freeze the hour during the ramp loop
    tm.SetWeather(WeatherState::Clear);  // Clear shows celestial bodies
    const float dayFade = tm.GetCelestialFade();
    ASSERT_GT(dayFade, 0.5f);
    tm.SetWeatherOverlay(WeatherState::Aurora);
    SettleOverlay(tm);

    EXPECT_NEAR(tm.GetCelestialFade(), dayFade, 1e-5f);
    EXPECT_GT(tm.GetAuroraFade(), 0.5f);
}

TEST(TimeManagerOverlay, AuroraOverlayPreservesNightSky)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(0.0f);       // midnight - already dark
    tm.SetTimeScale(0.0f);  // freeze the hour during the ramp loop
    tm.SetWeather(WeatherState::Clear);
    const glm::vec3 baseSky = tm.GetSkyColor();
    const float baseFade = tm.GetCelestialFade();
    tm.SetWeatherOverlay(WeatherState::Aurora);
    SettleOverlay(tm);

    EXPECT_NEAR(tm.GetSkyColor().r, baseSky.r, 1e-5f);
    EXPECT_NEAR(tm.GetSkyColor().g, baseSky.g, 1e-5f);
    EXPECT_NEAR(tm.GetSkyColor().b, baseSky.b, 1e-5f);
    EXPECT_NEAR(tm.GetCelestialFade(), baseFade, 1e-5f);
}

TEST(TimeManagerOverlay, AuroraWeatherFollowsTimeChangesAtFullStrength)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(12.0f);
    tm.SetWeather(WeatherState::Clear);
    const glm::vec3 daySky = tm.GetSkyColor();
    const glm::vec3 dayAmbient = tm.GetAmbientColor();
    const float dayStars = tm.GetStarVisibility();
    const float dayCelestial = tm.GetCelestialFade();
    ASSERT_GT(dayCelestial, 0.5f);

    tm.SetTime(0.0f);
    tm.SetWeather(WeatherState::Aurora);
    const glm::vec3 midnightSky = tm.GetSkyColor();
    EXPECT_GT(tm.GetAuroraFade(), 0.5f);

    // Mirrors entering `ts 12` while Aurora is already active.
    tm.SetTime(12.0f);
    const glm::vec3 auroraSky = tm.GetSkyColor();
    EXPECT_GT(auroraSky.r + auroraSky.g + auroraSky.b,
              midnightSky.r + midnightSky.g + midnightSky.b);
    EXPECT_NEAR(auroraSky.r, daySky.r, 1e-5f);
    EXPECT_NEAR(auroraSky.g, daySky.g, 1e-5f);
    EXPECT_NEAR(auroraSky.b, daySky.b, 1e-5f);
    EXPECT_NEAR(tm.GetAmbientColor().r, dayAmbient.r, 1e-5f);
    EXPECT_NEAR(tm.GetAmbientColor().g, dayAmbient.g, 1e-5f);
    EXPECT_NEAR(tm.GetAmbientColor().b, dayAmbient.b, 1e-5f);
    EXPECT_NEAR(tm.GetStarVisibility(), dayStars, 1e-5f);
    EXPECT_NEAR(tm.GetCelestialFade(), dayCelestial, 1e-5f);
    EXPECT_GT(tm.GetAuroraFade(), 0.5f);
}
