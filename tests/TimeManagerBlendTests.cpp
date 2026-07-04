#include <gtest/gtest.h>

#include "../src/TimeManager.hpp"
#include "../src/WeatherBlend.hpp"
#include "../src/WeatherDefinitions.hpp"

#include <glm/glm.hpp>

/// @file TimeManagerBlendTests.cpp
/// @brief Weather-blend-aware TimeManager getters: natural (weatherless)
/// channels, the dawn/dusk sky-override ramp, and endpoint-mix blending.

// GetNaturalStarVisibility ignores the weather override entirely.
TEST(TimeManagerBlend, NaturalStarVisibilityIgnoresWeatherOverride)
{
    TimeManager time;
    time.Initialize();
    time.SetTime(0.0f);                           // midnight: natural visibility 1.0
    time.SetWeather(WeatherState::Thunderstorm);  // starVisibilityOverride = 0
    time.SetWeatherIntensity(1.0f);

    EXPECT_FLOAT_EQ(time.GetNaturalStarVisibility(), 1.0f);
    EXPECT_FLOAT_EQ(time.GetStarVisibility(), 0.0f);  // weather-resolved
}

// The sky-override day/night factor ramps across sunrise/sunset instead of
// stepping. At one game-hour-per-real-second (24 s days) the old binary
// IsDay() was a single-frame 3.3x sky jump.
TEST(TimeManagerBlend, SkyOverrideRampsAcrossSunset)
{
    TimeManager time;
    time.Initialize();
    time.SetWeather(WeatherState::Thunderstorm);  // has skyColorOverride
    time.SetWeatherIntensity(1.0f);

    // Sample sky color across sunset (20:00) in 0.1 h steps; consecutive
    // samples must never jump more than ~35% of the total day->night drop.
    time.SetTime(19.4f);
    glm::vec3 prev = time.GetSkyColor();
    time.SetTime(19.4f);
    glm::vec3 daySky = prev;
    time.SetTime(20.6f);
    glm::vec3 nightSky = time.GetSkyColor();
    float totalDrop = glm::length(daySky - nightSky);
    ASSERT_GT(totalDrop, 0.0f) << "test vacuous: no day/night difference";

    for (float t = 19.5f; t <= 20.6f; t += 0.1f)
    {
        time.SetTime(t);
        glm::vec3 cur = time.GetSkyColor();
        EXPECT_LE(glm::length(cur - prev), 0.35f * totalDrop)
            << "sky stepped too hard at hour " << t;
        prev = cur;
    }
}

// Refactor guard: with no blend active, the getters must match the current
// single-definition behavior for a non-override weather.
TEST(TimeManagerBlend, IdleGettersUnchangedForClearWeather)
{
    TimeManager time;
    time.Initialize();
    time.SetTime(12.0f);
    time.SetWeather(WeatherState::Clear);

    // Clear has no overrides: sky = natural, stars = natural, ambient = base.
    EXPECT_FLOAT_EQ(time.GetStarVisibility(), 0.0f);         // noon
    EXPECT_FLOAT_EQ(time.GetNaturalStarVisibility(), 0.0f);  // same
    glm::vec3 ambient = time.GetAmbientColor();
    EXPECT_NEAR(ambient.r, 0.93f, 1e-4f);  // middayColor, tint = identity
    EXPECT_NEAR(ambient.g, 0.93f, 1e-4f);
    EXPECT_NEAR(ambient.b, 0.91f, 1e-4f);
}

// With a blend active, each getter equals the manual per-endpoint mix.
TEST(TimeManagerBlend, BlendedGettersEqualManualEndpointMix)
{
    TimeManager time;
    time.Initialize();
    time.SetTime(0.0f);  // midnight
    time.SetWeatherIntensity(1.0f);

    const WeatherDefinition& clear = GetWeatherDefinition(WeatherState::Clear);
    const WeatherDefinition& storm = GetWeatherDefinition(WeatherState::Thunderstorm);

    // Manual endpoint values (blend not yet active).
    time.SetWeather(WeatherState::Clear);
    glm::vec3 ambientFrom = time.GetAmbientColor();
    float starsFrom = time.GetStarVisibility();
    time.SetWeather(WeatherState::Thunderstorm);
    glm::vec3 ambientTo = time.GetAmbientColor();
    float starsTo = time.GetStarVisibility();

    const float t = 0.25f;
    WeatherDefinition effective = BlendWeatherDefinitions(clear, storm, t);
    time.SetWeatherBlend(&clear, &storm, t, &effective);

    glm::vec3 expectAmbient = glm::mix(ambientFrom, ambientTo, t);
    EXPECT_NEAR(time.GetAmbientColor().r, expectAmbient.r, 1e-5f);
    EXPECT_NEAR(time.GetAmbientColor().g, expectAmbient.g, 1e-5f);
    EXPECT_NEAR(time.GetAmbientColor().b, expectAmbient.b, 1e-5f);
    EXPECT_NEAR(time.GetStarVisibility(), starsFrom + (starsTo - starsFrom) * t, 1e-5f);

    // Effective definition is served while the blend is active.
    EXPECT_FLOAT_EQ(time.GetEffectiveWeatherDefinition().baseSpawnRate, effective.baseSpawnRate);

    time.ClearWeatherBlend();
    EXPECT_FLOAT_EQ(time.GetStarVisibility(), starsTo);  // back to single-def path
    EXPECT_FLOAT_EQ(time.GetEffectiveWeatherDefinition().baseSpawnRate, storm.baseSpawnRate);
}

// A resolved-from capture replaces the from-endpoint exactly (retarget seam).
TEST(TimeManagerBlend, ResolvedFromOverridesFromEndpoint)
{
    TimeManager time;
    time.Initialize();
    time.SetTime(0.0f);
    time.SetWeatherIntensity(0.5f);  // fractional intensity: the hard case
    time.SetWeather(WeatherState::Fog);

    const WeatherDefinition& clear = GetWeatherDefinition(WeatherState::Clear);
    const WeatherDefinition& fog = GetWeatherDefinition(WeatherState::Fog);

    ResolvedWeatherChannels captured;
    captured.ambient = glm::vec3(0.5f, 0.6f, 0.7f);
    captured.sky = glm::vec3(0.1f, 0.2f, 0.3f);
    captured.starVisibility = 0.42f;

    WeatherDefinition effective = BlendWeatherDefinitions(clear, fog, 0.0f);
    time.SetWeatherBlend(&clear, &fog, 0.0f, &effective);
    time.SetWeatherBlendResolvedFrom(captured);

    // At t=0 the getters must return the captured values verbatim.
    EXPECT_NEAR(time.GetAmbientColor().r, 0.5f, 1e-5f);
    EXPECT_NEAR(time.GetSkyColor().b, 0.3f, 1e-5f);
    EXPECT_NEAR(time.GetStarVisibility(), 0.42f, 1e-5f);
}

// Fades: derived from the effective def's bools when unset, published values
// when set, and cleared together with the blend.
TEST(TimeManagerBlend, FadesDeriveThenPublishThenClear)
{
    TimeManager time;
    time.Initialize();
    time.SetWeather(WeatherState::AuroraNight);
    EXPECT_FLOAT_EQ(time.GetAuroraFade(), 1.0f);     // showAurora = true
    EXPECT_FLOAT_EQ(time.GetCelestialFade(), 1.0f);  // showCelestialBodies default

    time.SetWeatherFades(0.25f, 0.75f);
    EXPECT_FLOAT_EQ(time.GetCelestialFade(), 0.25f);
    EXPECT_FLOAT_EQ(time.GetAuroraFade(), 0.75f);

    time.ClearWeatherBlend();
    EXPECT_FLOAT_EQ(time.GetAuroraFade(), 1.0f);  // back to bool-derived
}
