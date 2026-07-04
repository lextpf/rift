#include <gtest/gtest.h>

#include "../src/ParticleSystem.hpp"
#include "../src/TimeManager.hpp"
#include "../src/WeatherDefinitions.hpp"
#include "../src/WeatherDirector.hpp"

#include <glm/glm.hpp>

/// @file FogOpacityTests.cpp
/// @brief Regression guard: fog must stay a light haze, never an opaque wall.
///
/// Bug: fog rendered "way too strong in any weather and any particle zone." Two
/// compounding causes in ParticleBehavior<ParticleType::Fog>::Update:
///   1. Editor-zone fog borrowed the active weather's fogAlphaMultiplier, which
///      defaults to 1.0 outside the four fog-bearing weathers - so a fog zone
///      under Clear/Rain/etc. rendered fully un-softened.
///   2. The shared base alpha (0.40) and day boost (1.4x) stacked across many
///      large overlapping puffs into a near-opaque sheet.
///
/// These tests pin the per-puff alpha ceiling. They drive the real ParticleSystem
/// at full-day settings (the worst case for fog opacity) and assert no live Fog
/// particle ever exceeds a soft ceiling. They FAIL on the pre-fix constants
/// (zone peak ~0.56, weather peak ~0.36) and pass on the tuned ones
/// (zone peak ~0.14, weather peak ~0.19).
namespace
{
/// Highest alpha across all currently-live Fog particles (0 if none).
float MaxFogAlpha(const ParticleSystem& ps)
{
    float maxA = 0.0f;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == ParticleType::Fog)
        {
            maxA = std::max(maxA, p.color.a);
        }
    }
    return maxA;
}

int FogCount(const ParticleSystem& ps)
{
    int n = 0;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == ParticleType::Fog)
        {
            ++n;
        }
    }
    return n;
}
}  // namespace

// Editor-zone fog under NO fog weather (the default-1.0 multiplier path). This is
// the headline "any particle zone is too strong" case.
TEST(FogOpacity, ZoneFogStaysLightUnderNonFogWeather)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);   // Midday: maximum day boost.
    ps.SetNightFactor(0.0f);  // Full day: worst case for fog opacity.
    ps.SetPlayerPosition({0.0f, 0.0f});
    ps.SetMaxParticlesPerZone(200);

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};

    // One fog zone covering the viewport. No weather is set, so the active
    // weather definition is null and fogAlphaMultiplier would default to 1.0 -
    // exactly the pre-fix over-thick condition. Post-fix, zone fog uses its own
    // fixed 0.5 softening regardless.
    std::vector<ParticleZone> zones;
    zones.emplace_back(cameraPos, viewSize, ParticleType::Fog);
    ps.SetZones(&zones);

    // Run long enough for puffs to fully fade in and reach their peak alpha.
    for (int i = 0; i < 120; ++i)
    {
        ps.Update(0.2f, cameraPos, viewSize);
        EXPECT_LE(MaxFogAlpha(ps), 0.20f) << "zone fog too opaque at frame " << i;
    }

    EXPECT_GT(FogCount(ps), 0) << "test vacuous: no fog spawned";
}

// Weather-driven fog (Fog weather) - the "any weather is too strong" case. Even
// with the per-weather fogAlphaMultiplier (0.65 for Fog), the puff must stay a
// readable haze.
TEST(FogOpacity, WeatherFogStaysLight)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetPlayerPosition({0.0f, 0.0f});

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};

    // Drive the Fog weather at full intensity; no editor zones.
    ps.SetWeatherState(&GetWeatherDefinition(WeatherState::Fog), 1.0f);

    for (int i = 0; i < 120; ++i)
    {
        ps.Update(0.2f, cameraPos, viewSize);
        EXPECT_LE(MaxFogAlpha(ps), 0.25f) << "weather fog too opaque at frame " << i;
    }

    EXPECT_GT(FogCount(ps), 0) << "test vacuous: no weather fog spawned";
}

// Density guard for the dedicated Fog weather. Pre-fix it pinned at its 5000
// per-type cap (a wall of overlapping puffs); the lowered spawn rate (180->110)
// and cap (5000->2500) hold the live population far below that. At this viewport
// the Fog weather is cap-bound, so this asserts the cap reduction specifically.
TEST(FogOpacity, WeatherFogPopulationStaysBounded)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};
    ps.SetWeatherState(&GetWeatherDefinition(WeatherState::Fog), 1.0f);

    // Run well past the fill time so the population reaches steady state.
    for (int i = 0; i < 200; ++i)
    {
        ps.Update(0.2f, cameraPos, viewSize);
    }

    EXPECT_GT(FogCount(ps), 0) << "test vacuous: no weather fog spawned";
    EXPECT_LE(FogCount(ps), 3000) << "weather fog population back to a wall";
}

// A Fog -> Clear transition must respect the alpha ceiling through the blend
// AND through the post-transition decay window (residual puffs outlive the
// transition by up to ~18 s; the naive hold-until-t=1 popped at completion).
TEST(FogOpacity, FogToClearTransitionHoldsCeiling)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetPlayerPosition({0.0f, 0.0f});

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};

    TimeManager time;
    time.Initialize();
    time.SetTime(12.0f);
    time.SetWeather(WeatherState::Fog);
    WeatherDirector director;
    director.SetEnabled(true);

    // Fill the fog population at steady state first.
    ps.SetWeatherState(&time.GetEffectiveWeatherDefinition(), 1.0f);
    for (int i = 0; i < 120; ++i)
    {
        ps.Update(0.2f, cameraPos, viewSize);
    }
    ASSERT_GT(FogCount(ps), 0) << "test vacuous: no fog at steady state";

    // 10 s transition to Clear, then run 20 s past completion (decay window
    // is 18 s). 0.2 s steps: 50 transition frames + 100 decay frames.
    director.RequestWeather(time, WeatherState::Clear, 10.0f);
    for (int i = 0; i < 150; ++i)
    {
        director.Update(0.2f, time);
        ps.SetWind(director.GetWindDirection(), director.GetWindStrength());
        const WeatherDirector::SpawnStreams streams = director.GetSpawnStreams();
        ps.SetWeatherTransition(streams.outgoing, streams.incoming, streams.weight);
        ps.SetWeatherState(&time.GetEffectiveWeatherDefinition(), time.GetWeatherIntensity());
        ps.Update(0.2f, cameraPos, viewSize);
        EXPECT_LE(MaxFogAlpha(ps), 0.25f) << "fog popped during transition at frame " << i;
        EXPECT_LE(FogCount(ps), 3000) << "fog population wall at frame " << i;
    }
}

// Fog -> Blizzard: both endpoints spawn Fog-type particles; the shared-type
// cap floor must keep the population bounded mid-blend.
TEST(FogOpacity, FogToBlizzardTransitionKeepsPopulationBounded)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetPlayerPosition({0.0f, 0.0f});

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};

    TimeManager time;
    time.Initialize();
    time.SetTime(12.0f);
    time.SetWeather(WeatherState::Fog);
    WeatherDirector director;
    director.SetEnabled(true);

    ps.SetWeatherState(&time.GetEffectiveWeatherDefinition(), 1.0f);
    for (int i = 0; i < 120; ++i)
    {
        ps.Update(0.2f, cameraPos, viewSize);
    }
    ASSERT_GT(FogCount(ps), 0) << "test vacuous: no fog at steady state";

    director.RequestWeather(time, WeatherState::Blizzard, 10.0f);
    for (int i = 0; i < 100; ++i)
    {
        director.Update(0.2f, time);
        ps.SetWind(director.GetWindDirection(), director.GetWindStrength());
        const WeatherDirector::SpawnStreams streams = director.GetSpawnStreams();
        ps.SetWeatherTransition(streams.outgoing, streams.incoming, streams.weight);
        ps.SetWeatherState(&time.GetEffectiveWeatherDefinition(), time.GetWeatherIntensity());
        ps.Update(0.2f, cameraPos, viewSize);
        EXPECT_LE(FogCount(ps), 3000) << "fog population wall at frame " << i;
    }
}
