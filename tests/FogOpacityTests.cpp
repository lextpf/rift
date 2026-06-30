#include <gtest/gtest.h>

#include "../src/ParticleSystem.hpp"
#include "../src/WeatherDefinitions.hpp"

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
    ps.SetTimeOfDay(12.0f);            // Midday: maximum day boost.
    ps.SetNightFactor(0.0f);           // Full day: worst case for fog opacity.
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
