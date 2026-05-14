// Tests for the weather definition table and the EnumTraits<WeatherState>
// specialization. Renderer-free; validates the data layer that drives the
// weather system.

#include <gtest/gtest.h>

#include "../src/WeatherDefinitions.hpp"
#include "../src/TimeManager.hpp"

#include <utility>

TEST(WeatherDefinitionTests, EveryEnumValueResolves)
{
    // Walk every enum value and assert the table returns a usable definition.
    for (size_t i = 0; i < EnumTraits<WeatherState>::Count; ++i)
    {
        auto state = static_cast<WeatherState>(i);
        const WeatherDefinition& def = GetWeatherDefinition(state);
        // Tint must be non-negative.
        EXPECT_GE(def.ambientTintMultiplier.r, 0.0f);
        EXPECT_GE(def.ambientTintMultiplier.g, 0.0f);
        EXPECT_GE(def.ambientTintMultiplier.b, 0.0f);
    }
}

TEST(WeatherDefinitionTests, FromStringRoundTrips)
{
    for (size_t i = 0; i < EnumTraits<WeatherState>::Count; ++i)
    {
        auto state = static_cast<WeatherState>(i);
        std::string_view name = EnumTraits<WeatherState>::ToString(state);
        auto parsed = EnumTraits<WeatherState>::FromString(name);
        ASSERT_TRUE(parsed.has_value()) << "FromString failed for: " << name;
        EXPECT_EQ(*parsed, state);
    }
}

TEST(WeatherDefinitionTests, FromStringIsCaseSensitive)
{
    EXPECT_EQ(EnumTraits<WeatherState>::FromString("Thunderstorm"),
              std::optional<WeatherState>{WeatherState::Thunderstorm});
    EXPECT_FALSE(EnumTraits<WeatherState>::FromString("thunderstorm").has_value());
    EXPECT_FALSE(EnumTraits<WeatherState>::FromString("THUNDERSTORM").has_value());
}

TEST(WeatherDefinitionTests, FromStringRejectsGarbage)
{
    EXPECT_FALSE(EnumTraits<WeatherState>::FromString("").has_value());
    EXPECT_FALSE(EnumTraits<WeatherState>::FromString("nope").has_value());
}

TEST(WeatherDefinitionTests, ClearHasDefaults)
{
    const WeatherDefinition& def = GetWeatherDefinition(WeatherState::Clear);
    EXPECT_EQ(def.particleType, WeatherParticleType::None);
    EXPECT_EQ(def.lightningIntervalSeconds, 0.0f);
    EXPECT_FALSE(def.showAurora);
    EXPECT_TRUE(def.showCelestialBodies);
    EXPECT_FLOAT_EQ(def.meteorRateMultiplier, 1.0f);
}

TEST(WeatherDefinitionTests, ThunderstormHasLightning)
{
    const WeatherDefinition& def = GetWeatherDefinition(WeatherState::Thunderstorm);
    EXPECT_GT(def.lightningIntervalSeconds, 0.0f);
    EXPECT_EQ(def.particleType, WeatherParticleType::Rain);
    EXPECT_GT(def.maxWeatherParticles, 0);
}

TEST(WeatherDefinitionTests, AuroraNightShowsAurora)
{
    const WeatherDefinition& def = GetWeatherDefinition(WeatherState::AuroraNight);
    EXPECT_TRUE(def.showAurora);
    EXPECT_GT(def.starVisibilityOverride, 0.0f);
}

TEST(WeatherDefinitionTests, MeteorShowerBoostsMeteorRate)
{
    const WeatherDefinition& def = GetWeatherDefinition(WeatherState::MeteorShower);
    EXPECT_GT(def.meteorRateMultiplier, 1.0f);
}

TEST(WeatherDefinitionTests, OvercastDimsAmbient)
{
    const WeatherDefinition& def = GetWeatherDefinition(WeatherState::Overcast);
    EXPECT_LT(def.ambientTintMultiplier.r, 1.0f);
    EXPECT_LT(def.ambientTintMultiplier.g, 1.0f);
    EXPECT_FLOAT_EQ(def.starVisibilityOverride, 0.0f);
    EXPECT_FALSE(def.showCelestialBodies);
}

TEST(WeatherDefinitionTests, IntensityZeroProducesNeutralAmbient)
{
    // With intensity 0, GetAmbientColor should not be modified by the
    // weather tint at all (TimeManager mixes from glm::vec3(1) by intensity).
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(12.0f);
    tm.SetWeatherIntensity(0.0f);

    glm::vec3 clearColor = tm.GetAmbientColor();
    tm.SetWeather(WeatherState::Thunderstorm);
    glm::vec3 stormColor = tm.GetAmbientColor();

    EXPECT_FLOAT_EQ(clearColor.r, stormColor.r);
    EXPECT_FLOAT_EQ(clearColor.g, stormColor.g);
    EXPECT_FLOAT_EQ(clearColor.b, stormColor.b);
}

TEST(WeatherDefinitionTests, IntensityOneAppliesFullTint)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(12.0f);
    tm.SetWeather(WeatherState::Thunderstorm);
    tm.SetWeatherIntensity(1.0f);

    glm::vec3 stormColor = tm.GetAmbientColor();
    // Thunderstorm should darken substantially from the midday baseline.
    EXPECT_LT(stormColor.r, 0.7f);
    EXPECT_LT(stormColor.g, 0.7f);
}

TEST(WeatherDefinitionTests, SetWeatherIntensityClampsToRange)
{
    TimeManager tm;
    tm.Initialize();

    tm.SetWeatherIntensity(-0.5f);
    EXPECT_FLOAT_EQ(tm.GetWeatherIntensity(), 0.0f);

    tm.SetWeatherIntensity(1.5f);
    EXPECT_FLOAT_EQ(tm.GetWeatherIntensity(), 1.0f);

    tm.SetWeatherIntensity(0.5f);
    EXPECT_FLOAT_EQ(tm.GetWeatherIntensity(), 0.5f);
}

TEST(WeatherDefinitionTests, OvercastHidesStars)
{
    TimeManager tm;
    tm.Initialize();
    tm.SetTime(23.0f);  // Deep night
    tm.SetWeather(WeatherState::Clear);
    EXPECT_FLOAT_EQ(tm.GetStarVisibility(), 1.0f);

    tm.SetWeather(WeatherState::Overcast);
    tm.SetWeatherIntensity(1.0f);
    EXPECT_FLOAT_EQ(tm.GetStarVisibility(), 0.0f);
}

TEST(WeatherDefinitionTests, EnumTraitsCountMatchesEmberStorm)
{
    static_assert(EnumTraits<WeatherState>::Count == 19,
                  "WeatherState enum has 19 values");
    EXPECT_EQ(std::to_underlying(WeatherState::EmberStorm),
              EnumTraits<WeatherState>::Count - 1);
}

TEST(WeatherDefinitionTests, BlizzardHasSecondaryFog)
{
    const WeatherDefinition& def = GetWeatherDefinition(WeatherState::Blizzard);
    EXPECT_EQ(def.particleType, WeatherParticleType::Snow);
    EXPECT_EQ(def.secondaryParticleType, WeatherParticleType::Fog);
    EXPECT_GT(def.secondaryBaseSpawnRate, 0.0f);
    EXPECT_GT(def.secondaryMaxWeatherParticles, 0);
    // Blizzard's layered fog should be softer than the base alpha so the
    // snow remains the dominant visual element.
    EXPECT_LT(def.fogAlphaMultiplier, 1.0f);
}

TEST(WeatherDefinitionTests, FogStateSoftensAlpha)
{
    EXPECT_NEAR(GetWeatherDefinition(WeatherState::Fog).fogAlphaMultiplier, 0.7f, 0.001f);
}

TEST(WeatherDefinitionTests, MistSoftensAlphaMore)
{
    EXPECT_NEAR(GetWeatherDefinition(WeatherState::Mist).fogAlphaMultiplier, 0.6f, 0.001f);
}

TEST(WeatherDefinitionTests, NonFogWeathersHaveDefaultMultiplier)
{
    EXPECT_FLOAT_EQ(GetWeatherDefinition(WeatherState::Clear).fogAlphaMultiplier, 1.0f);
    EXPECT_FLOAT_EQ(GetWeatherDefinition(WeatherState::HeavyRain).fogAlphaMultiplier, 1.0f);
}

TEST(WeatherDefinitionTests, NonBlizzardWeathersHaveNoSecondaryParticle)
{
    for (size_t i = 0; i < EnumTraits<WeatherState>::Count; ++i)
    {
        auto state = static_cast<WeatherState>(i);
        if (state == WeatherState::Blizzard)
        {
            continue;
        }
        EXPECT_EQ(GetWeatherDefinition(state).secondaryParticleType,
                  WeatherParticleType::None)
            << "state=" << EnumTraits<WeatherState>::ToString(state);
    }
}
